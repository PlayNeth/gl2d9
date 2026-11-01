#include <glide.h>
#include <stdio.h>
/* UT-like single-pass multitexture: TMU1 = texture (P8, its own palette, ST_DIFF_TMU1),
   TMU0 = lightmap (RGB565) * TMU1. Left half: a checker texture; lightmap: horizontal ramp. */
static FxU8 tex8[64*64]; static FxU16 lm[32*32]; static GuTexPalette pal0, pal1;
int main(void) {
  GrHwConfiguration hw; GrTexInfo t1, t0; GrVertex v[4]; int i, x, y, f;
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  for (y = 0; y < 64; y++) for (x = 0; x < 64; x++) tex8[y*64+x] = ((x >> 3) ^ (y >> 3)) & 1 ? 1 : 2;
  for (i = 0; i < 256; i++) { pal1.data[i] = 0; pal0.data[i] = 0xFF00FF; }
  pal1.data[1] = 0xFFFFFF; pal1.data[2] = 0x0000FF;   /* white / blue checker */
  for (y = 0; y < 32; y++) for (x = 0; x < 32; x++) { int g = x * 63 / 31; lm[y*32+x] = (FxU16)((g >> 1) << 11 | g << 5 | (g >> 1)); }
  t1.smallLod = t1.largeLod = GR_LOD_64; t1.aspectRatio = GR_ASPECT_1x1; t1.format = GR_TEXFMT_P_8; t1.data = tex8;
  t0.smallLod = t0.largeLod = GR_LOD_32; t0.aspectRatio = GR_ASPECT_1x1; t0.format = GR_TEXFMT_RGB_565; t0.data = lm;
  grTexDownloadMipMap(GR_TMU1, 0, GR_MIPMAPLEVELMASK_BOTH, &t1);
  grTexDownloadMipMap(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &t0);
  grTexDownloadTable(GR_TMU1, GR_TEXTABLE_PALETTE, &pal1);
  grTexDownloadTable(GR_TMU0, GR_TEXTABLE_PALETTE, &pal0);   /* a different palette on TMU0 */
  grTexSource(GR_TMU1, 0, GR_MIPMAPLEVELMASK_BOTH, &t1);
  grTexSource(GR_TMU0, 0, GR_MIPMAPLEVELMASK_BOTH, &t0);
  guTexCombineFunction(GR_TMU1, GR_TEXTURECOMBINE_DECAL);
  grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, 0, 0); grTexClampMode(GR_TMU1, GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_WRAP);
  grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_TEXTURE, 0);
  grConstantColorValue(0xFFFFFFFF);
  grHints(GR_HINT_STWHINT, 0);
  for (i = 0; i < 4; i++) {
    v[i].x = (i & 1) ? 600 : 40; v[i].y = (i & 2) ? 440 : 40; v[i].oow = 1;
    v[i].tmuvtx[0].sow = 7; v[i].tmuvtx[0].tow = 3;            /* lightmap once */
    v[i].tmuvtx[1].sow = (i & 1) ? 255 * 4 : 0; v[i].tmuvtx[1].tow = (i & 2) ? 255 * 4 : 0;    /* texture 4x */
  }
  for (f = 0; f < 100000; f++) { grBufferClear(0x404040, 0, 0); grDrawTriangle(&v[0], &v[1], &v[3]); grDrawTriangle(&v[0], &v[3], &v[2]); grBufferSwap(1); }
  grGlideShutdown(); return 0;
}
