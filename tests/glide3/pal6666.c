#include <glide.h>
#include <stdio.h>
#include <string.h>
/* An 8-bit palette texture with a 6666 palette (GR_TEXTABLE_PALETTE_6666_EXT, which Voodoo 2's
   Glide 3 offers as PALETTE6666): its texels carry the palette's alpha. Left: entry 1, red at
   alpha 0x80, blended over blue. Right: entry 2, green at alpha 0, alpha tested away (blue
   stays). Prints the extension string and both pixels read back (565): `800f 001f` is right
   (red 0x82 over blue 0x7d; blue alone); `f800 07e0` is the palette's alpha lost. */
typedef struct { float x, y, s, t, w; } V;
static FxU8 tex[16*16];
static void quad(float x0, float y0, float x1, float y1) {
  V v[4] = {{x0,y0,0,0,1},{x1,y0,64,0,1},{x1,y1,64,64,1},{x0,y1,0,64,1}};
  grDrawTriangle(&v[0], &v[1], &v[2]); grDrawTriangle(&v[0], &v[2], &v[3]);
}
int main(void) {
  GrContext_t c; GrTexInfo ti; GuTexPalette pal; int i, f; FxU16 px[2];
  const char* ext;
  grGlideInit(); grSstSelect(0);
  c = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  if (!c) return 1;
  ext = grGetString(GR_EXTENSION);
  printf("extensions: %s (PALETTE6666 %s)\n", ext, strstr(ext, "PALETTE6666") ? "offered" : "missing");
  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_ST0, 8, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Q, 16, GR_PARAM_ENABLE);
  for (i = 0; i < 256; i++) pal.data[i] = 0;
  pal.data[1] = 0x80FF0000;  /* ARGB: red, alpha 0x80 */
  pal.data[2] = 0x0000FF00;  /* green, alpha 0 */
  ti.smallLodLog2 = ti.largeLodLog2 = GR_LOD_LOG2_16; ti.aspectRatioLog2 = GR_ASPECT_LOG2_1x1; ti.format = GR_TEXFMT_P_8; ti.data = tex;
  for (f = 0; f < 100000; f++) {
    grBufferClear(0x0000FF, 0, 0);
    grTexDownloadTable(GR_TEXTABLE_PALETTE_6666_EXT, &pal);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    grAlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    /* left: entry 1, blended */
    memset(tex, 1, sizeof(tex));
    grTexDownloadMipMap(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &ti);
    grAlphaBlendFunction(GR_BLEND_SRC_ALPHA, GR_BLEND_ONE_MINUS_SRC_ALPHA, GR_BLEND_ZERO, GR_BLEND_ZERO);
    quad(40, 140, 300, 340);
    /* right: entry 2, alpha tested */
    memset(tex, 2, sizeof(tex));
    grTexDownloadMipMap(GR_TMU0, grTexMinAddress(GR_TMU0) + 4096, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexSource(GR_TMU0, grTexMinAddress(GR_TMU0) + 4096, GR_MIPMAPLEVELMASK_BOTH, &ti);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ZERO, GR_BLEND_ZERO);
    grAlphaTestFunction(GR_CMP_GREATER); grAlphaTestReferenceValue(0);
    quad(340, 140, 600, 340);
    grAlphaTestFunction(GR_CMP_ALWAYS);
    if (f == 3) {
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 170, 240, 1, 1, 2, &px[0]);
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 470, 240, 1, 1, 2, &px[1]);
      printf("read %04x %04x\n", px[0], px[1]);
      fflush(stdout);
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
