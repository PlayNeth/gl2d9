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

// The window over the whole monitor, frameless, in front.
void Cover() {
    const RECT m = MonitorRect(g_w.hwnd, g_w.device);
    if (!g_w.restyled && !g_w.own) {
        g_w.style = GetWindowLongW(g_w.hwnd, GWL_STYLE);
        g_w.exStyle = GetWindowLongW(g_w.hwnd, GWL_EXSTYLE);
        GetWindowRect(g_w.hwnd, &g_w.rect);
        g_w.restyled = true;
    }
    SetWindowLongW(g_w.hwnd, GWL_STYLE,
                   (GetWindowLongW(g_w.hwnd, GWL_STYLE) & ~(WS_OVERLAPPEDWINDOW | WS_DLGFRAME | WS_BORDER)) | WS_POPUP);
    SetWindowLongW(
        g_w.hwnd, GWL_EXSTYLE,
        GetWindowLongW(g_w.hwnd, GWL_EXSTYLE) & ~(WS_EX_CLIENTEDGE | WS_EX_WINDOWEDGE | WS_EX_DLGMODALFRAME));
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

// Windows gl2d9 couldn't unhook (something subclassed them after it, dd2d9 for a game's
// DirectDraw menus): HookProc stays in their chains, passing their messages to the procedure
// it replaced. Opened again on one, gl2d9 takes that hook back rather than subclassing again:
// a second HookProc, below the other subclass, would call it, which calls the first HookProc,
// and so on (NFS High Stakes, 640x480 menus then a 1600x1200 race, with dd2d9: the stack
// overflowed at once, nothing logged).
struct StaleHook {
    HWND hwnd;
    WNDPROC orig;
    bool unicode;
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
    const LRESULT r = HookProcBody(h, msg, wp, lp);
    depth--;
    return r;
}

LRESULT HookProcBody(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    if (h != g_w.hwnd || !g_w.hooked) {
        // A window gl2d9 is no longer open on, still in its chain: its own procedure.
        if (StaleHook* e = FindStale(h)) {
            const WNDPROC orig = e->orig;
            const bool unicode = e->unicode;
            if (msg == WM_NCDESTROY) *e = StaleHook{};
            return CallProc(orig, unicode, h, msg, wp, lp);
        }
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
    if (StaleHook* e = FindStale(g_w.hwnd)) {  // still in its chain from before: taken back
        g_w.unicode = e->unicode;
        g_w.orig = e->orig;
        g_w.hooked = true;
        *e = StaleHook{};
        GL_LOG("window %p: still hooked from before (something subclassed it since)", static_cast<void*>(g_w.hwnd));
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

void Unhook() {
    if (!g_w.hooked || !g_w.hwnd || !IsWindow(g_w.hwnd)) return;
    // Something subclassed the window after gl2d9: it stays in the chain, passing messages on,
    // and is remembered (StaleHook) for gl2d9 opening on it again.
    if (GetProc(g_w.hwnd, g_w.unicode) == reinterpret_cast<LONG_PTR>(HookProc)) {
        SetProc(g_w.hwnd, g_w.unicode, reinterpret_cast<LONG_PTR>(g_w.orig));
    } else {
        StaleHook* e = FindStale(g_w.hwnd);
        for (StaleHook& s : g_stale)
            if (!e && (!s.hwnd || !IsWindow(s.hwnd))) e = &s;
        if (e) *e = StaleHook{g_w.hwnd, g_w.orig, g_w.unicode};
        else GL_LOG("window %p: left hooked, no room to remember it", static_cast<void*>(g_w.hwnd));
    }
    g_w.hooked = false;
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
