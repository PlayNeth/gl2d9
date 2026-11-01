#include <windows.h>
#include <glide.h>
#include <stdio.h>
typedef FxBool (__stdcall *SstOpenFn)(GrScreenResolution_t, GrScreenRefresh_t, GrColorFormat_t, GrOriginLocation_t, int, int);
/* A game window shown without activation, then Glide 2.1's grSstOpen (no window given). */
int main(void) {
  GrHwConfiguration hw; GrVertex v[3]; int f, i;
  WNDCLASSA wc = {0}; wc.lpfnWndProc = DefWindowProcA; wc.hInstance = GetModuleHandle(0); wc.lpszClassName = "game";
  RegisterClassA(&wc);
  HWND w = CreateWindowA("game", "game", WS_OVERLAPPEDWINDOW, 50, 50, 400, 300, 0, 0, wc.hInstance, 0);
  ShowWindow(w, SW_SHOWNOACTIVATE);
  printf("game window %p, active %p\n", (void*)w, (void*)GetActiveWindow()); fflush(stdout);
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  SstOpenFn sstOpen = (SstOpenFn)GetProcAddress(GetModuleHandleA("glide.dll"), "_grSstOpen@24");
  if (!sstOpen || !sstOpen(GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 0, 2)) return 1;
  for (i = 0; i < 3; i++) { v[i].x = 100 + i * 200; v[i].y = i == 1 ? 50 : 400; v[i].r = 255; v[i].g = 128; v[i].b = 0; v[i].a = 255; v[i].oow = 1; }
  guColorCombineFunction(GR_COLORCOMBINE_ITRGB);
  for (f = 0; f < 100000; f++) { MSG m; while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
    grBufferClear(0x202040, 0, 0); grDrawTriangle(&v[0], &v[1], &v[2]); grBufferSwap(1); }
  grGlideShutdown(); return 0;
}
