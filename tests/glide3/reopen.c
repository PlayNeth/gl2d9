#include <windows.h>
#include <glide.h>
#include <stdio.h>
/* NFS High Stakes' path: its own window; 640x480, frames, close, shutdown, init, 1600x1200,
   with another subclass put on the window in between (dd2d9 does it for the game's menus).
   Right: "open 1600" and "done" printed (it crashed at the second grSstWinOpen, stack overflow). */
static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }
/* Another subclasser after gl2d9 (dd2d9, its ddraw.dll, does it for the game's menus). */
static WNDPROC prev;
static LRESULT CALLBACK other(HWND h, UINT m, WPARAM w, LPARAM l) { return CallWindowProcA(prev, h, m, w, l); }
static void frames(int n, float w, float h) {
  typedef struct { float x, y; } V; V a = {10, 10}, b = {w - 10, 10}, c = {w / 2, h - 10}; int i;
  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  for (i = 0; i < n; i++) { grBufferClear(0x202020, 0, 0xFFFF); grConstantColorValue(0xFF00FF00); grDrawTriangle(&a, &b, &c); grBufferSwap(1); }
}
int main(void) {
  WNDCLASSA wc = {0}; HWND hw; GrContext_t ctx;
  wc.lpfnWndProc = proc; wc.hInstance = GetModuleHandleA(0); wc.lpszClassName = "nfshs"; RegisterClassA(&wc);
  hw = CreateWindowA("nfshs", "NFS", WS_POPUP | WS_VISIBLE, 0, 0, 640, 480, 0, 0, wc.hInstance, 0);
  grGlideInit(); grSstSelect(0);
  ctx = grSstWinOpen((FxU32)hw, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  printf("open 640: %p\n", (void*)ctx); fflush(stdout);
  frames(60, 640, 480);
  prev = (WNDPROC)SetWindowLongPtrA(hw, GWLP_WNDPROC, (LONG_PTR)other);
  grSstWinClose(ctx); grGlideShutdown(); grGlideInit(); grSstSelect(0);
  ctx = grSstWinOpen((FxU32)hw, GR_RESOLUTION_1600x1200, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  printf("open 1600: %p\n", (void*)ctx); fflush(stdout);
  frames(300, 1600, 1200);
  printf("done\n"); fflush(stdout);
  grSstWinClose(ctx); grGlideShutdown();
  return 0;
}
