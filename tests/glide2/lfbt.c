#include <glide.h>
#include <windows.h>
#include <stdio.h>
int main(int argc, char** argv) {
  GrHwConfiguration hw; GrLfbInfo_t info; int f, x, y; int mode = argc > 1 ? atoi(argv[1]) : 0;
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ABGR, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  for (f = 0; f < 100000; f++) {
    grBufferClear(0xFF0000, 0, 0);
    info.size = sizeof(info);
    if (mode == 0 && grLfbLock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER, GR_LFBWRITEMODE_565, GR_ORIGIN_UPPER_LEFT, FXFALSE, &info)) {
      FxU16* p = info.lfbPtr;
      for (y = 100; y < 200; y++) for (x = 100; x < 300; x++) p[y * (info.strideInBytes / 2) + x] = 0xF800;
      grLfbUnlock(GR_LFB_WRITE_ONLY, GR_BUFFER_BACKBUFFER);
    }
    if (mode == 1) { static FxU16 buf[100*200]; for (x = 0; x < 20000; x++) buf[x] = 0x07E0; grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 100, 100, GR_LFB_SRC_FMT_565, 200, 100, 400, buf); }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
