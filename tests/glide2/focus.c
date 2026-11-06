#include <windows.h>
#include <glide.h>
#include <stdio.h>
#include <string.h>
/* A game window of its own, drawn to from the window's thread or (argument "thread", as
   Unreal does) from another, the window's thread pumping messages meanwhile. For focus
   changes (Alt+Tab, minimize) done to it from outside: the picture must come back each time.
   Prints each second the frames drawn and whether the window is minimized. */
static HWND g_wnd;
static volatile LONG g_stop;
static DWORD WINAPI Draw(LPVOID unused) {
  GrVertex v[3]; int f, i;
  (void)unused;
  grGlideInit(); grSstSelect(0);
  if (!grSstWinOpen((FxU32)g_wnd, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) {
    printf("grSstWinOpen failed\n"); fflush(stdout); return 1;
  }
  DWORD t0 = GetTickCount(), frames = 0;
  for (f = 0; f < 1000000 && !g_stop; f++) {
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
  WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandle(0); wc.lpszClassName = "game";
  RegisterClassA(&wc);
  g_wnd = CreateWindowA("game", "focus test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 50, 50, 640, 480, 0, 0, wc.hInstance, 0);
  SetForegroundWindow(g_wnd);
  if (argc > 1 && !strcmp(argv[1], "thread")) {
    MSG m; HANDLE t = CreateThread(0, 0, Draw, 0, 0, 0);
    while (MsgWaitForMultipleObjects(1, &t, FALSE, INFINITE, QS_ALLINPUT) != WAIT_OBJECT_0)
      while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
  } else {
    Draw(0);
  }
  return 0;
}
