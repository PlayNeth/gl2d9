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
#include <cmath>

#include "render.h"
#include "voodoo.h"

namespace gl2d9 {

namespace {

struct WindowState {
    HWND hwnd = nullptr;
    bool own = false;          // made by gl2d9
    bool unicode = false;
    WNDPROC orig = nullptr;    // the game's procedure (subclassed)
    bool hooked = false;
    LONG style = 0, exStyle = 0;  // as the game had them
    RECT rect = {};
    bool restyled = false;     // style, exStyle and rect kept (KeepGameStyle); with ourStyle, under g_hookLock
    LONG ourStyle = 0;         // the style gl2d9 gave the window (Cover, Windowed)
    RECT ourRect = {};         // and where it put it
    int display = kDisplayBorderless;  // as opened (switch falls back to borderless)
    bool switched = false;     // the display mode is the screen's
    wchar_t device[CCHDEVICENAME] = {};
    bool suspended = false;    // focus away: minimized, or opened with another program in front
    bool quiet = false;        // gl2d9 moving the window itself
};
WindowState g_w;

// For the StaleHooks and the kept style (g_w.restyled, style, exStyle, rect, ourStyle), which
// the window's thread and a game's drawing thread both change. Held briefly, never while
// sending a window a message; taken after gl2d9's lock where both are.
struct HookLock {
    CRITICAL_SECTION cs;
    HookLock() { InitializeCriticalSection(&cs); }
} g_hookLock;
struct HookGuard {
    HookGuard() { EnterCriticalSection(&g_hookLock.cs); }
    ~HookGuard() { LeaveCriticalSection(&g_hookLock.cs); }
    HookGuard(const HookGuard&) = delete;
    HookGuard& operator=(const HookGuard&) = delete;
};

// A kHideCursor posted and not handled yet (one at a time: asked each frame).
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

// Whether the calling thread is the window's own.
bool OnWindowThread(HWND h) { return GetWindowThreadProcessId(h, nullptr) == GetCurrentThreadId(); }

const wchar_t kClass[] = L"gl2d9";

// The monitor the screen is shown on: the primary one (as a Voodoo game's own window is
// usually on it), or the window's own in window mode.
HMONITOR GameMonitor(HWND h) {
    if (g_w.display == kDisplayWindow && h) return MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    const POINT origin = {0, 0};
    return MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
}

RECT MonitorRect(HWND h, wchar_t* device = nullptr) {
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(GameMonitor(h), &mi)) {
        mi.rcMonitor = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        mi.szDevice[0] = 0;
    }
    if (device) lstrcpynW(device, mi.szDevice, CCHDEVICENAME);
    return mi.rcMonitor;
}

// SetWindowPos from the window's own thread, or asynchronously from another (whose
// waiting for that thread could deadlock with a game thread waiting for this one).
void PlaceWindow(HWND insertAfter, const RECT& r, UINT flags) {
    if (!OnWindowThread(g_w.hwnd)) flags |= SWP_ASYNCWINDOWPOS;
    g_w.quiet = true;
    SetWindowPos(g_w.hwnd, insertAfter, r.left, r.top, r.right - r.left, r.bottom - r.top, flags);
    g_w.quiet = false;
}

bool SwitchMode() {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = DWORD(g_board.screenW);
    dm.dmPelsHeight = DWORD(g_board.screenH);
    dm.dmBitsPerPel = 32;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    // The refresh rate grSstWinOpen asked for, where the monitor has it.
    static const int kRates[] = {60, 70, 72, 75, 80, 90, 100, 85, 120};
    if (g_board.refresh >= 0 && g_board.refresh < int(sizeof(kRates) / sizeof(kRates[0]))) {
        dm.dmDisplayFrequency = DWORD(kRates[g_board.refresh]);
        dm.dmFields |= DM_DISPLAYFREQUENCY;
        if (ChangeDisplaySettingsExW(g_w.device, &dm, nullptr, CDS_FULLSCREEN | CDS_TEST, nullptr) !=
            DISP_CHANGE_SUCCESSFUL)
            dm.dmFields &= ~DM_DISPLAYFREQUENCY;
    }
    const LONG r = ChangeDisplaySettingsExW(g_w.device, &dm, nullptr, CDS_FULLSCREEN, nullptr);
    if (r != DISP_CHANGE_SUCCESSFUL) {
        GL_LOG("display: %dx%d not set (%ld): shown scaled instead", g_board.screenW, g_board.screenH, r);
        return false;
    }
    GL_LOG("display: switched to %dx%d", g_board.screenW, g_board.screenH);
    return true;
}

void RestoreMode() {
    if (!g_w.switched) return;
    ChangeDisplaySettingsExW(g_w.device, nullptr, nullptr, 0, nullptr);
    g_w.switched = false;
    GL_LOG("display: desktop mode restored");
}

// The style, extended style and place the game gave its window, kept before gl2d9 changes
// them (put back by WindowClose). (Opened again on a window closed and not yet put back
// (kRestore), Hook took those it had then.)
void KeepGameStyle() {
    if (g_w.own) return;
    {
        HookGuard guard;
        if (g_w.restyled) return;
    }
    const LONG style = GetWindowLongW(g_w.hwnd, GWL_STYLE), ex = GetWindowLongW(g_w.hwnd, GWL_EXSTYLE);
    RECT rect = {};
    GetWindowRect(g_w.hwnd, &rect);
    HookGuard guard;
    g_w.style = style;
    g_w.exStyle = ex;
    g_w.rect = rect;
    g_w.restyled = true;
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
    KeepGameStyle();
    SetWindowLongW(g_w.hwnd, GWL_STYLE,
                   (GetWindowLongW(g_w.hwnd, GWL_STYLE) & ~(WS_OVERLAPPEDWINDOW | WS_DLGFRAME | WS_BORDER)) | WS_POPUP);
    SetWindowLongW(
        g_w.hwnd, GWL_EXSTYLE,
        GetWindowLongW(g_w.hwnd, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
    PlaceWindow(HWND_TOPMOST, m, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
    NoteOurStyle(m);
}

// An ordinary window, its client area the screen's size, centered on its monitor's work area.
void Windowed() {
    KeepGameStyle();
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

// The picture drawn again where nothing else is drawing (a game drawing on another thread
// presents its next frame anyway).
void RefreshIfFree() {
    if (!TryEnterCriticalSection(&g_lock)) return;
    if (g_board.open) RenderRefresh();
    LeaveCriticalSection(&g_lock);
}

UINT ShowMsg();
void RestoreClosed(HWND h);
enum : WPARAM { kShow, kMinimize, kRestore, kHideCursor, kActivate };  // ShowMsg's wParam

// What has the foreground, for the log: its class, and whose it is.
void DescribeForeground(char* out, size_t size) {
    HWND f = GetForegroundWindow();
    char cls[64] = "none";
    if (f) GetClassNameA(f, cls, sizeof(cls));
    DWORD pid = 0;
    if (f) GetWindowThreadProcessId(f, &pid);
    snprintf(out, size, "%s, %s", cls, !f ? "-" : f == g_w.hwnd ? "the game's window"
                                            : pid == GetCurrentProcessId() ? "this program's"
                                                                           : "another program's");
}

// Focus lost or back (WM_ACTIVATEAPP), on the window's own thread. Without gl2d9's lock: a
// game drawing on another thread holds it most of the time (through Present, waiting for
// the display), and the focus change, waiting for it, was dropped (Unreal: the window left
// over the desktop, or minimized for good). What this changes is the window's, on its own
// thread; the device, lost by a mode change, is Reset by the thread that draws.
void Activate(bool active) {
    if (g_w.display == kDisplayWindow || !g_w.hwnd || !g_board.open) return;
    char fg[128];
    if (!active && !g_w.suspended) {
        DescribeForeground(fg, sizeof(fg));
        GL_LOG("focus lost: %s (foreground: %s)", g_w.switched ? "desktop mode restored, minimizing" : "minimizing",
               fg);
        g_w.suspended = true;
        RestoreMode();
        PlaceWindow(HWND_NOTOPMOST, RECT{}, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        // Minimized once Windows has finished moving the focus, not inside WM_ACTIVATEAPP: a
        // minimize there (SW_MINIMIZE, which activates the next window) made a second switch
        // within Windows' own, which gave the game the focus back at once, its window left
        // minimized (Windows' Alt+Tab, NFS 3: the mouse held by the game, the game not coming
        // back). SW_SHOWMINNOACTIVE activates nothing; and done only if the focus is still
        // away by then (ShowMsg's handler).
        if (!ShowMsg() || !PostMessageW(g_w.hwnd, ShowMsg(), kMinimize, 0)) ShowWindowAsync(g_w.hwnd, SW_SHOWMINNOACTIVE);
    } else if (active && g_w.suspended) {
        DescribeForeground(fg, sizeof(fg));
        GL_LOG("focus back (foreground: %s)", fg);
        g_w.suspended = false;
        if (IsIconic(g_w.hwnd)) ShowWindow(g_w.hwnd, SW_RESTORE);
        // (The game's own procedure, restoring, may have closed Glide: no mode switched for it.)
        if (!g_board.open || !g_w.hwnd) return;
        if (g_w.display == kDisplaySwitch) g_w.switched = SwitchMode();
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
void Show() {
    if (g_w.hooked && !OnWindowThread(g_w.hwnd) && ShowMsg() &&
        PostMessageW(g_w.hwnd, ShowMsg(), kShow, 0))
        return;
    ShowNow();
}

// Windows gl2d9 couldn't unhook (something subclassed them after it, dd2d9 for a game's
// DirectDraw menus): HookProc stays in their chains, passing their messages to the procedure
// it replaced. Opened again on one, gl2d9 takes that hook back rather than subclassing again:
// a second HookProc, below the other subclass, would call it, which calls the first HookProc,
// and so on (NFS High Stakes, 640x480 menus then a 1600x1200 race, with dd2d9: the stack
// overflowed at once, nothing logged).
// Closed from another thread (WindowClose), a StaleHook keeps too what kRestore puts back:
// the game's style, extended style and place, over the style gl2d9 gave the window only
// (ourStyle: one the game set since is left alone), and whether the focus was away (the window
// kept minimized).
struct StaleHook {
    HWND hwnd = nullptr;
    WNDPROC orig = nullptr;
    bool unicode = false;
    bool restore = false;
    LONG style = 0, exStyle = 0, ourStyle = 0;
    RECT rect = {}, ourRect = {};
    bool away = false;
};
StaleHook g_stale[8];

StaleHook* FindStale(HWND h) {
    for (StaleHook& e : g_stale)
        if (e.hwnd && e.hwnd == h) return &e;
    return nullptr;
}

LRESULT CallProc(WNDPROC orig, bool unicode, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (!orig) return unicode ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
    return unicode ? CallWindowProcW(orig, h, msg, wp, lp) : CallWindowProcA(orig, h, msg, wp, lp);
}

LRESULT CallOrig(HWND h, UINT msg, WPARAM wp, LPARAM lp) { return CallProc(g_w.orig, g_w.unicode, h, msg, wp, lp); }

LONG_PTR GetProc(HWND h, bool unicode) {
    return unicode ? GetWindowLongPtrW(h, GWLP_WNDPROC) : GetWindowLongPtrA(h, GWLP_WNDPROC);
}
LONG_PTR SetProc(HWND h, bool unicode, LONG_PTR proc) {
    return unicode ? SetWindowLongPtrW(h, GWLP_WNDPROC, proc) : SetWindowLongPtrA(h, GWLP_WNDPROC, proc);
}

LRESULT HookProcBody(HWND h, UINT msg, WPARAM wp, LPARAM lp);

// The window procedure gl2d9 puts in front of the game's. Nested more than 200 deep on one
// thread it can only be calling itself round a loop of subclasses (two HookProcs in one chain,
// which StaleHook prevents): logged once and broken there, rather than the stack overflowing
// with nothing logged.
LRESULT CALLBACK HookProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    static thread_local int depth;
    if (depth >= 200) {
        static LONG logged;
        if (InterlockedExchange(&logged, 1) == 0)
            GL_LOG("window %p: its window procedures call each other round a loop (message 0x%x): broken there",
                   static_cast<void*>(h), msg);
        return IsWindowUnicode(h) ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
    }
    depth++;
    LRESULT r;
    try {
        r = HookProcBody(h, msg, wp, lp);
    } catch (...) {
        // (Not into Windows' frames either: the message left to Windows, as on a failed call.
        // The game's own procedure throwing (Visual C++'s exceptions) isn't caught here.)
        depth--;
        CallFailed("the window procedure");
        return IsWindowUnicode(h) ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
    }
    depth--;
    return r;
}

LRESULT HookProcBody(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    // gl2d9's own, never the game's: the window covered or made a window (Show), or
    // minimized with the focus lost (Activate).
    if (const UINT show = ShowMsg(); show && msg == show) {
        if (wp == kHideCursor) InterlockedExchange(&g_cursorAsked, 0);  // (whatever became of the window)
        if (wp == kRestore) {
            RestoreClosed(h);
            return 0;
        }
        if (wp == kMinimize) {
            // Still away: open on it (suspended), or closed on it since, another program in front.
            bool away = false;
            if (h == g_w.hwnd && g_w.hooked) {
                away = g_w.suspended;
            } else {
                bool stale;
                {
                    HookGuard guard;
                    stale = FindStale(h) != nullptr;
                }
                away = stale && !OursInFront();
            }
            if (away && !IsIconic(h)) ShowWindow(h, SW_SHOWMINNOACTIVE);
            return 0;
        }
        if (h != g_w.hwnd || !g_w.hooked) return 0;
        if (wp == kHideCursor) {
            if (!g_w.suspended) SetCursor(nullptr);
        } else if (wp == kActivate) {
            if (g_w.suspended && g_board.open && OursInFront()) Activate(true);  // (see WindowOpened)
        } else if (!g_w.suspended) {
            ShowNow();  // (the board may not be open yet)
        }
        return 0;
    }
    if (h != g_w.hwnd || !g_w.hooked) {
        // A window gl2d9 is no longer open on, still in its chain: its own procedure.
        WNDPROC orig = nullptr;
        bool unicode = false, stale = false;
        {
            HookGuard guard;
            if (StaleHook* e = FindStale(h)) {
                orig = e->orig;
                unicode = e->unicode;
                stale = true;
                if (msg == WM_NCDESTROY) *e = StaleHook{};
            }
        }
        if (stale) return CallProc(orig, unicode, h, msg, wp, lp);
        if (h == g_w.hwnd) return CallOrig(h, msg, wp, lp);
        return IsWindowUnicode(h) ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
    }
    switch (msg) {
    case WM_SETCURSOR:
        // No Windows cursor over the picture: the Voodoo's output never had one (a game draws
        // its own). Games that capture the mouse get no WM_SETCURSOR: see WM_MOUSEMOVE.
        if (g_board.open && LOWORD(lp) == HTCLIENT) {
            SetCursor(nullptr);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (g_board.open && GetCursor()) SetCursor(nullptr);
        break;
    case WM_ACTIVATEAPP:
        Activate(wp != 0);
        break;
    case WM_PAINT:
        if (g_w.suspended) {
            // Focus away: painted here, not by the game. A game may take the foreground back
            // as it paints (NFS 3: SetForegroundWindow in its WM_PAINT while fullscreen), and
            // a paint comes with the switch itself (Alt+Tab's switcher over the window): the
            // game was back in front at once, the desktop only flashing.
            PAINTSTRUCT ps;
            if (BeginPaint(h, &ps)) EndPaint(h, &ps);
            return 0;
        }
        if (g_board.open && TryEnterCriticalSection(&g_lock)) {
            RenderRefresh();
            LeaveCriticalSection(&g_lock);
        }
        break;
    case WM_ERASEBKGND:
        if (g_board.open) return 1;  // the picture covers it
        break;
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
    case WM_SIZE:
    case WM_MOVE:
        if (g_w.quiet) return g_w.unicode ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
        if (msg == WM_SIZE && g_board.open && TryEnterCriticalSection(&g_lock)) {
            RenderRefresh();
            LeaveCriticalSection(&g_lock);
        }
        break;
    case WM_GETMINMAXINFO: {
        // A covering window may be larger than the default maximum tracking size.
        const LRESULT r = CallOrig(h, msg, wp, lp);
        if (lp && g_w.display != kDisplayWindow) {
            const RECT m = MonitorRect(h);
            auto* mmi = reinterpret_cast<MINMAXINFO*>(lp);
            mmi->ptMaxTrackSize.x = std::max<LONG>(mmi->ptMaxTrackSize.x, m.right - m.left);
            mmi->ptMaxTrackSize.y = std::max<LONG>(mmi->ptMaxTrackSize.y, m.bottom - m.top);
        }
        return r;
    }
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_SCREENSAVE && g_board.open) return 0;
        break;
    case WM_NCDESTROY: {
        const LRESULT r = CallOrig(h, msg, wp, lp);
        if (g_w.orig && GetProc(h, g_w.unicode) == reinterpret_cast<LONG_PTR>(HookProc))
            SetProc(h, g_w.unicode, reinterpret_cast<LONG_PTR>(g_w.orig));
        g_w.hooked = false;
        g_w.hwnd = nullptr;
        g_board.hwnd = nullptr;
        return r;
    }
    default:
        break;
    }
    return CallOrig(h, msg, wp, lp);
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
    HINSTANCE inst = GetModuleHandleW(nullptr);
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

void Hook() {
    // The hook procedure may stay in the window's chain (something subclassed after it):
    // the DLL is kept loaded for the rest of the process.
    static bool pinned;
    if (!pinned) {
        HMODULE self = nullptr;
        pinned = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                                    reinterpret_cast<LPCWSTR>(&HookProc), &self) != FALSE;
    }
    {
        HookGuard guard;
        if (StaleHook* e = FindStale(g_w.hwnd)) {  // still in its chain from before: taken back
            g_w.unicode = e->unicode;
            g_w.orig = e->orig;
            g_w.hooked = true;
            if (e->restore) {  // closed, not put back yet: the game's style is the one kept then
                g_w.style = e->style;
                g_w.exStyle = e->exStyle;
                g_w.rect = e->rect;
                g_w.ourStyle = e->ourStyle;
                g_w.ourRect = e->ourRect;
                g_w.restyled = true;
            }
            GL_LOG("window %p: still hooked from before (%s)", static_cast<void*>(g_w.hwnd),
                   e->restore ? "closed, not put back yet" : "something subclassed it since");
            *e = StaleHook{};
            return;
        }
    }
    g_w.unicode = IsWindowUnicode(g_w.hwnd) != FALSE;
    g_w.orig = reinterpret_cast<WNDPROC>(GetProc(g_w.hwnd, g_w.unicode));
    const LONG_PTR prev = SetProc(g_w.hwnd, g_w.unicode, reinterpret_cast<LONG_PTR>(HookProc));
    if (!prev) {
        GL_LOG("window %p not hooked (%lu)", static_cast<void*>(g_w.hwnd), GetLastError());
        return;
    }
    g_w.orig = reinterpret_cast<WNDPROC>(prev);
    g_w.hooked = true;
}

// gl2d9's hook left in the window's chain, passing messages on, remembered (StaleHook) for
// gl2d9 opening on it again. Whether there was room. (Under g_hookLock.)
bool LeaveHooked(const StaleHook& entry) {
    StaleHook* e = FindStale(entry.hwnd);
    for (StaleHook& s : g_stale)
        if (!e && (!s.hwnd || !IsWindow(s.hwnd))) e = &s;
    if (!e) {
        GL_LOG("window %p: left hooked, no room to remember it", static_cast<void*>(entry.hwnd));
        return false;
    }
    *e = entry;
    return true;
}

// keep: left in the chain with what to put back (WindowClose from another thread; kRestore puts
// it back, then unhooks). Whether it was left in the chain so.
bool Unhook(const StaleHook* keep = nullptr) {
    if (!g_w.hooked || !g_w.hwnd || !IsWindow(g_w.hwnd)) return false;
    HookGuard guard;
    g_w.hooked = false;
    const bool top = GetProc(g_w.hwnd, g_w.unicode) == reinterpret_cast<LONG_PTR>(HookProc);
    // Something subclassed the window after gl2d9: it stays in the chain.
    const bool kept = (keep || !top) && LeaveHooked(keep ? *keep : StaleHook{g_w.hwnd, g_w.orig, g_w.unicode});
    if (!kept && top) SetProc(g_w.hwnd, g_w.unicode, reinterpret_cast<LONG_PTR>(g_w.orig));
    return kept && keep;
}

// The window's frame: the style bits Cover and Windowed change. (Its state, minimized, maximized
// or shown, is the window's own, kept: compared whole, a window minimized meanwhile looked
// restyled by the game, and was left borderless.)
const LONG kFrameBits = WS_POPUP | WS_OVERLAPPEDWINDOW | WS_DLGFRAME | WS_BORDER;
const LONG kStateBits = WS_MINIMIZE | WS_MAXIMIZE | WS_VISIBLE;

// The game's style, extended style and place put back on its window (Glide closed), its state
// kept. Minimized, or the focus away (the game not in front, nothing in front counting, as in
// the middle of the switch: a game closing Glide as it loses the focus): its place set as
// where it comes back to (its normal position), minimized. Else placed behind the program in
// front, if another's (above it, a game left in the background came in front of the program
// switched to). (From another thread than the window's, the rare fallback of WindowClose:
// the moves are asynchronous, the style changes wait for the window's thread.)
void PutBack(HWND h, const StaleHook& r) {
    const LONG state = GetWindowLongW(h, GWL_STYLE) & kStateBits;
    SetWindowLongW(h, GWL_STYLE, (r.style & ~kStateBits) | state);
    SetWindowLongW(h, GWL_EXSTYLE, r.exStyle);
    const UINT async = OnWindowThread(h) ? 0 : SWP_ASYNCWINDOWPOS;
    const bool topmost = (r.exStyle & WS_EX_TOPMOST) != 0;
    const HWND band = topmost ? HWND_TOPMOST : HWND_NOTOPMOST;  // (covering, it was topmost)
    if (IsIconic(h) || (r.away && !OursInFront())) {
        WINDOWPLACEMENT wp = {};
        wp.length = sizeof(wp);
        GetWindowPlacement(h, &wp);
        wp.rcNormalPosition = r.rect;
        if (!(r.exStyle & WS_EX_TOOLWINDOW)) {
            // (In workspace coordinates: the primary monitor's work area's origin is 0,0.)
            MONITORINFO mi = {};
            mi.cbSize = sizeof(mi);
            const POINT origin = {0, 0};
            GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi);
            OffsetRect(&wp.rcNormalPosition, mi.rcMonitor.left - mi.rcWork.left, mi.rcMonitor.top - mi.rcWork.top);
        }
        wp.showCmd = SW_SHOWMINNOACTIVE;
        wp.flags = 0;
        SetWindowPlacement(h, &wp);
        SetWindowPos(h, band, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_FRAMECHANGED | SWP_NOACTIVATE | async);
        return;
    }
    // Behind another program in front (not a topmost one: behind it, a topmost window would stay
    // in the topmost band; inserted after a window that isn't, it leaves it), in one move.
    const HWND front = GetForegroundWindow();
    const bool behind = !topmost && AnotherInFront() && front && !(GetWindowLongW(front, GWL_EXSTYLE) & WS_EX_TOPMOST);
    SetWindowPos(h, behind ? front : band, r.rect.left, r.rect.top, r.rect.right - r.rect.left,
                 r.rect.bottom - r.rect.top, SWP_FRAMECHANGED | SWP_NOACTIVATE | async);
}

// kRestore, on the window's own thread: the style the game had put back (if the window still
// has gl2d9's), minimized if the focus is still away, and gl2d9's hook taken out of its chain
// (unless something subclassed it since). Opened on again first: nothing (Hook took it).
void RestoreClosed(HWND h) {
    StaleHook r;
    {
        HookGuard guard;
        StaleHook* e = FindStale(h);
        if (!e || !e->restore) return;
        r = *e;
        e->restore = false;
    }
    // (Its frame, and, shown and covering, its place: a frameless game's window has gl2d9's
    // frame anyway. Not in window mode: the player may have moved the window.)
    RECT now = {};
    const bool covering = !(r.ourStyle & WS_CAPTION);
    const bool moved = covering && !IsIconic(h) && GetWindowRect(h, &now) && !EqualRect(&now, &r.ourRect);
    if ((GetWindowLongW(h, GWL_STYLE) & kFrameBits) == (r.ourStyle & kFrameBits) && !moved)
        PutBack(h, r);
    else
        GL_LOG("window %p: restyled or moved by the game since Glide closed: left as it is", static_cast<void*>(h));
    HookGuard guard;
    StaleHook* e = FindStale(h);
    if (e && !e->restore && GetProc(h, e->unicode) == reinterpret_cast<LONG_PTR>(HookProc)) {
        SetProc(h, e->unicode, reinterpret_cast<LONG_PTR>(e->orig));
        *e = StaleHook{};
    }
}

}  // namespace

HWND WindowMakeHelper() {
    static const wchar_t kHelperClass[] = L"gl2d9helper";
    static bool registered;
    HINSTANCE inst = GetModuleHandleW(nullptr);
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

RECT ImageRect(int cw, int ch) {
    const int sw = g_board.screenW, sh = g_board.screenH;
    if (g_config.stretch == 1 || cw <= 0 || ch <= 0 || sw <= 0 || sh <= 0) return RECT{0, 0, cw, ch};
    double s = std::min(double(cw) / sw, double(ch) / sh);
    if (g_config.stretch == 2 && s >= 1) s = std::floor(s);
    const int w = int(sw * s + 0.5), h = int(sh * s + 0.5);
    const int x = (cw - w) / 2, y = (ch - h) / 2;
    return RECT{x, y, x + w, y + h};
}

bool MonitorHasMode(int w, int h) {
    // The primary monitor's modes (the one the screen covers), listed once.
    static std::vector<std::pair<DWORD, DWORD>> modes = [] {
        std::vector<std::pair<DWORD, DWORD>> m;
        MONITORINFOEXW mi = {};
        mi.cbSize = sizeof(mi);
        const POINT origin = {0, 0};
        const wchar_t* device =
            GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi) ? mi.szDevice : nullptr;
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        for (DWORD i = 0; EnumDisplaySettingsW(device, i, &dm); i++) m.push_back({dm.dmPelsWidth, dm.dmPelsHeight});
        return m;
    }();
    if (modes.empty()) return true;  // (nothing listed: everything allowed)
    for (const auto& m : modes)
        if (m.first == DWORD(w) && m.second == DWORD(h)) return true;
    return false;
}

void FilterGameResolutionLists() {
    if (!g_config.filterResolutions) return;
    static bool done;
    if (done) return;
    done = true;
    HMODULE m = GetModuleHandleW(L"GlideDrv.dll");
    if (!m) return;
    static const wchar_t kList[] = L"512x384 640x480 800x600 1024x768 1280x1024 1600x1200";
    const size_t len = sizeof(kList) / sizeof(kList[0]) - 1;
    // The module's sections searched for the list (each section mapped whole: readable).
    auto* base = reinterpret_cast<FxU8*>(m);
    const auto* nt =
        reinterpret_cast<const IMAGE_NT_HEADERS*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    wchar_t* found = nullptr;
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && !found; i++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        FxU8* p = base + sec->VirtualAddress;
        const size_t n = std::min<size_t>(sec->Misc.VirtualSize, sec->SizeOfRawData);
        for (size_t at = 0; at + sizeof(kList) <= n; at += 2) {
            if (memcmp(p + at, kList, sizeof(kList)) == 0) {
                found = reinterpret_cast<wchar_t*>(p + at);
                break;
            }
        }
    }
    if (!found) {
        GL_LOG("resolutions: GlideDrv.dll's list not found (left as it is)");
        return;
    }
    // The entries the monitor has (640x480 always: every game runs in it), in their order.
    std::wstring out;
    const std::wstring all(kList);
    for (size_t pos = 0; pos < all.size();) {
        size_t end = all.find(L' ', pos);
        if (end == std::wstring::npos) end = all.size();
        const std::wstring e = all.substr(pos, end - pos);
        pos = end + 1;
        int w = 0, h = 0;
        if (swscanf(e.c_str(), L"%dx%d", &w, &h) != 2) continue;
        if (!(w == 640 && h == 480) && !MonitorHasMode(w, h)) continue;
        if (!out.empty()) out += L' ';
        out += e;
    }
    DWORD old;
    if (!VirtualProtect(found, sizeof(kList), PAGE_READWRITE, &old)) return;
    memset(found, 0, sizeof(kList));
    memcpy(found, out.c_str(), std::min(out.size(), len) * sizeof(wchar_t));
    VirtualProtect(found, sizeof(kList), old, &old);
    char narrow[sizeof(kList)];
    WideCharToMultiByte(CP_UTF8, 0, out.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    GL_LOG("resolutions: GlideDrv.dll's list made the monitor's: %s", narrow);
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
    HWND h = requested && IsWindow(requested) ? requested : nullptr;
    if (!h) {
        h = GetActiveWindow();
        // Only a window of this process's (another one's can't be subclassed).
        DWORD pid = 0;
        if (h) GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) h = nullptr;
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
        GL_LOG("window: %p%s", static_cast<void*>(h), requested ? "" : " (the game's, none given)");
    }
    g_w.hwnd = h;
    g_board.hwnd = h;
    g_board.ownWindow = g_w.own;
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
        PlaceWindow(HWND_NOTOPMOST, RECT{}, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (!IsIconic(h)) ShowWindowAsync(h, SW_SHOWMINNOACTIVE);
    }
    openedBefore = true;
    if (g_w.display == kDisplaySwitch && !g_w.suspended) {
        g_w.switched = SwitchMode();
        if (!g_w.switched) g_w.display = kDisplayBorderless;
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
    if (!g_w.hwnd) return;
    bool restyled;
    StaleHook back;
    {
        HookGuard guard;
        restyled = g_w.restyled;
        back = StaleHook{g_w.hwnd,    g_w.orig,     g_w.unicode, true,           g_w.style,
                         g_w.exStyle, g_w.ourStyle, g_w.rect,    g_w.ourRect,    g_w.suspended};
    }
    if (g_w.own) {
        DestroyWindow(g_w.hwnd);
    } else if (restyled && g_w.hooked && IsWindow(g_w.hwnd) && !OnWindowThread(g_w.hwnd) && ShowMsg() &&
               Unhook(&back)) {
        // From another thread: put back on the window's (kRestore), gl2d9's hook kept till then.
        if (!PostMessageW(g_w.hwnd, ShowMsg(), kRestore, 0)) {
            HookGuard guard;  // (a full queue: left as gl2d9 had it, rather than risk a hang)
            if (StaleHook* e = FindStale(back.hwnd)) e->restore = false;
        }
    } else {
        Unhook();
        // (Closed with the focus away, the game closing Glide as it loses it: kept minimized.)
        if (restyled && IsWindow(g_w.hwnd)) PutBack(g_w.hwnd, back);
    }
    g_w.hwnd = nullptr;
    g_board.hwnd = nullptr;
    g_board.ownWindow = false;
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
    // Asks after the thread's input, as games did each frame (and gl2d9's screenshot key, before
    // 1.1.0): on Wine that is when the window's new size (covering the monitor) reaches the
    // X server for a game that doesn't pump its messages while drawing (else the picture was
    // shown at the window's old size, cut off). It dispatches nothing.
    GetQueueStatus(QS_ALLINPUT);
    if (!g_w.own || !g_w.hwnd) return;
    MSG m;
    while (PeekMessageW(&m, g_w.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

}  // namespace gl2d9
