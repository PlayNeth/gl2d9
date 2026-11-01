#include <glide.h>
#include <stdio.h>
/* 1. A quad at Z 0x8000 drawn, its depth read back (grLfbReadRegion of the aux buffer).
   2. The depth buffer written near (0x1000) through a ZA16 lock over the left half of a
      second region; a far quad (0x9000) drawn over the region with LESS: only the right
      half shows (green).
   3. A pipeline write of 50%-alpha red over blue with alpha blending: purple. */
static GrVertex v[4];
static void quad(float x0, float y0, float x1, float y1, float ooz) {
  int i; for (i = 0; i < 4; i++) { v[i].x = (i & 1) ? x1 : x0; v[i].y = (i & 2) ? y1 : y0; v[i].ooz = ooz; v[i].oow = 1; }
  grDrawTriangle(&v[0], &v[1], &v[3]); grDrawTriangle(&v[0], &v[3], &v[2]);
}
int main(void) {
  GrHwConfiguration hw; GrLfbInfo_t info; FxU16 zr[4]; int x, y, f;
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  grDepthBufferMode(GR_DEPTHBUFFER_ZBUFFER); grDepthMask(FXTRUE); grDepthBufferFunction(GR_CMP_ALWAYS);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  for (f = 0; f < 100000; f++) {
    grDepthBufferFunction(GR_CMP_ALWAYS);
    grBufferClear(0x404040, 0, 0xFFFF);
    grConstantColorValue(0xFFFFFF00); quad(20, 20, 200, 200, 0x8000);
    if (f == 3) {
      grLfbReadRegion(GR_BUFFER_AUXBUFFER, 100, 100, 4, 1, 8, zr);
      printf("depth read: %04x %04x (want 8000); outside: ", zr[0], zr[3]);
      grLfbReadRegion(GR_BUFFER_AUXBUFFER, 300, 300, 1, 1, 2, zr); printf("%04x (want ffff)\n", zr[0]); fflush(stdout);
    }
    /* 2 */
    info.size = sizeof(info);
    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_AUXBUFFER, GR_LFBWRITEMODE_ZA16, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info)) {
      for (y = 20; y < 200; y++) for (x = 240; x < 330; x++) ((FxU16*)info.lfbPtr)[y * (info.strideInBytes / 2) + x] = 0x1000;
      grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_AUXBUFFER);
    }
    grDepthBufferFunction(GR_CMP_LESS);
    grConstantColorValue(0xFF00FF00); quad(240, 20, 420, 200, 0x9000);
    /* 3 */
    grDepthBufferFunction(GR_CMP_ALWAYS);
    grConstantColorValue(0xFF0000FF); quad(20, 260, 200, 440, 0x1000);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ONE, GR_BLEND_ZERO);
    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT, FXTRUE, &info)) {
      for (y = 260; y < 440; y++) for (x = 20; x < 200; x++) ((FxU32*)info.lfbPtr)[y * (info.strideInBytes / 4) + x] = 0x80FF0000;
      grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
    }
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
