#include <windows.h>
#include <glide.h>
#include <stdio.h>
/* Frame buffer writes wider than 1024 pixels (NFS Porsche Unleashed's loading screens at
   1600x1200): each row a horizontal ramp (red rising with x), written at the stride the lock
   gives, read back at x = 100, 900 and 1500 of a middle row. Right: three different reds,
   rising; the picture a smooth ramp over the whole width. */
int main(void) {
  GrLfbInfo_t info; GrContext_t ctx; int x, y, f; FxU16 px[3];
  grGlideInit(); grSstSelect(0);
  ctx = grSstWinOpen(0, GR_RESOLUTION_1600x1200, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  if (!ctx) { printf("no 1600x1200\n"); return 1; }
  for (f = 0; f < 100000; f++) {
    grBufferClear(0, 0, 0xFFFF);
    info.size = sizeof(info);
    if (grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info)) {
      for (y = 300; y < 900; y++) {
        FxU16* row = (FxU16*)((char*)info.lfbPtr + y * info.strideInBytes);
        for (x = 0; x < 1600; x++) row[x] = (FxU16)((x * 31 / 1599) << 11);
      }
      grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
    }
    if (f == 3) {
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 100, 600, 1, 1, 2, &px[0]);
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 900, 600, 1, 1, 2, &px[1]);
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 1500, 600, 1, 1, 2, &px[2]);
      printf("stride %lu; red at 100, 900, 1500: %d %d %d (want 1 17 29)\n", (unsigned long)info.strideInBytes,
             px[0] >> 11, px[1] >> 11, px[2] >> 11); fflush(stdout);
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
