#include <glide.h>
#include <stdio.h>
#include <gump.h>
/* Top: guMP multiply of a red/white vertical-stripe texture and a green/white horizontal one
   (white*white = white, else red/green/black squares). Bottom: an RLE16 image (blue and
   yellow halves) written with grLfbWriteRegion, encoded with guEncodeRLE16. */
static FxU16 t0[16*16], t1[16*16], img[200*100];
static FxU32 rle[200*100];
int main(void) {
  GrHwConfiguration hw; GrMipMapId_t m0, m1; GrVertex v[4]; int i, x, y, f, bytes;
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) { t0[y*16+x] = (x & 4) ? 0xF800 : 0xFFFF; t1[y*16+x] = (y & 4) ? 0x07E0 : 0xFFFF; }
  m0 = guTexAllocateMemory(0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_RGB_565, GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16,
                           GR_ASPECT_1x1, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP, GR_TEXTUREFILTER_POINT_SAMPLED,
                           GR_TEXTUREFILTER_POINT_SAMPLED, 0, FXFALSE);
  m1 = guTexAllocateMemory(0, GR_MIPMAPLEVELMASK_BOTH, 16, 16, GR_TEXFMT_RGB_565, GR_MIPMAP_DISABLE, GR_LOD_16, GR_LOD_16,
                           GR_ASPECT_1x1, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP, GR_TEXTUREFILTER_POINT_SAMPLED,
                           GR_TEXTUREFILTER_POINT_SAMPLED, 0, FXFALSE);
  guTexDownloadMipMap(m0, t0, 0); guTexDownloadMipMap(m1, t1, 0);
  guMPInit(); guMPTexSource(0, m0); guMPTexSource(1, m1); guMPTexCombineFunction(GR_MPTEXTURECOMBINE_MULTIPLY);
  guColorCombineFunction(GR_COLORCOMBINE_DECAL_TEXTURE);
  for (i = 0; i < 200*100; i++) img[i] = (i % 200) < 100 ? 0x001F : 0xFFE0;
  bytes = guEncodeRLE16(rle, img, 200, 100);
  printf("rle bytes %d (count-only %d)\n", bytes, guEncodeRLE16(0, img, 200, 100)); fflush(stdout);
  for (i = 0; i < 4; i++) { v[i].x = (i & 1) ? 600 : 40; v[i].y = (i & 2) ? 220 : 20; v[i].oow = 1;
    v[i].tmuvtx[0].sow = (i & 1) ? 255 * 2 : 0; v[i].tmuvtx[0].tow = (i & 2) ? 255 * 2 : 0; }
  for (f = 0; f < 100000; f++) {
    grBufferClear(0x404040, 0, 0);
    guMPDrawTriangle(&v[0], &v[1], &v[3]); guMPDrawTriangle(&v[0], &v[3], &v[2]);
    grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 40, 260, GR_LFB_SRC_FMT_RLE16, 200, 100, 0, rle);
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
