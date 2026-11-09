// gl2d9's procedure in the chain of the game's window (subclassing): what it does with the
// game's messages while Glide is open on the window (HookProc), taking it in and out of the
// chain (Hook, Unhook), and putting the game's window back as it was once Glide closes on it
// (PutBack, RestoreClosed). See window.cpp for the takeover itself.
#include <algorithm>

#include "windowint.h"

namespace gl2d9 {
namespace win {

namespace {

StaleHook g_stale[8];

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
        return CallProc(nullptr, IsWindowUnicode(h) != FALSE, h, msg, wp, lp);
    }
    depth++;
    LRESULT r;
    try {
        r = HookProcBody(h, msg, wp, lp);
    } catch (...) {
        // (Not into Windows' frames either: the message left to Windows, as on a failed call.
        // The game's own procedure throwing (Visual C++'s exceptions) isn't caught here. Logged
        // only: this thread never waits for gl2d9's lock, which the drawing thread may hold
        // while it waits for this one, and the batch it is building isn't at fault.)
        depth--;
        LogCallFailed("the window procedure");
        return CallProc(nullptr, IsWindowUnicode(h) != FALSE, h, msg, wp, lp);
    }
    depth--;
    return r;
}

LRESULT HookProcBody(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    // Whether gl2d9 is open on this window, and the game's procedure, read together: Glide's
    // drawing thread may close on the window and open again meanwhile (a message sent on with
    // the procedure of a WindowState started anew, none, was lost to the game).
    bool open, mine;
    WNDPROC orig;
    bool unicode;
    {
        HookGuard guard;
        mine = h == g_w.hwnd;
        open = mine && g_w.hooked;
        orig = g_w.orig;
        unicode = g_w.unicode;
    }
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
            if (open) {
                away = g_w.suspended;
            } else {
                bool stale;
                {
                    HookGuard guard;
                    stale = FindStale(h) != nullptr;
                }
                away = stale && !OursInFront();
            }
            if (away && !IsIconic(h) && !OnWine()) ShowWindow(h, SW_SHOWMINNOACTIVE);  // (Wine: see Activate)
            return 0;
        }
        if (!open) return 0;
        if (wp == kHideCursor) {
            if (!g_w.suspended) SetCursor(nullptr);
        } else if (wp == kActivate) {
            if (g_w.suspended && g_board.open && OursInFront()) Activate(true);  // (see WindowOpened)
        } else if (!g_w.suspended) {
            ShowNow();  // (the board may not be open yet)
        }
        return 0;
    }
    if (!open) {
        // A window gl2d9 is no longer open on, still in its chain: its own procedure.
        bool stale = false, restore = false, unminimize = false;
        {
            HookGuard guard;
            if (StaleHook* e = FindStale(h)) {
                orig = e->orig;
                unicode = e->unicode;
                stale = true;
                if (msg == WM_NCDESTROY) *e = StaleHook{};
                if (msg == WM_ACTIVATEAPP && wp && e->waitFocus) {
                    e->waitFocus = false;
                    restore = true;
                }
                unminimize = msg == WM_ACTIVATEAPP && wp && OnWine() && IsIconic(h);
            }
        }
        // Wine: the focus back on it while it is minimized: restored first, then the game told.
        // Unreal minimizes its window as it loses the focus (Glide closed), and opens Glide again
        // only once it is restored; on Windows the taskbar or Alt+Tab restores it, but switching
        // back to its workspace under i3 gave it the focus still minimized, and nothing restored
        // it (Nerf Arena Blast: black until switching through an empty workspace). Through the
        // game's own procedure, as Wine restores a window the window manager shows again
        // (WM_SYSCOMMAND, SC_RESTORE), before the game handles the focus coming back.
        if (unminimize && IsIconic(h)) {
            GL_LOG("window %p: the focus back on it minimized (Wine): restored", static_cast<void*>(h));
            SendMessageW(h, WM_SYSCOMMAND, SC_RESTORE, 0);
        }
        // (Wine: the focus back on a window Glide closed on while away: put back now, unless
        // Glide is opened on it again first. See RestoreClosed.)
        if (restore && ShowMsg()) PostMessageW(h, ShowMsg(), kRestore, 0);
        if (stale || mine) return CallProc(orig, unicode, h, msg, wp, lp);
        return CallProc(nullptr, IsWindowUnicode(h) != FALSE, h, msg, wp, lp);
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
    case WM_ACTIVATEAPP: Activate(wp != 0); break;
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
        RefreshIfFree();
        break;
    case WM_ERASEBKGND:
        if (g_board.open) return 1;  // the picture covers it
        break;
    case WM_WINDOWPOSCHANGING:
    case WM_WINDOWPOSCHANGED:
    case WM_SIZE:
    case WM_MOVE:
        if (g_w.quiet) return CallProc(nullptr, unicode, h, msg, wp, lp);
        if (msg == WM_SIZE) RefreshIfFree();
        break;
    case WM_GETMINMAXINFO: {
        // A covering window may be larger than the default maximum tracking size.
        const LRESULT r = CallProc(orig, unicode, h, msg, wp, lp);
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
        const LRESULT r = CallProc(orig, unicode, h, msg, wp, lp);
        HookGuard guard;
        if (h != g_w.hwnd) return r;  // (closed on meanwhile)
        if (g_w.orig && GetProc(h, g_w.unicode) == reinterpret_cast<LONG_PTR>(HookProc))
            SetProc(h, g_w.unicode, reinterpret_cast<LONG_PTR>(g_w.orig));
        g_w.hooked = false;
        g_w.hwnd = nullptr;
        g_board.hwnd = nullptr;
        return r;
    }
    default: break;
    }
    return CallProc(orig, unicode, h, msg, wp, lp);
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

