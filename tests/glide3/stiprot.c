#include <windows.h>
#include <glide.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
/* Stipple rotate mode against a model of the Voodoo's rasterizer (MAME's rules): white
   triangles on black, the pattern 0xF0E1A53C turning one bit per pixel from row to row and
   triangle to triangle, the last two under a clip window; at frame 3 with the origin upper
   left, at frame 6 lower left. Each frame read back is compared with the model pixel by pixel.
   Right: a handful differ, all on edges, where Direct3D's coverage misses a pixel the Voodoo
   draws (a wrong position in the pattern would make thousands differ). */
#define W 640
#define H 480
typedef struct { float x, y; } V;
static unsigned char want[H][W];
static unsigned count;
static const unsigned kPattern = 0xF0E1A53Cu;
static int clipOn, cl, cr, ct, cb;
static int rnd(float v) { float ip = floorf(v); return (int)ip + ((v - ip) > 0.5f ? 1 : 0); }
static float snap(float v) { return (float)(int)(v * 16.0f) / 16.0f; }
/* One triangle as the Voodoo rasterizes it, in Glide's coordinates (the clip window too). */
static void model(V a, V b, V c) {
  V p[3], *v1 = &p[0], *v2 = &p[1], *v3 = &p[2], *t;
  int y, x, y0, y1; float d12, d13, d23;
  p[0].x = snap(a.x); p[0].y = snap(a.y); p[1].x = snap(b.x); p[1].y = snap(b.y); p[2].x = snap(c.x); p[2].y = snap(c.y);
  if (v2->y < v1->y) { t = v1; v1 = v2; v2 = t; }
  if (v3->y < v2->y) { t = v2; v2 = v3; v3 = t; if (v2->y < v1->y) { t = v1; v1 = v2; v2 = t; } }
  y0 = rnd(v1->y); y1 = rnd(v3->y); if (y0 < 0) y0 = 0; if (y1 > H) y1 = H;
  d12 = v2->y == v1->y ? 0 : (v2->x - v1->x) / (v2->y - v1->y);
  d13 = v3->y == v1->y ? 0 : (v3->x - v1->x) / (v3->y - v1->y);
  d23 = v3->y == v2->y ? 0 : (v3->x - v2->x) / (v3->y - v2->y);
  for (y = y0; y < y1; y++) {
    float fy = y + 0.5f, sx = v1->x + (fy - v1->y) * d13;
    float ex = fy < v2->y ? v1->x + (fy - v1->y) * d12 : v2->x + (fy - v2->y) * d23;
    int s = rnd(sx), e = rnd(ex), tt; if (s > e) { tt = s; s = e; e = tt; }
    if (s < 0) s = 0; if (e > W) e = W;
    if (clipOn) { if (y < ct || y >= cb || s >= cr) continue; if (e > cr) e = cr; if (s < cl) s = cl; }
    for (x = s; x < e; x++) {           /* the register rotated left, then bit 31 */
      if ((kPattern >> ((30u - count) & 31u)) & 1) want[y][x] = 1;
      count++;
    }
  }
}
static void tri(V a, V b, V c) { model(a, b, c); grDrawTriangle(&a, &b, &c); }
int main(void) {
  static FxU16 got[H][W]; int f, x, y, bad, drawn, lower;
  grGlideInit(); grSstSelect(0);
  grSstWinOpen(0, GR_RESOLUTION_640x480, GR_REFRESH_60Hz, GR_COLORFORMAT_ARGB, GR_ORIGIN_UPPER_LEFT, 2, 1);
  grVertexLayout(GR_PARAM_XY, 0, GR_PARAM_ENABLE);
  grColorCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_NONE, GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_NONE, FXFALSE);
  grConstantColorValue(0xFFFFFFFF);
  for (f = 0; f < 100000; f++) {
    V a = {20.3f, 30.7f}, b = {300.9f, 60.2f}, c = {140.1f, 250.5f};
    V d = {320.5f, 20.25f}, e = {620.75f, 200.1f}, g = {400.0f, 300.0f};
    V h1 = {50.0f, 300.0f}, h2 = {600.0f, 305.5f}, h3 = {55.5f, 309.0f};     /* thin */
    V i1 = {100.0f, 350.0f}, i2 = {500.0f, 470.0f}, i3 = {90.0f, 460.0f};
    lower = f >= 5 && f < 7;
    grSstOrigin(lower ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT);
    grBufferClear(0, 0, 0xFFFF);
    memset(want, 0, sizeof(want)); count = 0; clipOn = 0;
    grStipplePattern(kPattern); grStippleMode(GR_STIPPLE_ROTATE);
    grClipWindow(0, 0, W, H);
    tri(a, b, c); tri(d, e, g); tri(h1, h2, h3);
    clipOn = 1; cl = 150; cr = 420; ct = 360; cb = 440; grClipWindow(cl, ct, cr, cb);
    tri(i1, i2, i3); tri(a, b, c);
    grStippleMode(GR_STIPPLE_DISABLE); grClipWindow(0, 0, W, H);
    if (f == 3 || f == 6) {
      grLfbReadRegion(GR_BUFFER_BACKBUFFER, 0, 0, W, H, W * 2, got);   /* (the screen's rows, top down) */
      bad = drawn = 0;
      for (y = 0; y < H; y++) for (x = 0; x < W; x++) {
        int on = got[lower ? H - 1 - y : y][x] != 0;
        drawn += on; bad += on != want[y][x];
      }
      printf("rotate stipple, origin %s: %d pixels drawn, %d differ from the model\n", lower ? "lower left" : "upper left", drawn, bad);
      fflush(stdout);
    }
    grBufferSwap(1);
  }
  grGlideShutdown(); return 0;
}
