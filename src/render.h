// The renderer: what the Voodoo registers describe, drawn with Direct3D 9.
#pragma once
#include "voodoo.h"

namespace gl2d9 {

// A vertex as the Voodoo's triangle setup takes it (screen coordinates, Glide's units).
struct RVtx {
    float x, y;
    float ooz;  // Z buffer value (0..65535)
    float oow;  // 1/w (the FBI's)
    float r, g, b, a;  // 0..255
    float sow[2], tow[2], oowt[2];  // per TMU: s/w, t/w (256-texel units) and its 1/w
    float fog;  // what the fog table is looked up with (1/w, or Glide 3's fog coordinate)
};

bool RenderOpen(HWND hwnd);  // the device and the buffers, at g_board.screenW x screenH
void RenderClose();
bool RenderReady();
// Draws triangles (n vertices, three each), already culled and in drawing order.
// Stipple rotate mode: pixels the pipeline wrote through the frame buffer turn the register too.
void RenderStippleAdvance(unsigned pixels);
void RenderTriangles(const RVtx* v, int n);
// grBufferClear: the draw buffer within the clip window, as the write masks allow.
void RenderClear(FxU32 argb, FxU8 alpha, FxU16 depth);
// grBufferSwap: the back buffer shown.
void RenderSwap(int interval);
void RenderFlush();  // draws what is batched (before the state or the texture memory changes)
void RenderSetGamma(float gamma);
// The window was moved, resized or activated: its picture redrawn.
void RenderRefresh();

// The texture memory or a table was written: what is batched drawn first, the textures
// looked at again.
void RenderTexturesChanged();

// The linear frame buffer. Reads a color buffer (front or back) at the game's resolution as ARGB.
bool RenderReadBuffer(int buffer, std::vector<FxU32>* out);
// Reads the aux buffer as Glide's 16-bit values: the depth buffer's Z or W, or (the alpha
// buffer enabled) the alpha.
bool RenderReadAux(std::vector<FxU16>* out);
// Writes pixels w x h at x, y: argb into the color buffer (null: none), aux into the aux
// buffer (null: none), only where mask is set (null: all). Through the pixel pipeline
// (chroma key, alpha and depth tests, fog, blending; the depth the pixel's or the constant
// one) or directly.
void RenderWritePixels(int buffer, int x, int y, int w, int h, const FxU32* argb, const FxU16* aux, const FxU8* mask,
                       bool pipeline);

}  // namespace gl2d9
