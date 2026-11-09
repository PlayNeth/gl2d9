// The window takeover's internals, shared by window.cpp (the policy: covering the monitor or
// making a window, focus changes, opening and closing), hook.cpp (gl2d9's procedure in the
// game window's chain) and display.cpp (monitors and display modes).
#pragma once
#include "voodoo.h"
#include "window.h"

namespace gl2d9 {
namespace win {

struct WindowState {
    HWND hwnd = nullptr;
    bool own = false;  // made by gl2d9
    bool unicode = false;
    WNDPROC orig = nullptr;  // the game's procedure (subclassed)
    bool hooked = false;
    LONG style = 0, exStyle = 0;  // as the game had them
    RECT rect = {};
    bool restyled = false;             // style, exStyle and rect kept (KeepGameStyle); with ourStyle, under g_hookLock
    LONG ourStyle = 0;                 // the style gl2d9 gave the window (Cover, Windowed)
    RECT ourRect = {};                 // and where it put it
    int display = kDisplayBorderless;  // as opened (switch falls back to borderless)
    wchar_t device[CCHDEVICENAME] = {};
    bool suspended = false;  // focus away: minimized, or opened with another program in front
    bool quiet = false;      // gl2d9 moving the window itself, on the window's own thread
};
extern WindowState g_w;

// For which window gl2d9 is open on and its procedure (g_w.hwnd, hooked, orig, unicode), the
// StaleHooks and the kept style (g_w.restyled, style, exStyle, rect, ourStyle), which the
// window's thread and a game's drawing thread both change. Held briefly, never while sending a
// window a message; taken after gl2d9's lock where both are.
struct HookLock {
    CRITICAL_SECTION cs;
    HookLock() { InitializeCriticalSection(&cs); }
};
extern HookLock g_hookLock;
struct HookGuard {
    HookGuard() { EnterCriticalSection(&g_hookLock.cs); }
    ~HookGuard() { LeaveCriticalSection(&g_hookLock.cs); }
    HookGuard(const HookGuard&) = delete;
    HookGuard& operator=(const HookGuard&) = delete;
};

// A kHideCursor posted and not handled yet (one at a time: asked each frame).
extern volatile LONG g_cursorAsked;

bool AnotherInFront();
bool OursInFront();

// Whether the calling thread is the window's own.
inline bool OnWindowThread(HWND h) { return GetWindowThreadProcessId(h, nullptr) == GetCurrentThreadId(); }

// gl2d9's own message to the window's thread (window.cpp), and what it asks for.
UINT ShowMsg();
enum : WPARAM { kShow, kMinimize, kRestore, kHideCursor, kActivate };  // ShowMsg's wParam

// (window.cpp)
void RefreshIfFree();
void Activate(bool active);
void ShowNow();

// (display.cpp) The display mode is the screen's (SwitchMode), on that monitor. The process's, not the open
// window's: the window's thread may switch it (Activate) as Glide closes on another thread,
// and the next WindowOpen starts a new WindowState.
extern volatile LONG g_switched;
HMONITOR GameMonitor(HWND h);
RECT MonitorRect(HWND h, wchar_t* device = nullptr);
bool SwitchMode();
void RestoreMode();

// (hook.cpp) Windows gl2d9 couldn't unhook (something subclassed them after it, dd2d9 for a game's
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
    bool waitFocus = false;  // Wine: put back once the focus is back (RestoreClosed), not while away
};
StaleHook* FindStale(HWND h);
LRESULT CallProc(WNDPROC orig, bool unicode, HWND h, UINT msg, WPARAM wp, LPARAM lp);
void Hook();
bool Unhook(const StaleHook* keep = nullptr);
void PutBack(HWND h, const StaleHook& r);
void RestoreClosed(HWND h);

}  // namespace win
}  // namespace gl2d9
