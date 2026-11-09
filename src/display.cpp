// Monitors and display modes for the window takeover (see window.cpp): the game's monitor, the
// mode switched to and back (display=switch), where the picture goes in the window, and the
// resolutions offered to games.
#include <algorithm>
#include <cmath>

#include "core.h"
#include "windowint.h"

namespace gl2d9 {
namespace win {

volatile LONG g_switched;

namespace {
wchar_t g_switchedDevice[CCHDEVICENAME];  // the monitor g_switched is about
}  // namespace

// The monitor the screen is shown on: the primary one (as a Voodoo game's own window is
// usually on it), or the window's own in window mode.
HMONITOR GameMonitor(HWND h) {
    if (g_w.display == kDisplayWindow && h) return MonitorFromWindow(h, MONITOR_DEFAULTTONEAREST);
    const POINT origin = {0, 0};
    return MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
}

RECT MonitorRect(HWND h, wchar_t* device) {
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (!GetMonitorInfoW(GameMonitor(h), &mi)) {
        mi.rcMonitor = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        mi.szDevice[0] = 0;
    }
    if (device) lstrcpynW(device, mi.szDevice, CCHDEVICENAME);
    return mi.rcMonitor;
}

bool SwitchMode() {
    DEVMODEW dm = {};
    dm.dmSize = sizeof(dm);
    dm.dmPelsWidth = DWORD(g_board.screenW);
    dm.dmPelsHeight = DWORD(g_board.screenH);
    dm.dmBitsPerPel = 32;
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT | DM_BITSPERPEL;
    // The refresh rate grSstWinOpen asked for, where the monitor has it.
    if (const int hz = RefreshHz(g_board.refresh)) {
        dm.dmDisplayFrequency = DWORD(hz);
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
    lstrcpynW(g_switchedDevice, g_w.device, CCHDEVICENAME);
    InterlockedExchange(&g_switched, 1);
    GL_LOG("display: switched to %dx%d", g_board.screenW, g_board.screenH);
    return true;
}

void RestoreMode() {
    if (!InterlockedExchange(&g_switched, 0)) return;
    ChangeDisplaySettingsExW(g_switchedDevice, nullptr, nullptr, 0, nullptr);
    GL_LOG("display: desktop mode restored");
}

}  // namespace win

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
    // Only where a mode the monitor doesn't have fails: display=switch. Scaled (borderless,
    // window), every mode works, and trimming the list would only hide choices.
    if (!g_config.filterResolutions || g_config.display != kDisplaySwitch) return;
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
    bool code = false;  // in a section that holds code too
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && !found; i++, sec++) {
        if (!(sec->Characteristics & IMAGE_SCN_MEM_READ)) continue;
        FxU8* p = base + sec->VirtualAddress;
        const size_t n = std::min<size_t>(sec->Misc.VirtualSize, sec->SizeOfRawData);
        for (size_t at = 0; at + sizeof(kList) <= n; at += 2) {
            if (memcmp(p + at, kList, sizeof(kList)) == 0) {
                found = reinterpret_cast<wchar_t*>(p + at);
                code = (sec->Characteristics & IMAGE_SCN_MEM_EXECUTE) != 0;
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
    // (Executable still where the section holds code: a thread running code on that page
    // meanwhile faulted without.)
    DWORD old;
    if (!VirtualProtect(found, sizeof(kList), code ? PAGE_EXECUTE_READWRITE : PAGE_READWRITE, &old)) return;
    memset(found, 0, sizeof(kList));
    memcpy(found, out.c_str(), std::min(out.size(), len) * sizeof(wchar_t));
    VirtualProtect(found, sizeof(kList), old, &old);
    char narrow[sizeof(kList)];
    WideCharToMultiByte(CP_UTF8, 0, out.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    GL_LOG("resolutions: GlideDrv.dll's list made the monitor's: %s", narrow);
}

}  // namespace gl2d9
