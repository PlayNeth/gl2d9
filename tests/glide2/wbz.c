#include <glide.h>
#include <stdio.h>
/* W buffer: depth read back against MAME's formula; coplanar decals at a near and a far W.
   Left column: decal with bias -16 over a LESS test (shows: green). Right: bias +16 (hidden: gray). */
static GrVertex v[4];
static void quad(float x0, float y0, float x1, float y1, float oow) {
  int i; for (i = 0; i < 4; i++) { v[i].x = (i & 1) ? x1 : x0; v[i].y = (i & 2) ? y1 : y0; v[i].ooz = 0; v[i].oow = oow; }
  grDrawTriangle(&v[0], &v[1], &v[3]); grDrawTriangle(&v[0], &v[3], &v[2]);
}
static unsigned mame(double oow) {
  unsigned long long it = (unsigned long long)(oow * 4294967296.0); unsigned t, w; int e;
  if (it & 0xffff00000000ull) return 0;
  t = (unsigned)it; if (!(t & 0xffff0000)) return 0xffff;
  for (e = 0; !(t & (0x80000000u >> e)); e++) {}
  w = (e << 12) | ((~t >> (19 - e)) & 0xfff); if (w < 0xffff) w++; return w;
}
int main(void) {
  GrHwConfiguration hw; FxU16 zr[1]; int f, k; float ws[3] = {1.0f / 5, 1.0f / 3000, 1.0f / 50000};
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  grDepthBufferMode(GR_DEPTHBUFFER_WBUFFER); grDepthMask(FXTRUE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  for (f = 0; f < 100000; f++) {
    grBufferClear(0x000040, 0, 0xFFFF);
    for (k = 0; k < 3; k++) {
      float y = 20 + k * 150;
      grDepthBufferFunction(GR_CMP_LESS); grDepthBiasLevel(0);
      grConstantColorValue(0xFF808080); quad(20, y, 300, y + 130, ws[k]); quad(340, y, 620, y + 130, ws[k]);
      if (f == 3) {
        grLfbReadRegion(GR_BUFFER_AUXBUFFER, 100, (int)y + 50, 1, 1, 2, zr);
        printf("w=%g: read %04x, MAME %04x\n", 1 / ws[k], zr[0], mame(ws[k])); fflush(stdout);
      }
      grConstantColorValue(0xFF00FF00); grDepthBiasLevel(-16); quad(60, y + 20, 260, y + 110, ws[k]);
      grConstantColorValue(0xFFFF0000); grDepthBiasLevel(16); quad(380, y + 20, 580, y + 110, ws[k]);
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
