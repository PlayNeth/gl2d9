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
    bool restyled = false;
    int display = kDisplayBorderless;  // as opened (switch falls back to borderless)
    bool switched = false;     // the display mode is the screen's
    wchar_t device[CCHDEVICENAME] = {};
    bool suspended = false;    // minimized with focus lost
    bool quiet = false;        // gl2d9 moving the window itself
};
WindowState g_w;

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
    if (GetWindowThreadProcessId(g_w.hwnd, nullptr) != GetCurrentThreadId()) flags |= SWP_ASYNCWINDOWPOS;
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
        if (ChangeDisplaySettingsExW(g_w.device, &dm, nullptr, CDS_FULLSCREEN | CDS_TEST, nullptr) != DISP_CHANGE_SUCCESSFUL)
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

// The window over the whole monitor, frameless, in front.
void Cover() {
    const RECT m = MonitorRect(g_w.hwnd, g_w.device);
    if (!g_w.restyled && !g_w.own) {
        g_w.style = GetWindowLongW(g_w.hwnd, GWL_STYLE);
        g_w.exStyle = GetWindowLongW(g_w.hwnd, GWL_EXSTYLE);
        GetWindowRect(g_w.hwnd, &g_w.rect);
        g_w.restyled = true;
    }
    SetWindowLongW(g_w.hwnd, GWL_STYLE, (GetWindowLongW(g_w.hwnd, GWL_STYLE) & ~(WS_OVERLAPPEDWINDOW | WS_DLGFRAME | WS_BORDER)) | WS_POPUP);
    SetWindowLongW(g_w.hwnd, GWL_EXSTYLE, GetWindowLongW(g_w.hwnd, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
    PlaceWindow(HWND_TOPMOST, m, SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

// An ordinary window, its client area the screen's size, centered on its monitor's work area.
void Windowed() {
    if (!g_w.restyled && !g_w.own) {
        g_w.style = GetWindowLongW(g_w.hwnd, GWL_STYLE);
        g_w.exStyle = GetWindowLongW(g_w.hwnd, GWL_EXSTYLE);
        GetWindowRect(g_w.hwnd, &g_w.rect);
        g_w.restyled = true;
    }
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
}

void Activate(bool active) {
    if (g_w.display == kDisplayWindow || !g_w.hwnd) return;
    if (!active && !g_w.suspended) {
        GL_LOG("focus lost: %s", g_w.switched ? "desktop mode restored, minimized" : "minimized");
        g_w.suspended = true;
        RestoreMode();
        PlaceWindow(HWND_NOTOPMOST, RECT{}, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        ShowWindowAsync(g_w.hwnd, SW_MINIMIZE);
    } else if (active && g_w.suspended) {
        GL_LOG("focus back");
        g_w.suspended = false;
        if (IsIconic(g_w.hwnd)) ShowWindow(g_w.hwnd, SW_RESTORE);
        if (g_w.display == kDisplaySwitch) g_w.switched = SwitchMode();
        Cover();
        RenderRefresh();
    }
}

LRESULT CallOrig(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (!g_w.orig) return g_w.unicode ? DefWindowProcW(h, msg, wp, lp) : DefWindowProcA(h, msg, wp, lp);
    return g_w.unicode ? CallWindowProcW(g_w.orig, h, msg, wp, lp) : CallWindowProcA(g_w.orig, h, msg, wp, lp);
}

LONG_PTR GetProc(HWND h, bool unicode) {
    return unicode ? GetWindowLongPtrW(h, GWLP_WNDPROC) : GetWindowLongPtrA(h, GWLP_WNDPROC);
}
LONG_PTR SetProc(HWND h, bool unicode, LONG_PTR proc) {
    return unicode ? SetWindowLongPtrW(h, GWLP_WNDPROC, proc) : SetWindowLongPtrA(h, GWLP_WNDPROC, proc);
}

LRESULT CALLBACK HookProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (h != g_w.hwnd) return CallOrig(h, msg, wp, lp);
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
        if (TryEnterCriticalSection(&g_lock)) {
            Activate(wp != 0);
            LeaveCriticalSection(&g_lock);
        }
        break;
    case WM_PAINT:
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
    return CreateWindowExW(0, kClass, L"gl2d9", WS_POPUP, 0, 0, g_board.screenW, g_board.screenH, nullptr, nullptr, inst,
                           nullptr);
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

void Unhook() {
    if (!g_w.hooked || !g_w.hwnd || !IsWindow(g_w.hwnd)) return;
    // Something subclassed the window after gl2d9: it stays in the chain, passing messages on.
    if (GetProc(g_w.hwnd, g_w.unicode) == reinterpret_cast<LONG_PTR>(HookProc))
        SetProc(g_w.hwnd, g_w.unicode, reinterpret_cast<LONG_PTR>(g_w.orig));
    else
        return;
    g_w.hooked = false;
}

}  // namespace

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
        const wchar_t* device = GetMonitorInfoW(MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY), &mi) ? mi.szDevice : nullptr;
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

bool WindowOpen(HWND requested) {
    WindowClose();
    g_w = WindowState{};
    g_w.display = g_config.display;
    HWND h = requested && IsWindow(requested) ? requested : nullptr;
    if (!h) {
        h = GetActiveWindow();
        // Only a window of this process's (another one's can't be subclassed).
        DWORD pid = 0;
        if (h) GetWindowThreadProcessId(h, &pid);
        if (pid != GetCurrentProcessId()) h = nullptr;
    }
    if (!h) {
        h = MakeOwnWindow();
        if (!h) {
            GL_LOG("could not make a window (%lu)", GetLastError());
            return false;
        }
        g_w.own = true;
        GL_LOG("window: gl2d9's own %p", static_cast<void*>(h));
    } else {
        GL_LOG("window: %p%s", static_cast<void*>(h), requested ? "" : " (the active window)");
    }
    g_w.hwnd = h;
    g_board.hwnd = h;
    g_board.ownWindow = g_w.own;
    if (!g_w.own) Hook();
    MonitorRect(h, g_w.device);
    if (g_w.display == kDisplaySwitch) {
        g_w.switched = SwitchMode();
        if (!g_w.switched) g_w.display = kDisplayBorderless;
    }
    if (g_w.display == kDisplayWindow) Windowed();
    else Cover();
    if (g_w.own) {
        ShowWindow(h, SW_SHOW);
        SetForegroundWindow(h);
    }
    return true;
}

void WindowClose() {
    RestoreMode();
    if (!g_w.hwnd) return;
    if (g_w.own) {
        DestroyWindow(g_w.hwnd);
    } else {
        Unhook();
        if (g_w.restyled && IsWindow(g_w.hwnd)) {
            SetWindowLongW(g_w.hwnd, GWL_STYLE, g_w.style);
            SetWindowLongW(g_w.hwnd, GWL_EXSTYLE, g_w.exStyle);
            PlaceWindow((g_w.exStyle & WS_EX_TOPMOST) ? HWND_TOPMOST : HWND_NOTOPMOST, g_w.rect,
                        SWP_FRAMECHANGED | SWP_NOACTIVATE);
        }
    }
    g_w.hwnd = nullptr;
    g_board.hwnd = nullptr;
    g_board.ownWindow = false;
}

// The cursor hidden over the picture each frame too: a game that captures the mouse (Unreal)
// gets no WM_SETCURSOR, and keeps the cursor its window class has. (The cursor is the
// calling thread's: games draw from the thread of their window.)
void HideCursor() {
    HWND hwnd = g_w.hwnd;
    if (!hwnd || g_w.suspended || !GetCursor()) return;
    POINT p;
    RECT rc;
    if (GetCapture() != hwnd) {
        if (!GetCursorPos(&p) || WindowFromPoint(p) != hwnd || !ScreenToClient(hwnd, &p) || !GetClientRect(hwnd, &rc) ||
            !PtInRect(&rc, p))
            return;
    }
    SetCursor(nullptr);
}

void WindowPump() {
    HideCursor();
    if (!g_w.own || !g_w.hwnd) return;
    MSG m;
    while (PeekMessageW(&m, g_w.hwnd, 0, 0, PM_REMOVE)) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
}

}  // namespace gl2d9
