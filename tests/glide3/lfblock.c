#include <windows.h>
#include <glide.h>
#include <stdio.h>
/* Frame buffer locks held while other things happen (fixed in 1.0.2):
   1. A write lock in 8888 with a read lock taken meanwhile: the read lock's 16-bit fill must
      not write over the 32-bit pixels the game is writing. A blue square written in 8888,
      read lock taken and given back, unlock: the square is blue (read back: 001f).
   2. A pixel written through a write lock, then grLfbReadRegion (which shows the write lock's
      changes first), then a green triangle drawn over it, then the unlock: the pixel is green
      (07e0), not written again red over the triangle.
   Right: prints "8888 + read: 001f" and "commit then draw: 07e0"; a blue square at the left,
   a green triangle. */
static void Tri(float x0, float y0, float x1, float y1, float x2, float y2) {
  float v[3][2] = {{x0, y0}, {x1, y1}, {x2, y2}};
  grDrawTriangle(v[0], v[1], v[2]);
}
int main(void) {
  GrLfbInfo_t info, rinfo; GrContext_t ctx; int x, y, f; FxU16 a = 0, b = 0;
  grGlideInit(); grSstSelect(0);
  ctx = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  if (!ctx) { printf("no window\n"); return 1; }
  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT,
                 GR_COMBINE_OTHER_NONE, FXFALSE);
  grConstantColorValue(0xFF00FF00);
  for (f = 0; f < 100000; f++) {
    grBufferClear(0, 0, 0xFFFF);
    /* 1 */
    info.size = sizeof(info);
    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_8888, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info)) {
      for (y = 100; y < 200; y++) {
        FxU32* row = (FxU32*)((char*)info.lfbPtr + y * info.strideInBytes);
        for (x = 50; x < 150; x++) row[x] = 0xFF0000FF;
      }
      rinfo.size = sizeof(rinfo);
      if (grLfbLock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER, GR_LFBWRITEMODE_ANY, GR_ORIGIN_UPPER_LEFT, FXFALSE, &rinfo))
        grLfbUnlock(GR_LFB_READ_ONLY, GR_BUFFER_FRONTBUFFER);
      grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
    }
    if (f == 3) grLfbReadRegion(GR_BUFFER_BACKBUFFER, 100, 150, 1, 1, 2, &a);
    /* 2 */
    info.size = sizeof(info);
    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info)) {
      FxU16 dummy;
      for (y = 300; y < 340; y++) {
        FxU16* row = (FxU16*)((char*)info.lfbPtr + y * info.strideInBytes);
        for (x = 300; x < 340; x++) row[x] = 0xF800;
      }
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, 1, 1, 2, &dummy);
      Tri(250, 250, 450, 250, 250, 450);
      grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
    }
    if (f == 3) {
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 310, 310, 1, 1, 2, &b);
      printf("8888 + read: %04x (want 001f)\ncommit then draw: %04x (want 07e0)\n", a, b); fflush(stdout);
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
