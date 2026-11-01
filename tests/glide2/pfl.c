#include <glide.h>
#include <stdio.h>
/* PREFOG_COLOR through a pipeline LFB write: gray background, red written with full fog to
   blue, blend (ONE, PREFOG_COLOR): blue + gray * red = (0.5, 0, 1) - magenta-ish purple.
   (The old approximation, dst * fogged: (0, 0, 1).) A triangle the same way beside it. */
static GrVertex v[4];
static void quad(float x0, float y0, float x1, float y1) {
  int i; for (i = 0; i < 4; i++) { v[i].x = (i & 1) ? x1 : x0; v[i].y = (i & 2) ? y1 : y0; v[i].oow = 1.0f / 100; v[i].ooz = 0; }
  grDrawTriangle(&v[0], &v[1], &v[3]); grDrawTriangle(&v[0], &v[3], &v[2]);
}
int main(void) {
  GrHwConfiguration hw; GrLfbInfo_t info; GrFog_t fog[GR_FOG_TABLE_SIZE]; int i, x, y, f; FxU32 px[1];
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  for (i = 0; i < GR_FOG_TABLE_SIZE; i++) fog[i] = 255;
  grFogTable(fog); grFogColorValue(0x0000FF);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  for (f = 0; f < 100000; f++) {
    grFogMode(GR_FOG_DISABLE); grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grBufferClear(0x808080, 0, 0xFFFF);
    grFogMode(GR_FOG_WITH_TABLE);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_PREFOG_COLOR, GR_BLEND_ONE, GR_BLEND_ZERO);
    info.size = sizeof(info);
    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT, FXTRUE, &info)) {
      for (y = 100; y < 300; y++) for (x = 50; x < 280; x++) ((FxU32*)info.lfbPtr)[y * (info.strideInBytes / 4) + x] = 0xFFFF0000;
      grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
    }
    grConstantColorValue(0xFFFF0000); quad(360, 100, 590, 300);
    if (f == 3) {
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 150, 200, 1, 1, 4, px); printf("lfb: %08x ", px[0]);
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 450, 200, 1, 1, 4, px); printf("tri: %08x (want ~ff8000ff / 7f/80)\n", px[0]); fflush(stdout);
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
