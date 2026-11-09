#include <glide.h>
#include <stdio.h>
/* Destination alpha without an alpha buffer, and compare-to-bias with depth writes. Left: red
   drawn with (DST_ALPHA, ZERO) over gray, no alpha buffer: the Voodoo blends with a destination
   alpha of 1, so red (black is wrong). Right: a blue quad drawn in Z compare-to-bias mode (bias
   0x4000, the quad's Z 0x8000) writes its own Z, then a green quad at Z 0x6000 in plain Z mode
   (LESS) over its middle passes; the depth outside it reads 8000 (4000 is wrong). */
static GrVertex v[4];
static void quad(float x0, float y0, float x1, float y1, float z) {
  int i; for (i = 0; i < 4; i++) { v[i].x = (i & 1) ? x1 : x0; v[i].y = (i & 2) ? y1 : y0; v[i].oow = 1.0f; v[i].ooz = z; }
  grDrawTriangle(&v[0], &v[1], &v[3]); grDrawTriangle(&v[0], &v[3], &v[2]);
}
int main(void) {
  GrHwConfiguration hw; int f; FxU16 px[1], z[1];
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  for (f = 0; f < 100000; f++) {
    grDepthBufferMode(GR_DEPTHBUFFER_DISABLE); grDepthMask(FXTRUE);
    grBufferClear(0x808080, 0, 0xFFFF);
    grAlphaBlendFunction(GR_BLEND_DST_ALPHA, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grConstantColorValue(0xFFFF0000); quad(50, 100, 280, 300, 0);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS); grDepthBufferFunction(GR_CMP_LESS); grDepthBiasLevel(0x4000);
    grConstantColorValue(0xFF0000FF); quad(360, 100, 590, 300, 0x8000);
    grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER); grDepthBiasLevel(0);
    grConstantColorValue(0xFF00FF00); quad(420, 150, 530, 250, 0x6000);
    if (f == 3) {
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 150, 200, 1, 1, 2, px); printf("dst alpha: %04x (want f800) ", px[0]);
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 470, 200, 1, 1, 2, px); printf("green: %04x (want 07e0) ", px[0]);
      grLfbReadRegion(GR_BUFFER_AUXBUFFER, 380, 120, 1, 1, 2, z); printf("depth: %04x (want 8000)\n", z[0]); fflush(stdout);
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
