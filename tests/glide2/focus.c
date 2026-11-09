#include <windows.h>
#include <glide.h>
#include <stdio.h>
#include <string.h>
/* A game window of its own, drawn to from the window's thread or (argument "thread", as
   Unreal does) from another, the window's thread pumping messages meanwhile. For focus
   changes (Alt+Tab, minimize) done to it from outside: the picture must come back each time.
   Prints each second the frames drawn and whether the window is minimized. With "paintfg"
   too, its WM_PAINT takes the foreground back, as NFS 3's does while fullscreen: switched away,
   it must stay away. With "reopen", Glide is closed and opened again every 200 frames (from
   the drawing thread: NFS 3 between menus and races), twice in a row each time (the second
   close before the window's thread has put it back after the first), five times, then
   closed: it prints the window's style and procedure then, which must be the program's own
   again. With "awayclose", Glide is closed when the focus goes and opened again when it comes
   back (from the drawing thread, as Unreal does: WM_ACTIVATEAPP sets a flag it reads). With
   "awaymin", the window minimizes itself (SW_SHOWMINNOACTIVE) when the focus goes, as
   Unreal's does in fullscreen. With "awaywait" too, Glide is opened again only once the window
   is no longer minimized (Unreal: back with the focus, it waits for its window to be restored;
   under Wine with a window manager that doesn't restore it, nothing else does). */
static HWND g_wnd;
static volatile LONG g_stop;
static int g_paintfg, g_reopen, g_awayclose, g_awaymin, g_awaywait;
static volatile LONG g_active = 1;
static LONG g_style0;
static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
static LRESULT CALLBACK Proc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
  if (msg == WM_ACTIVATEAPP) {
    InterlockedExchange(&g_active, wp != 0);
    if (!wp && g_awaymin) ShowWindow(h, SW_SHOWMINNOACTIVE);
  }
  if (msg == WM_PAINT && g_paintfg) {
    PAINTSTRUCT ps;
    SetForegroundWindow(h);
    BeginPaint(h, &ps); EndPaint(h, &ps);
    return 0;
  }
  return DefWindowProcA(h, msg, wp, lp);
}
static DWORD WINAPI Draw(LPVOID unused) {
  GrVertex v[3]; int f, i;
  (void)unused;
  grGlideInit(); grSstSelect(0);
  if (!grSstWinOpen((FxU32)g_wnd, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
    printf("grSstWinOpen failed\n"); fflush(stdout); return 1;
  }
  DWORD t0 = GetTickCount(), frames = 0;
  int opens = 1, open = 1;
  for (f = 0; f < 1000000 && !g_stop; f++) {
    if (g_awayclose && open != (int)g_active) {
      if (open) {
        grSstWinClose(); open = 0;
        printf("closed: focus away\n"); fflush(stdout);
      } else if (!(g_awaywait && IsIconic(g_wnd)) &&
                 grSstWinOpen((FxU32)g_wnd, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        open = 1;
        printf("opened: focus back\n"); fflush(stdout);
      }
    }
    if (!open) {
      if (GetCurrentThreadId() == GetWindowThreadProcessId(g_wnd, NULL)) {
        MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
      }
      Sleep(10);
      continue;
    }
    if (g_reopen && f && f % 200 == 0) {
      grSstWinClose();
      if (opens == 6) {
        Sleep(500);  /* (the window's thread putting it back) */
        printf("closed: style %08lx (was %08lx), procedure %s\n", (unsigned long)GetWindowLongA(g_wnd, GWL_STYLE),
               (unsigned long)g_style0, GetWindowLongPtrA(g_wnd, GWLP_WNDPROC) == (LONG_PTR)Proc ? "the program's" : "another");
        fflush(stdout);
        break;
      }
      if (!grSstWinOpen((FxU32)g_wnd, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        printf("reopen failed\n"); fflush(stdout); return 1;
      }
      grSstWinClose();
      if (!grSstWinOpen((FxU32)g_wnd, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
        printf("reopen failed\n"); fflush(stdout); return 1;
      }
      printf("reopened %d\n", opens++); fflush(stdout);
    }
    for (i = 0; i < 3; i++) {
      v[i].x = 100.0f + i * 200; v[i].y = i == 1 ? 50.0f : 400.0f; v[i].oow = 1;
      v[i].r = 255; v[i].g = (float)(f & 255); v[i].b = 0; v[i].a = 255;
    }
    guColorCombineFunction(GR_COLORCOMBINE_ITRGB);
    grBufferClear(0x204020, 0, 0); grDrawTriangle(&v[0], &v[1], &v[2]); grBufferSwap(1);
    frames++;
    if (GetTickCount() - t0 >= 1000) {
      printf("frames %lu iconic %d\n", frames, IsIconic(g_wnd)); fflush(stdout); t0 = GetTickCount(); frames = 0;
    }
    if (GetCurrentThreadId() == GetWindowThreadProcessId(g_wnd, NULL)) {
      MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
    }
  }
  grGlideShutdown();
  return 0;
}
int main(int argc, char** argv) {
  int i, thread = 0;
  WNDCLASSA wc = {0}; wc.lpfnWndProc = Proc; wc.hInstance = GetModuleHandle(0); wc.lpszClassName = "game";
  RegisterClassA(&wc);
  g_wnd = CreateWindowA("game", "focus test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 640, 480, 0, 0, wc.hInstance, 0);
  g_style0 = GetWindowLongA(g_wnd, GWL_STYLE);
  for (i = 1; i < argc; i++) {
    if (!strcmp(argv[i], "thread")) thread = 1;
    if (!strcmp(argv[i], "paintfg")) g_paintfg = 1;
    if (!strcmp(argv[i], "reopen")) g_reopen = 1;
    if (!strcmp(argv[i], "awayclose")) g_awayclose = 1;
    if (!strcmp(argv[i], "awaymin")) g_awaymin = 1;
    if (!strcmp(argv[i], "awaywait")) g_awaywait = 1;
  }
  SetForegroundWindow(g_wnd);
  if (thread) {
    MSG m; HANDLE t = CreateThread(0, 0, Draw, 0, 0, 0);
    while (MsgWaitForMultipleObjects(1, &t, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
      while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
  } else {
    Draw(0);
  }
  return 0;
}
