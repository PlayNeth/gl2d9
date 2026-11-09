// The renderer's stipple rotate mode: the rows the Voodoo walks, for the shader.
#include "renderint.h"

#include <algorithm>
#include <cmath>

#include "shader.h"

namespace gl2d9 {

// The batch's stipple rows into their texture (s4), 2048 a line, and where it is (c12).
void UploadStippleRows() {
    IDirect3DDevice9* d = g_r.dev;
    const size_t rows = g_r.stipRows.size() / 4;
    const int h = int((rows + 2047) / 2048);
    if (g_r.stipTex && g_r.stipTexH < h) g_r.stipTex->Release(), g_r.stipTex = nullptr;
    if (!g_r.stipTex) {
        const int th = std::max(h, 16);
        if (FAILED(d->CreateTexture(2048, th, 1, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &g_r.stipTex,
                                    nullptr))) {
            g_r.stipTex = nullptr;
            return;
        }
        g_r.stipTexH = th;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(g_r.stipTex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) return;
    for (int y = 0; y < h; y++) {
        const size_t first = size_t(y) * 2048, n = std::min<size_t>(2048, rows - first);
        memcpy(static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch, &g_r.stipRows[first * 4], n * 16);
    }
    g_r.stipTex->UnlockRect(0);
    BindStage(4, g_r.stipTex);
    PointSampler(4);
    const float k[4] = {float(g_board.screenH - 1), (g_r.batchRegs.fbzMode & SST_YORIGIN) ? 1.0f : 0.0f, 1.0f / 2048.0f,
                        1.0f / float(g_r.stipTexH)};
    Constants(kPsStippleRows, k, 1);
}

// ---------------------------------------------------------------- stipple rotate mode

// The Voodoo's stipple rotate mode: the stipple register turns one bit for each pixel the
// rasterizer reaches (after clipping, before the other tests), row by row, left to right, from
// triangle to triangle, and bit 31 says whether the pixel is drawn; it starts over when the
// register is written. As MAME's Voodoo (0.220's sequential register: rotated left, then bit 31,
// so the k-th pixel since the write tests bit 30 - k; current MAME rotates right on a per-row
// copy, for its threads). A GPU draws in parallel, so the CPU walks each triangle's rows as the
// Voodoo does (MAME's poly_manager: vertices snapped to 1/16 by truncation, rows round(y1) to
// round(y3), each from round(x) on the long edge to round(x) on the other at the row's middle,
// round taking .5 down) and gives the shader every row's span and the bit of its first pixel.
// (0.220's direction is kept: which one the chip has is not known, and that is what the tests
// were made against.)

namespace {

// (A coordinate far off the screen, or not a number, as one just off it: converted to an int,
// an out-of-range float is undefined.)
float InRange(float v) { return v == v ? std::clamp(v, -1.0e7f, 1.0e7f) : 0.0f; }
int RoundCoordinate(float v) {
    v = InRange(v);
    const float ip = std::floor(v);
    return int(ip) + ((v - ip) > 0.5f ? 1 : 0);
}
float Snap16(float v) {  // the FIFO's float to 12.4 fixed point (MAME's float_to_int32: toward zero)
    return float(int(InRange(v) * 16.0f)) * (1.0f / 16.0f);
}

}  // namespace

// The rows of a triangle (three vertices) into stipRows; first: its first entry and scanline.
// One empty row above and below the Voodoo's: Direct3D's rows of the triangle may reach one
// further, and those pixels aren't drawn.
void StippleRows(const RVtx* t, const Regs& r, float* firstEntry, float* firstScan) {
    struct P {
        float x, y;
    } a = {Snap16(t[0].x), Snap16(t[0].y)}, b = {Snap16(t[1].x), Snap16(t[1].y)}, c = {Snap16(t[2].x), Snap16(t[2].y)};
    const P* v1 = &a;
    const P* v2 = &b;
    const P* v3 = &c;
    if (v2->y < v1->y) std::swap(v1, v2);
    if (v3->y < v2->y) {
        std::swap(v2, v3);
        if (v2->y < v1->y) std::swap(v1, v2);
    }
    const int W = g_board.screenW, H = g_board.screenH;
    const int v1y = RoundCoordinate(v1->y), v3y = RoundCoordinate(v3->y);
    // (Within the screen, both: a triangle far below it had rows counted up to it.)
    const int y0 = std::clamp(v1y, 0, H), y1 = std::clamp(v3y, 0, H);
    const float d12 = v2->y == v1->y ? 0.0f : (v2->x - v1->x) / (v2->y - v1->y);
    const float d13 = v3->y == v1->y ? 0.0f : (v3->x - v1->x) / (v3->y - v1->y);
    const float d23 = v3->y == v2->y ? 0.0f : (v3->x - v2->x) / (v3->y - v2->y);
    const bool clip = (r.fbzMode & SST_ENRECTCLIP) != 0;
    const int cl = int((r.clipLeftRight >> SST_CLIPLEFT_SHIFT) & 0xFFF), cr = int(r.clipLeftRight & 0xFFF);
    const int ct = int((r.clipBottomTop >> SST_CLIPBOTTOM_SHIFT) & 0xFFF), cb = int(r.clipBottomTop & 0xFFF);
    const int top = std::min(y0, y1) - 1, bottom = std::max(y0, y1) + 1;  // (the empty rows around)
    *firstEntry = float(g_r.stipRows.size() / 4);
    *firstScan = float(top);
    for (int y = top; y < bottom; y++) {
        int sx = 0, ex = 0;
        if (y >= y0 && y < y1) {
            const float fy = float(y) + 0.5f;
            const float startx = v1->x + (fy - v1->y) * d13;
            const float stopx = fy < v2->y ? v1->x + (fy - v1->y) * d12 : v2->x + (fy - v2->y) * d23;
            sx = RoundCoordinate(startx), ex = RoundCoordinate(stopx);
            if (sx > ex) std::swap(sx, ex);
            sx = std::max(sx, 0), ex = std::min(ex, W);
            if (clip) {  // (the clip window relative to the origin, as the scissor: ClipRect)
                if (y < ct || y >= cb || sx >= cr) sx = ex = 0;
                ex = std::min(ex, cr);
                sx = std::max(sx, cl);
            }
            if (sx >= ex) sx = ex = 0;
        }
        const FxU32 k = g_r.stipCount;
        g_r.stipRows.insert(g_r.stipRows.end(), {float(sx), float(ex), float((30u - k) & 31u), -1.0f});
        g_r.stipCount = (k + FxU32(ex - sx)) & 31u;
    }
}

// Pixels the pipeline wrote through the frame buffer: the register turns for them too (the
// pattern isn't applied to them: no game is known to stipple frame buffer writes, and current
// MAME warns of it as unexpected).
void RenderStippleAdvance(unsigned pixels) {
    const Regs& r = St().r;
    if ((r.fbzMode & (SST_ENSTIPPLE | SST_ENSTIPPLEPATTERN)) != SST_ENSTIPPLE) return;
    g_r.stipCount = (g_r.stipCount + pixels) & 31u;
}

}  // namespace gl2d9
