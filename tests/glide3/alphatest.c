#include <glide.h>
#include <stdio.h>
/* The alpha test on the Voodoo's 8-bit combined alpha. A constant alpha k tested against the
   reference k for every k with GREATER, EQUAL and LESS (only EQUAL draws); then iterated alphas
   with a fraction tested GREATER their integer part (the fraction is dropped: none draws);
   then frame buffer writes through the pixel pipeline (8888, alpha k) against the reference k.
   Prints `0 of 256 wrong` six times and `not drawn` four times. */
typedef struct { float x, y, a; } V;
static void quad(float a) {
  V v[4] = {{100,100,a},{200,100,a},{200,200,a},{100,200,a}};
  grDrawTriangle(&v[0], &v[1], &v[2]); grDrawTriangle(&v[0], &v[2], &v[3]);
}
int main(void) {
  GrContext_t c; int k, fn, i; FxU16 px; FxU32 block[16];
  static const int fns[3] = {GR_CMP_GREATER, GR_CMP_EQUAL, GR_CMP_LESS};
  static const char* nm[3] = {"GREATER", "EQUAL", "LESS"};
  static const float as[4] = {127.6f, 127.0f, 100.2f, 128.4f};
  grGlideInit(); grSstSelect(0);
  c = grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  if (!c) return 1;
  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grVertexLayout(GR_PARAM_A, 8, GR_PARAM_ENABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_CONSTANT, FXFALSE);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_CONSTANT, FXFALSE);
  grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
  for (fn = 0; fn < 3; fn++) {
    int wrong = 0;
    for (k = 0; k < 256; k++) {
      grBufferClear(0x000000, 0, 0);
      grConstantColorValue(((FxU32)k << 24) | 0xFFFFFF);
      grAlphaTestFunction(fns[fn]); grAlphaTestReferenceValue((GrAlpha_t)k);
      quad(0);
      grAlphaTestFunction(GR_CMP_ALWAYS);
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 150, 150, 1, 1, 2, &px);
      if ((px != 0) != (fn == 1)) wrong++;
    }
    printf("constant alpha k %s k: %d of 256 wrong\n", nm[fn], wrong);
  }
  grConstantColorValue(0xFFFFFFFF);
  grAlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_CONSTANT, FXFALSE);
  for (i = 0; i < 4; i++) {
    grBufferClear(0, 0, 0);
    grAlphaTestFunction(GR_CMP_GREATER); grAlphaTestReferenceValue((GrAlpha_t)(int)as[i]);
    quad(as[i]);
    grLfbReadRegion(GR_BUFFER_BACKBUFFER, 150, 150, 1, 1, 2, &px);
    printf("iterated alpha %.1f GREATER %d: %s\n", as[i], (int)as[i], px ? "drawn (wrong)" : "not drawn");
  }
  grAlphaTestFunction(GR_CMP_ALWAYS);
  for (fn = 0; fn < 3; fn++) {
    int wrong = 0;
    for (k = 0; k < 256; k++) {
      for (i = 0; i < 16; i++) block[i] = ((FxU32)k << 24) | 0xFFFFFF;
      grBufferClear(0x000000, 0, 0);
      grAlphaTestFunction(fns[fn]); grAlphaTestReferenceValue((GrAlpha_t)k);
      grLfbWriteRegion(GR_BUFFER_BACKBUFFER, 148, 148, GR_LFB_SRC_FMT_8888, 4, 4, FXTRUE, 16, block);
      grAlphaTestFunction(GR_CMP_ALWAYS);
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 150, 150, 1, 1, 2, &px);
      if ((px != 0) != (fn == 1)) wrong++;
    }
    printf("frame buffer write alpha k %s k: %d of 256 wrong\n", nm[fn], wrong);
  }
  fflush(stdout); grGlideShutdown(); return 0;
}