// The window's frame: the style bits Cover and Windowed change. (Its state, minimized, maximized
// or shown, is the window's own, kept: compared whole, a window minimized meanwhile looked
// restyled by the game, and was left borderless.)
const LONG kFrameBits = WS_POPUP | WS_OVERLAPPEDWINDOW | WS_DLGFRAME | WS_BORDER;
const LONG kStateBits = WS_MINIMIZE | WS_MAXIMIZE | WS_VISIBLE;

}  // namespace

StaleHook* FindStale(HWND h) {
    for (StaleHook& e : g_stale)
        if (e.hwnd && e.hwnd == h) return &e;
    return nullptr;
}

// The procedure orig (none: Windows' default), in the window's character set.
LRESULT CallProc(WNDPROC orig, bool unicode, HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (!orig) return unicode ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
    return unicode ? CallWindowProcW(orig, h, msg, wp, lp) : CallWindowProcA(orig, h, msg, wp, lp);
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
    // (Under g_hookLock throughout, for HookProcBody: setting the procedure sends nothing.)
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

// keep: left in the chain with what to put back (WindowClose from another thread; kRestore puts
// it back, then unhooks). Whether it was left in the chain so. (Under g_hookLock.)
bool Unhook(const StaleHook* keep) {
    if (!g_w.hooked || !g_w.hwnd || !IsWindow(g_w.hwnd)) return false;
    g_w.hooked = false;
    const bool top = GetProc(g_w.hwnd, g_w.unicode) == reinterpret_cast<LONG_PTR>(HookProc);
    // Something subclassed the window after gl2d9: it stays in the chain.
    const bool kept = (keep || !top) && LeaveHooked(keep ? *keep : StaleHook{g_w.hwnd, g_w.orig, g_w.unicode});
    if (!kept && top) SetProc(g_w.hwnd, g_w.unicode, reinterpret_cast<LONG_PTR>(g_w.orig));
    return kept && keep;
}

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
        wp.showCmd = OnWine() && !IsIconic(h) ? SW_SHOWNOACTIVATE : SW_SHOWMINNOACTIVE;  // (Wine: see Activate)
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
    // Under Wine, the focus still away: left as gl2d9 had it, its hook kept, until the focus is
    // back (HookProcBody's WM_ACTIVATEAPP). Putting the game's style back there made the window
    // the window manager's anew, and it moved it to the workspace switched to (openbox, Unreal
    // closing Glide as it loses the focus: black on coming back). A game opening Glide on it
    // again first (Unreal, once back) takes it as it is (Hook).
    const bool wait = OnWine() && !OursInFront();
    StaleHook r;
    {
        HookGuard guard;
        StaleHook* e = FindStale(h);
        if (!e || !e->restore) return;
        if (wait && e->away) {
            e->waitFocus = true;
            return;
        }
        r = *e;
        e->restore = false;
        e->waitFocus = false;
    }
    // (Its frame, and, shown and covering, its place: a frameless game's window has gl2d9's
    // frame anyway. Not in window mode: the player may have moved the window.)
    RECT now = {};
    const bool covering = !(r.ourStyle & WS_CAPTION);
    const bool moved = covering && !IsIconic(h) && GetWindowRect(h, &now) && !EqualRect(&now, &r.ourRect);
    if ((GetWindowLongW(h, GWL_STYLE) & kFrameBits) == (r.ourStyle & kFrameBits) && !moved) PutBack(h, r);
    else GL_LOG("window %p: restyled or moved by the game since Glide closed: left as it is", static_cast<void*>(h));
    HookGuard guard;
    StaleHook* e = FindStale(h);
    if (e && !e->restore && GetProc(h, e->unicode) == reinterpret_cast<LONG_PTR>(HookProc)) {
        SetProc(h, e->unicode, reinterpret_cast<LONG_PTR>(e->orig));
        *e = StaleHook{};
    }
}

}  // namespace win
}  // namespace gl2d9
