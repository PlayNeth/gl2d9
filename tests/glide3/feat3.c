#include <glide.h>
#include <stdio.h>
/* Top left: pattern stipple (red, 4-pixel stripes swapping every 2 rows). Top right: a
   mirrored texture (black->white ramp, mirrored: a triangle wave). Bottom left: PREFOG blend
   over gray (green pre-fog, blue after fog): dark green when right, dark blue when not. */
typedef struct { float x, y, a, s, t, w; } V;
static FxU16 ramp[16*16];
static void quad(float x0, float y0, float x1, float y1, float s1, float a) {
  V v[4] = {{x0,y0,a,0,0,1},{x1,y0,a,s1,0,1},{x1,y1,a,s1,s1,1},{x0,y1,a,0,s1,1}};
  grDrawTriangle(&v[0], &v[1], &v[2]); grDrawTriangle(&v[0], &v[2], &v[3]);
}
int main(void) {
  GrContext_t c; GrTexInfo ti; int x, y, f;
  grGlideInit(); grSstSelect(0);
  c = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  if (!c) return 1;
  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_A, 8, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_ST0, 12, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_Q, 20, GR_PARAM_ENABLE);
  for (y = 0; y < 16; y++) for (x = 0; x < 16; x++) { int g = x * 31 / 15; ramp[y*16+x] = (FxU16)(g << 11 | (g*2) << 5 | g); }
  ti.smallLodLog2 = ti.largeLodLog2 = GR_LOD_LOG2_16; ti.aspectRatioLog2 = GR_ASPECT_LOG2_1x1; ti.format = GR_TEXFMT_RGB_565; ti.data = ramp;
  grTexDownloadMipMap(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &ti);
  for (f = 0; f < 100000; f++) {
    grBufferClear(0x808080, 0, 0);
    /* stipple */
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0xFFFF0000);
    grStipplePattern(0x0F0FF0F0); grStippleMode(GR_STIPPLE_PATTERN);
    quad(20, 20, 300, 220, 0, 255);
    grStippleMode(GR_STIPPLE_DISABLE);
    /* mirror */
    grTexSource(GR_TMU0, grTexMinAddress(GR_TMU0), GR_MIPMAPLEVELMASK_BOTH, &ti);
    grTexCombine(GR_TMU0, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, FXFALSE, FXFALSE);
    grTexFilterMode(GR_TMU0, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
    grTexClampMode(GR_TMU0, GR_TEXTURECLAMP_MIRROR_EXT, GR_TEXTURECLAMP_MIRROR_EXT);
    grColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_NONE, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
    quad(340, 20, 620, 220, 256 * 4, 255);
    /* prefog */
    grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
    grConstantColorValue(0xFF00FF00);
    grFogColorValue(0xFF0000FF);   /* ARGB: blue */
    grFogMode(GR_FOG_WITH_ITERATED_ALPHA_EXT);
    grAlphaBlendFunction(GR_BLEND_ZERO, GR_BLEND_PREFOG_COLOR, GR_BLEND_ZERO, GR_BLEND_ZERO);
    quad(20, 260, 300, 460, 0, 255);
    grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    grFogMode(GR_FOG_DISABLE);
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
