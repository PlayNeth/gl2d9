#include <glide.h>
#include <stdio.h>
/* White antialiased triangle (left half) next to a plain one (right half), AA lines and
   points under them, alpha blended, on black. */
static void vtx(GrVertex* v, float x, float y) { v->x = x; v->y = y; v->r = v->g = v->b = v->a = 255; v->oow = 1; }
int main(void) {
  GrHwConfiguration hw; GrVertex a, b, c; int f, i;
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  guColorCombineFunction(GR_COLORCOMBINE_ITRGB);
  guAlphaSource(GR_ALPHASOURCE_ITERATED_ALPHA);
  grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
  for (f = 0; f < 100000; f++) {
    grBufferClear(0, 0, 0);
    vtx(&a, 40, 40); vtx(&b, 300, 70); vtx(&c, 90, 230);
    grAADrawTriangle(&a, &b, &c, FXTRUE, FXTRUE, FXTRUE);
    vtx(&a, 340, 40); vtx(&b, 600, 70); vtx(&c, 390, 230);
    grDrawTriangle(&a, &b, &c);
    for (i = 0; i < 8; i++) { vtx(&a, 40 + i * 30, 260); vtx(&b, 60 + i * 35, 440); grAADrawLine(&a, &b); }
    for (i = 0; i < 8; i++) { vtx(&a, 360 + i * 30.3f, 300 + i * 10.6f); grAADrawPoint(&a); }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
