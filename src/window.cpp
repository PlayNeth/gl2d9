// See window.h. A Voodoo 1/2 drew on its own monitor, the 2D card's desktop untouched: the
// game's window only had to exist. Here the screen is shown in that window, so it is set up
// as the configuration says (display=):
//  - borderless: the window covers the game's monitor, the screen scaled into it;
//  - switch: the monitor switched to the screen's resolution, the window covering it;
//  - window: an ordinary window with a client area of the screen's size.
// With focus lost a covering window is minimized (and the display mode put back); with it
// back it covers the monitor again. The mouse isn't translated: a Voodoo 1/2 left the
// desktop at its own resolution, and games were written for its coordinates. The Windows
// cursor is hidden over the picture (the Voodoo's output never showed it).
#include "window.h"

#include <algorithm>

#include "core.h"
#include "windowint.h"

extern "C" IMAGE_DOS_HEADER __ImageBase;  // (the linker's: this DLL's own image)

namespace gl2d9 {
namespace win {

WindowState g_w;
HookLock g_hookLock;
volatile LONG g_cursorAsked;

// Another program's window in front (none in front, as in the middle of a switch: not).
bool AnotherInFront() {
    const HWND f = GetForegroundWindow();
    DWORD pid = 0;
    return f && GetWindowThreadProcessId(f, &pid) && pid != GetCurrentProcessId();
}

// This process's window in front (none in front, as in the middle of a switch: not).
bool OursInFront() {
    const HWND f = GetForegroundWindow();
    DWORD pid = 0;
    return f && GetWindowThreadProcessId(f, &pid) && pid == GetCurrentProcessId();
}

namespace {

// gl2d9's window classes, registered under this DLL's instance (their procedure is in it):
// glide2x.dll and glide3x.dll loaded together each have their own, and unregistered as the DLL
// is unloaded (WindowUnload), loaded again they are registered anew rather than found with
// the procedure of the image gone.
const wchar_t kClass[] = L"gl2d9";
const wchar_t kHelperClass[] = L"gl2d9helper";
HINSTANCE Self() { return reinterpret_cast<HINSTANCE>(&__ImageBase); }

// SetWindowPos from the window's own thread, or asynchronously from another (whose
// waiting for that thread could deadlock with a game thread waiting for this one). Quiet only
// on the window's own thread, where the move's messages come while SetWindowPos runs: set from
// another, it hid the game's own moves handled meanwhile, and not the posted one.
void PlaceWindow(HWND insertAfter, const RECT& r, UINT flags) {
    const bool own = OnWindowThread(g_w.hwnd);
    if (!own) flags |= SWP_ASYNCWINDOWPOS;
    if (own) g_w.quiet = true;
    SetWindowPos(g_w.hwnd, insertAfter, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
    if (own) g_w.quiet = false;
}

// The style, extended style and place the game gave its window h, kept before gl2d9 changes
// them (put back by WindowClose). (Opened again on a window closed and not yet put back
// (kRestore), Hook took those it had then.) False: Glide was closed on it meanwhile by another
// thread (this one being the window's): left alone, as WindowClose puts back only what was
// kept by then.
bool KeepGameStyle(HWND h) {
    if (g_w.own) return true;
    {
        HookGuard guard;
        if (g_w.hwnd != h) return false;
        if (g_w.restyled) return true;
    }
    const LONG style = GetWindowLongW(h, GWL_STYLE), ex = GetWindowLongW(h, GWL_EXSTYLE);
    RECT rect = {};
    GetWindowRect(h, &rect);
    HookGuard guard;
    if (g_w.hwnd != h) return false;
    g_w.style = style;
    g_w.exStyle = ex;
    g_w.rect = rect;
    g_w.restyled = true;
    return true;
}

// The style gl2d9 has given the window, and where it put it (where it is, placed from its own
// thread: a window manager may have adjusted it): put back on closing only over them
// (RestoreClosed).
void NoteOurStyle(const RECT& placed) {
    const LONG style = GetWindowLongW(g_w.hwnd, GWL_STYLE);
    RECT r = placed;
    if (OnWindowThread(g_w.hwnd)) GetWindowRect(g_w.hwnd, &r);
    HookGuard guard;
    g_w.ourStyle = style;
    g_w.ourRect = r;
}

// The window over the whole monitor, frameless, in front.
void Cover() {
    const RECT m = MonitorRect(g_w.hwnd, g_w.device);
    if (!KeepGameStyle(g_w.hwnd)) return;
    SetWindowLongW(g_w.hwnd, GWL_STYLE,
                   (GetWindowLongW(g_w.hwnd, GWL_STYLE) & ~(WS_OVERLAPPEDWINDOW | WS_DLGFRAME | WS_BORDER)) | WS_POPUP);
    SetWindowLongW(
        g_w.hwnd, GWL_EXSTYLE,
        GetWindowLongW(g_w.hwnd, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
    // Topmost, but not under Wine: there a window covering the screen is the window manager's
    // fullscreen, above its panels while in front; and taking topmost away as the focus went
    // (Activate) moved the window to the workspace switched to (openbox; Nerf Arena Blast on
    // the player's desktop: black on coming back, until switching again now and then worked).
    PlaceWindow(OnWine() ? HWND_TOP : HWND_TOPMOST, m, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    NoteOurStyle(m);
}

// An ordinary window, its client area the screen's size, centered on its monitor's work area.
void Windowed() {
    if (!KeepGameStyle(g_w.hwnd)) return;
    LONG style = GetWindowLongW(g_w.hwnd, GWL_STYLE);
    if (!(style & WS_CAPTION)) {  // a frameless game window: given a frame to move it by
        style = (style & ~WS_POPUP) | WS_OVERLAPPEDWINDOW;
        SetWindowLongW(g_w.hwnd, GWL_STYLE, style);
    }
    const LONG ex = GetWindowLongW(g_w.hwnd, GWL_EXSTYLE) & ~WS_EX_TOPMOST;
    SetWindowLongW(g_w.hwnd, GWL_EXSTYLE, ex);
    RECT r = {0, 0, g_board.screenW, g_board.screenH};
    AdjustWindowRectEx(&r, DWORD(style), GetMenu(g_w.hwnd) != nullptr, DWORD(ex));
    MONITORINFO mi = {};
    mi.cbSize = sizeof(mi);
    GetMonitorInfoW(GameMonitor(g_w.hwnd), &mi);
    const LONG w = r.right - r.left, h = r.bottom - r.top;
    const LONG x = mi.rcWork.left + std::max<LONG>(0, (mi.rcWork.right - mi.rcWork.left - w) / 2);
    const LONG y = mi.rcWork.top + std::max<LONG>(0, (mi.rcWork.bottom - mi.rcWork.top - h) / 2);
    PlaceWindow(HWND_NOTOPMOST, RECT{x, y, x + w, y + h}, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    NoteOurStyle(RECT{x, y, x + w, y + h});
}

}  // namespace

// The picture drawn again where nothing else is drawing (a game drawing on another thread
// presents its next frame anyway).
void RefreshIfFree() {
    if (!TryEnterCriticalSection(&g_lock)) return;
    if (g_board.open) RenderRefresh();
    LeaveCriticalSection(&g_lock);
}

namespace {

// What has the foreground, for the log: its class, and whose it is.
void DescribeForeground(char* out, size_t size) {
    HWND f = GetForegroundWindow();
    char cls[64] = "none";
    if (f) GetClassNameA(f, cls, sizeof(cls));
    DWORD pid = 0;
    if (f) GetWindowThreadProcessId(f, &pid);
    snprintf(out, size, "%s, %s", cls,
             !f                             ? "-"
             : f == g_w.hwnd                ? "the game's window"
             : pid == GetCurrentProcessId() ? "this program's"
                                            : "another program's");
}

}  // namespace

// Focus lost or back (WM_ACTIVATEAPP), on the window's own thread. Without gl2d9's lock: a
// game drawing on another thread holds it most of the time (through Present, waiting for
// the display), and the focus change, waiting for it, was dropped (Unreal: the window left
// over the desktop, or minimized for good). What this changes is the window's, on its own
// thread; the device, lost by a mode change, is Reset by the thread that draws.
// The focus lost counts from the window's opening on, the board not open yet: a game opening
// Glide on its drawing thread has the window covered on this one (kShow) while the device is
// still being made there, and covered after a focus loss dropped then, it stayed in front of
// the program switched to. (Suspended, kShow covers nothing; back, WindowOpened's kActivate
// covers it once the board is open.)
void Activate(bool active) {
    // (Read once: a close on the drawing thread clears it meanwhile.)
    const HWND h = g_w.hwnd;
    if (g_w.display == kDisplayWindow || !h) return;
    char fg[128];
    if (!active && !g_w.suspended) {
        DescribeForeground(fg, sizeof(fg));
        GL_LOG("focus lost: %s%s (foreground: %s)", g_switched ? "desktop mode restored, " : "",
               OnWine() ? "not topmost (Wine: not minimized)" : "minimizing", fg);
        g_w.suspended = true;
        RestoreMode();
        if (!OnWine()) PlaceWindow(HWND_NOTOPMOST, RECT{}, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // Minimized once Windows has finished moving the focus, not inside WM_ACTIVATEAPP: a
        // minimize there (SW_MINIMIZE, which activates the next window) made a second switch
        // within Windows' own, which gave the game the focus back at once, its window left
        // minimized (Windows' Alt+Tab, NFS 3: the mouse held by the game, the game not coming
        // back). SW_SHOWMINNOACTIVE activates nothing; and done only if the focus is still
        // away by then (ShowMsg's handler). Under Wine neither, nor topmost taken away (never
        // set there: see Cover): switching workspaces is a focus loss there, and the window,
        // moved to the workspace switched to, or iconified, was black on coming back, gl2d9
        // restoring it only once the game had the focus again (Nerf Arena Blast: switching
        // back and forth until the game showed, Wine's minimized caption on black). The window
        // manager hides a window on another workspace and stacks the one in front.
        if (!OnWine()) {
            if (!ShowMsg() || !PostMessageW(h, ShowMsg(), kMinimize, 0)) ShowWindowAsync(h, SW_SHOWMINNOACTIVE);
        }
    } else if (active && g_w.suspended && g_board.open) {
        DescribeForeground(fg, sizeof(fg));
        GL_LOG("focus back (foreground: %s)", fg);
        g_w.suspended = false;
        if (IsIconic(h)) ShowWindow(h, SW_RESTORE);
        // (The game's own procedure, restoring, may have closed Glide: no mode switched for it.)
        if (!g_board.open || !g_w.hwnd) return;
        if (g_w.display == kDisplaySwitch) SwitchMode();
        // Closed by the drawing thread meanwhile (CoreWinClose clears open first, then
        // RestoreMode reads g_switched; here SwitchMode set g_switched, then open is read: with
        // a locked instruction between each pair, one of the two threads sees the other's
        // change): the desktop's mode put back here, else it stayed the game's after Glide.
        if (!g_board.open) {
            RestoreMode();
            return;
        }
        Cover();
        RefreshIfFree();
    }
}

// The window covered or made a window (as display= says), on its own thread: from another
// (a game opening Glide on its drawing thread), asked of it (ShowMsg), so that gl2d9's own
// moves are known as such there (quiet) and no thread waits for the other with the lock held.
UINT ShowMsg() {
    static const UINT msg = RegisterWindowMessageW(L"gl2d9 show");
    return msg;
}
void ShowNow() {
    if (g_w.display == kDisplayWindow) Windowed();
    else Cover();
}
namespace {

void Show() {
    if (g_w.hooked && !OnWindowThread(g_w.hwnd) && ShowMsg() && PostMessageW(g_w.hwnd, ShowMsg(), kShow, 0)) return;
    ShowNow();
}

LRESULT CALLBACK OwnProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CLOSE:
        // A console program has no other way out: as with closing its console.
        GL_LOG("own window closed: exiting");
        ExitProcess(0);
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    default: break;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

HWND MakeOwnWindow() {
    static bool registered;
    const HINSTANCE inst = Self();
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = OwnProc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursorW(nullptr, reinterpret_cast<LPCWSTR>(IDC_ARROW));
        wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
        wc.lpszClassName = kClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    return CreateWindowExW(0, kClass, L"gl2d9", WS_POPUP, 0, 0, g_board.screenW, g_board.screenH, nullptr, nullptr,
                           inst, nullptr);
}

}  // namespace

}  // namespace win

using namespace win;

HWND WindowMakeHelper() {
    static bool registered;
    const HINSTANCE inst = Self();
    if (!registered) {
        WNDCLASSEXW wc = {};
        wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = DefWindowProcW;
        wc.hInstance = inst;
        wc.lpszClassName = kHelperClass;
        registered = RegisterClassExW(&wc) != 0;
    }
    return CreateWindowExW(WS_EX_TOOLWINDOW, kHelperClass, L"gl2d9", WS_POPUP, 0, 0, 16, 16, nullptr, nullptr, inst,
                           nullptr);
}

void WindowDestroyHelper(HWND h) {
    if (h && IsWindow(h)) DestroyWindow(h);
}

void WindowUnload() {
    // gl2d9's own window left open by a program unloading Glide without closing it: its
    // procedure goes with the DLL. (Only a window of this thread's can be destroyed here; a
    // class with windows left isn't unregistered.)
    if (g_w.own && g_w.hwnd && OnWindowThread(g_w.hwnd)) DestroyWindow(g_w.hwnd);
    UnregisterClassW(kClass, Self());
    UnregisterClassW(kHelperClass, Self());
}

SIZE WindowClientSize() {
    RECT c = {};
    if (g_w.hwnd && !g_w.suspended && g_w.display != kDisplayWindow) {
        const RECT m = MonitorRect(g_w.hwnd);
        return SIZE{m.right - m.left, m.bottom - m.top};
    }
    // A window: Windowed's client size, the game's resolution, though the window may not be
    // there yet (placed on its own thread, a game drawing on another).
    if (g_w.display == kDisplayWindow && g_board.screenW > 0 && g_board.screenH > 0)
        return SIZE{g_board.screenW, g_board.screenH};
    if (g_w.hwnd && GetClientRect(g_w.hwnd, &c) && c.right > 0 && c.bottom > 0) return SIZE{c.right, c.bottom};
    return SIZE{g_board.screenW, g_board.screenH};
}

namespace {
struct FindMain {
    HWND best = nullptr;
    bool bestOwnThread = false;
    LONG bestArea = 0;
};
BOOL CALLBACK FindMainProc(HWND w, LPARAM p) {
    auto* f = reinterpret_cast<FindMain*>(p);
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(w, &pid);
    if (pid != GetCurrentProcessId() || !IsWindowVisible(w) || GetWindow(w, GW_OWNER)) return TRUE;
    RECT r;
    if (!GetWindowRect(w, &r)) return TRUE;
    const LONG area = (r.right - r.left) * (r.bottom - r.top);
    const bool own = tid == GetCurrentThreadId();
    if (!f->best || (own && !f->bestOwnThread) || (own == f->bestOwnThread && area > f->bestArea)) {
        f->best = w;
        f->bestOwnThread = own;
        f->bestArea = area;
    }
    return TRUE;
}
// The largest visible top-level window of this process's, the calling thread's first (the
// game's main window); null if it has none.
HWND GameMainWindow() {
    FindMain f;
    EnumWindows(FindMainProc, reinterpret_cast<LPARAM>(&f));
    return f.best;
}
}  // namespace

bool WindowOpen(HWND requested) {
    WindowClose();
    {
        HookGuard guard;  // (restyled and the style kept: the window's thread reads them)
        g_w = WindowState{};
    }
    g_w.display = g_config.display;
    InterlockedExchange(&g_cursorAsked, 0);
    // Only a window of this process's: another one's can't be subclassed, and covering the
    // monitor would have moved that program's window (a stale or garbage handle from the game).
    auto ours = [](HWND w) {
        DWORD pid = 0;
        return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
    };
    HWND h = ours(requested) ? requested : nullptr;
    if (requested && !h) GL_LOG("window %p given: not one of this program's", static_cast<void*>(requested));
    if (!h) {
        h = GetActiveWindow();
        if (!ours(h)) h = nullptr;
    }
    // None given and none active (Glide 2.1's grSstOpen takes no window): the game's own main
    // window, as on a Voodoo the game kept it (and its focus); gl2d9's own only without one.
    if (!h) h = GameMainWindow();
    if (!h) {
        h = MakeOwnWindow();
        if (!h) {
            GL_LOG("could not make a window (%lu)", GetLastError());
            return false;
        }
        g_w.own = true;
        GL_LOG("window: gl2d9's own %p", static_cast<void*>(h));
    } else {
        GL_LOG("window: %p%s", static_cast<void*>(h), h == requested ? "" : " (the game's, none given)");
    }
    {
        HookGuard guard;
        g_w.hwnd = h;
    }
    g_board.hwnd = h;
    if (!g_w.own) Hook();
    MonitorRect(h, g_w.device);
    // Opened again with another program in front (switched away from, the game reopening Glide
    // meanwhile: NFS 3 between its menus and a race): not covering the screen over that
    // program, nor the display mode switched. Taken as the focus lost: done once the game has
    // it (Activate). Not at the first open: a launcher's or the console's window may still be
    // in front then.
    static bool openedBefore;
    if (openedBefore && !g_w.own && g_w.display != kDisplayWindow && AnotherInFront()) {
        g_w.suspended = true;
        GL_LOG("window: another program in front, the screen covered once the game has the focus");
        // As with the focus lost (Activate): minimized, not topmost (it may still be as gl2d9
        // left it, closed and opened again before its own thread put it back).
        if (!OnWine()) {  // (Wine: see Activate)
            PlaceWindow(HWND_NOTOPMOST, RECT{}, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            if (!IsIconic(h)) ShowWindowAsync(h, SW_SHOWMINNOACTIVE);
        }
    }
    openedBefore = true;
    if (g_w.display == kDisplaySwitch && !g_w.suspended && !SwitchMode()) g_w.display = kDisplayBorderless;
    // Minimized, with the game in front: restored first (as Activate does with the focus back).
    // Unreal minimizes its own window as it loses the focus, and closes Glide; back, it has the
    // focus again, still minimized, and opens Glide on it: the window covered the screen
    // minimized, black (i3, until Wine's own restore came first now and then). From another
    // thread asynchronously, before Show's (queued after it on the window's thread).
    if (!g_w.suspended && IsIconic(h) && OursInFront()) {
        GL_LOG("window: minimized with the game in front: restored");
        if (OnWindowThread(h)) ShowWindow(h, SW_RESTORE);
        else ShowWindowAsync(h, SW_RESTORE);
    }
    if (!g_w.suspended) Show();
    if (g_w.own) {
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
    }
    return true;
}

// Opened with another program in front (WindowOpen), and the game in front by now (its focus
// back before the board was open, which Activate ignored): covered now, on the window's thread.
void WindowOpened() {
    if (g_w.suspended && g_w.hooked && OursInFront() && ShowMsg()) PostMessageW(g_w.hwnd, ShowMsg(), kActivate, 0);
}

void WindowClose() {
    RestoreMode();
    // Unhooked, and no longer open on the window, in one step for its thread (HookProcBody,
    // KeepGameStyle: nothing restyled after the style kept is read here); then put back, which
    // sends the game's procedure messages: one that opens Glide again from there (on this
    // thread: gl2d9's lock is recursive) starts from a closed window, not from this one half
    // closed.
    enum { kNothing, kDestroy, kPost, kRestoreNow, kPutBack } then;
    HWND h;
    StaleHook back;
    {
        HookGuard guard;
        h = g_w.hwnd;
        if (!h) return;
        const bool restyled = g_w.restyled;
        back = StaleHook{h,           g_w.orig,     g_w.unicode, true,        g_w.style,
                         g_w.exStyle, g_w.ourStyle, g_w.rect,    g_w.ourRect, g_w.suspended};
        if (g_w.own) {
            then = kDestroy;
        } else if (restyled && g_w.hooked && IsWindow(h) && !OnWindowThread(h) && ShowMsg() && Unhook(&back)) {
            then = kPost;  // from another thread: put back on the window's, gl2d9's hook kept till then
        } else if (restyled && OnWine() && back.away && g_w.hooked && IsWindow(h) && Unhook(&back)) {
            then = kRestoreNow;  // (Wine, closed with the focus away: put back once it is back)
        } else {
            Unhook();
            then = restyled && IsWindow(h) ? kPutBack : kNothing;
        }
        g_w.hwnd = nullptr;
    }
    g_board.hwnd = nullptr;
    switch (then) {
    case kDestroy: DestroyWindow(h); break;
    case kPost:
        if (!PostMessageW(h, ShowMsg(), kRestore, 0)) {
            HookGuard guard;  // (a full queue: left as gl2d9 had it, rather than risk a hang)
            if (StaleHook* e = FindStale(h)) e->restore = false;
        }
        break;
    case kRestoreNow:
        RestoreClosed(h);
        break;  // (see RestoreClosed)
    // (Closed with the focus away, the game closing Glide as it loses it: kept minimized.)
    case kPutBack: PutBack(h, back); break;
    case kNothing: break;
    }
}

// The cursor hidden over the picture each frame too: a game that captures the mouse (Unreal)
// gets no WM_SETCURSOR, and keeps the cursor its window class has. The cursor is set per
// thread (its input): from a game drawing on another thread than its window's, SetCursor did
// nothing (Unreal's arrow over the picture), so the window's thread is asked (kHideCursor).
static void HideCursor() {
    HWND hwnd = g_w.hwnd;
    if (!hwnd || g_w.suspended) return;
    const DWORD windowThread = GetWindowThreadProcessId(hwnd, nullptr);
    const bool own = windowThread == GetCurrentThreadId();
    if (own) {
        if (!GetCursor()) return;
    } else {
        CURSORINFO ci = {};
        ci.cbSize = sizeof(ci);
        if (!GetCursorInfo(&ci) || !(ci.flags & CURSOR_SHOWING) || !ci.hCursor) return;
    }
    GUITHREADINFO gti = {};
    gti.cbSize = sizeof(gti);
    const HWND capture = own ? GetCapture() : GetGUIThreadInfo(windowThread, &gti) ? gti.hwndCapture : nullptr;
    if (capture != hwnd) {
        POINT p;
        RECT rc;
        if (!GetCursorPos(&p) || WindowFromPoint(p) != hwnd || !ScreenToClient(hwnd, &p) || !GetClientRect(hwnd, &rc) ||
            !PtInRect(&rc, p))
            return;
    }
    if (own) SetCursor(nullptr);
    else if (g_w.hooked && ShowMsg() && InterlockedExchange(&g_cursorAsked, 1) == 0 &&
             !PostMessageW(hwnd, ShowMsg(), kHideCursor, 0))
        InterlockedExchange(&g_cursorAsked, 0);
}

void WindowPump() {
    HideCursor();
    // Under Wine, asks after the thread's input, as games did each frame (and gl2d9's screenshot
    // key, before 1.1.0): that is when the window's new size (covering the monitor) reaches the
    // X server for a game that doesn't pump its messages while drawing (else the picture was
    // shown at the window's old size, cut off). It dispatches nothing. Not on Windows, which
    // needs none of it: there it marked the input queued as seen, and a game waiting for input
    // (WaitMessage, MsgWaitForMultipleObjects) after its swap didn't wake for a key already in
    // its queue.
    if (OnWine()) GetQueueStatus(QS_ALLINPUT);
    if (!g_w.own || !g_w.hwnd) return;
    MSG m;
    while (PeekMessageW(&m, g_w.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

}  // namespace gl2d9
