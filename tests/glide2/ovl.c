#include <glide.h>
#include <stdio.h>
#include <stdlib.h>
/* Headlights on a road: a sloped road (depth written) cut one way, then an additive light
   (depth LEQUAL, no depth write) over the same plane cut another way. Top: Z buffer, bottom:
   W buffer. A clean light patch is right; a fine hatch of road showing through is the fight. */
static float jit;  /* the light's depth a little off (its own maths), under one 16-bit step */
static void vtx(GrVertex* o, float u, float v, float y0) {
  float w = 1 / (0.5f + (1.0f / 32 - 0.5f) * v);  /* the road recedes, w 2 to 32: 1/w linear on screen */
  o->x = 40 + u * 560 + (v * 0.37f);  /* (slightly skewed: no exact grid) */
  o->y = y0 + 200 - v * 190;
  o->oow = 1.0f / w;
  o->ooz = 65535.0f * (1 - 1.0f / w) * 0.97f;
  if (jit != 0) { float r = (rand() % 2001 - 1000) / 1000.0f; o->ooz += r * 0.6f; o->oow *= 1 + r * 0.00012f; }
}
static void tri(float u0, float v0, float u1, float v1, float u2, float v2, float y0) {
  GrVertex a, b, c; vtx(&a, u0, v0, y0); vtx(&b, u1, v1, y0); vtx(&c, u2, v2, y0); grDrawTriangle(&a, &b, &c);
}
int main(int argc, char** argv) {
  GrHwConfiguration hw; int f, k, i;
  grGlideInit(); grSstQueryHardware(&hw); grSstSelect(0);
  if (!grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1)) return 1;
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  for (f = 0; f < 100000; f++) {
    grDepthMask(FXTRUE); grBufferClear(0x000000, 0, 0xFFFF);
    for (k = 0; k < 2; k++) {
      float y0 = 20 + k * 230;
      grDepthBufferMode(k ? GR_DEPTHBUFFER_WBUFFER : GR_DEPTHBUFFER_ZBUFFER);
      grDepthBufferFunction(GR_CMP_LESS); grDepthMask(FXTRUE);
      grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
      grConstantColorValue(0xFF404040);
      for (i = 0; i < 8; i++) { float a = i / 8.0f, b = (i + 1) / 8.0f;  /* road: strips along v */
        tri(0, a, 1, a, 1, b, y0); tri(0, a, 1, b, 0, b, y0); }
      grDepthBufferFunction(GR_CMP_LEQUAL); grDepthMask(FXFALSE);
      grAlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ONE, GR_BLEND_ZERO);
      grConstantColorValue(0xFFC0A000); jit = 1; srand(7);
      for (i = 0; i < 5; i++) { float a = 0.1f + i * 0.16f, b = a + 0.16f; /* light: strips along u */
        tri(a, 0.05f, b, 0.05f, b, 0.9f, y0); tri(a, 0.05f, b, 0.9f, a, 0.9f, y0); }
      jit = 0;
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
