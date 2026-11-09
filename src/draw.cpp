// The renderer's drawing: triangles batched by state and drawn, quads, buffer clears.
#include "renderint.h"

#include <algorithm>
#include <cmath>

#include "shader.h"

namespace gl2d9 {

const char* kClearPs =
    "float4 c : register(c0);\n"
    "float4 main() : COLOR { return c; }\n";

namespace {

// Which of a vertex's s, t and 1/w each TMU iterates, as Glide 2 sends them (3dfx's
// _grUpdateParamIndex): a TMU whose texture combine reads its texel is active; the FBI's 1/w
// goes to every TMU, a TMU's own only with the hint that it differs; TMU1's s, t are TMU0's
// unless they differ (the hint) or TMU0 isn't active (TMU1's own are sent then).
struct TmuParams {
    bool ownW0, ownW1, ownSt1;
};

void Convert(const RVtx& v, DVtx* o, bool flipY, const TmuParams& p) {
    const float s = float(g_r.scale);
    const float y = flipY ? float(g_board.screenH) - v.y : v.y;
    o->x = v.x * s - 0.5f;
    o->y = y * s - 0.5f;
    o->z = 0;       // (with depth buffering on the shader writes the depth, ShaderKey::depthOut)
    o->rhw = 1.0f;  // every attribute interpolated linearly on the screen, as the Voodoo iterates them
    o->col[0] = v.r;
    o->col[1] = v.g;
    o->col[2] = v.b;
    o->col[3] = v.a;
    const float q0 = p.ownW0 ? v.oowt[0] : v.oow;
    o->st0[0] = v.sow[0];
    o->st0[1] = v.tow[0];
    o->st0[2] = q0;
    o->st0[3] = 0;
    o->st1[0] = p.ownSt1 ? v.sow[1] : v.sow[0];
    o->st1[1] = p.ownSt1 ? v.tow[1] : v.tow[0];
    o->st1[2] = p.ownW1 ? v.oowt[1] : v.oow;
    o->st1[3] = 0;
    o->misc[0] = v.fog;
    o->misc[1] = std::clamp(v.ooz / 65535.0f, 0.0f, 1.0f);
    o->misc[2] = v.oow;
    o->misc[3] = v.ooz;
}

}  // namespace

// ---------------------------------------------------------------- drawing

void RenderFlush() {
    // (Without its shader the batch isn't drawn: fixed function would draw raw textures.)
    if (g_r.batch.empty() || !g_r.batchPs || !RenderReady()) {
        g_r.batch.clear();
        g_r.stipRows.clear();
        return;
    }
    IDirect3DDevice9* d = g_r.dev;
    Dirty();
    if (!g_r.stipRows.empty()) UploadStippleRows();
    const bool copies = g_r.batchBlend && g_r.dstCopy;
    if (copies || g_r.biasWritePs) {
        // Blended in the shader, each triangle is drawn over a copy of what it covers (it may
        // cover the one before); compare to bias with depth writes, in two passes, as a later
        // one tests against the depth an earlier one wrote. The first tests zaColor's depth and
        // marks the pixels that pass in the stencil buffer (no color or depth written); the
        // second draws those, writing the pixel's own depth, and clears the mark. Triangles go
        // in groups whose rectangles don't overlap: none in a group reads what another of it
        // writes, so a group is drawn at once, over one copy (of its rectangles' union).
        IDirect3DSurface9* rt = nullptr;
        IDirect3DSurface9* copy = nullptr;
        if (copies) {
            d->GetRenderTarget(0, &rt);
            g_r.dstCopy->GetSurfaceLevel(0, &copy);
        }
        if (g_r.biasWritePs) {
            RenderState(D3DRS_STENCILREF, 1);
            RenderState(D3DRS_STENCILMASK, 0xFF);
            RenderState(D3DRS_STENCILWRITEMASK, 0xFF);
            RenderState(D3DRS_STENCILFAIL, D3DSTENCILOP_KEEP);
            RenderState(D3DRS_STENCILZFAIL, D3DSTENCILOP_KEEP);
        }
        std::vector<RECT> group;
        RECT all = {};
        size_t first = 0;
        auto drawGroup = [&](size_t end) {
            if (group.empty()) return;
            group.clear();
            const DVtx* v = &g_r.batch[first];
            const UINT n = UINT((end - first) / 3);
            if (copies) {
                EndDraw();
                if (rt && copy) d->StretchRect(rt, &all, copy, &all, D3DTEXF_NONE);
            }
            BeginDraw();
            if (!g_r.inScene) return;
            if (g_r.biasWritePs) {
                UseShader(g_r.batchPs);
                RenderState(D3DRS_COLORWRITEENABLE, 0);
                RenderState(D3DRS_ZFUNC, g_r.batchZFunc);
                RenderState(D3DRS_ZWRITEENABLE, FALSE);
                RenderState(D3DRS_STENCILENABLE, TRUE);
                RenderState(D3DRS_STENCILFUNC, D3DCMP_ALWAYS);
                RenderState(D3DRS_STENCILPASS, D3DSTENCILOP_REPLACE);
                d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n, v, sizeof(DVtx));
                UseShader(g_r.biasWritePs);
                RenderState(D3DRS_COLORWRITEENABLE, g_r.batchMask);
                RenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
                RenderState(D3DRS_ZWRITEENABLE, TRUE);
                RenderState(D3DRS_STENCILFUNC, D3DCMP_EQUAL);
                RenderState(D3DRS_STENCILPASS, D3DSTENCILOP_ZERO);
            }
            d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, n, v, sizeof(DVtx));
        };
        for (size_t i = 0; i + 3 <= g_r.batch.size(); i += 3) {
            const DVtx* v = &g_r.batch[i];
            float x0 = std::min({v[0].x, v[1].x, v[2].x}), x1 = std::max({v[0].x, v[1].x, v[2].x});
            float y0 = std::min({v[0].y, v[1].y, v[2].y}), y1 = std::max({v[0].y, v[1].y, v[2].y});
            RECT rc = {LONG(std::floor(x0)), LONG(std::floor(y0)), LONG(std::ceil(x1)) + 2, LONG(std::ceil(y1)) + 2};
            rc.left = std::clamp<LONG>(rc.left, 0, g_r.w), rc.right = std::clamp<LONG>(rc.right, rc.left, g_r.w);
            rc.top = std::clamp<LONG>(rc.top, 0, g_r.h), rc.bottom = std::clamp<LONG>(rc.bottom, rc.top, g_r.h);
            if (rc.right <= rc.left || rc.bottom <= rc.top) {  // (nothing on the target: left out)
                drawGroup(i);
                first = i + 3;
                continue;
            }
            bool overlaps = group.size() >= 64;  // (the test is quadratic: groups kept small)
            for (size_t j = 0; j < group.size() && !overlaps; j++) {
                RECT both;
                overlaps = IntersectRect(&both, &group[j], &rc) != FALSE;
            }
            if (overlaps) {
                drawGroup(i);
                first = i;
            }
            if (group.empty()) all = rc;
            else UnionRect(&all, &all, &rc);
            group.push_back(rc);
        }
        drawGroup(g_r.batch.size());
        if (g_r.biasWritePs) {  // (back to the batch's state, as ApplyState left it)
            UseShader(g_r.batchPs);
            RenderState(D3DRS_ZFUNC, g_r.batchZFunc);
            RenderState(D3DRS_STENCILENABLE, FALSE);
        }
        if (copy) copy->Release();
        if (rt) rt->Release();
    } else {
        BeginDraw();
        if (g_r.inScene)
            d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(g_r.batch.size() / 3), g_r.batch.data(), sizeof(DVtx));
    }
    g_r.batch.clear();
    if (!g_r.stipRows.empty()) {
        g_r.stipRows.clear();
        BindStage(4, nullptr);
    }
}

namespace {

// The registers as triangles are drawn with them: those that can't change the drawing cleared
// (the LFB mode; fog, chroma key and stipple registers while those are off; a third TMU), so
// that changing them doesn't end the batch.
Regs DrawnRegs(const Regs& in) {
    Regs r = in;
    r.lfbMode = 0;
    if (!(r.fogMode & SST_ENFOGGING)) {
        r.fogColor = 0;
        memset(r.fogTable, 0, sizeof(r.fogTable));
    }
    if (!(r.fbzMode & SST_ENCHROMAKEY)) r.chromaKey = 0;
    if (!(r.fbzMode & SST_ENSTIPPLE)) r.stipple = 0;
    for (int t = 2; t < kMaxTmu; t++) r.tmu[t] = TmuRegs{};
    return r;
}

void DrawTriangles(const RVtx* v, int n) {
    if (!RenderReady() || n < 3) return;
    const GlideState& st = St();
    // A state change ends the batch (the registers compared: cheap next to a draw call).
    const Regs regs = DrawnRegs(st.r);
    const float cc[4] = {st.ccR, st.ccG, st.ccB, st.ccA};
    if (!g_r.batchState || memcmp(&g_r.batchRegs, &regs, sizeof(Regs)) != 0 || g_r.batchDelta0 != (st.ccDelta0 != 0) ||
        (st.ccDelta0 && memcmp(g_r.batchCc, cc, sizeof(cc)) != 0)) {
        RenderFlush();
        if (!DeviceUsable()) return;
        ApplyState();
        g_r.batchRegs = regs;
        g_r.batchDelta0 = st.ccDelta0 != 0;
        memcpy(g_r.batchCc, cc, sizeof(cc));
        g_r.batchBlend = BlendInShader(st.r);
        g_r.batchState = true;
    }
    const bool flipY = (st.r.fbzMode & SST_YORIGIN) != 0;
    const size_t at = g_r.batch.size();
    g_r.batch.resize(at + size_t(n));
    const FxU32 hints = st.paramHints;
    const bool active0 = TmuActive(st.r.tmu[0].textureMode);
    const bool active1 = g_board.numTmu > 1 && TmuActive(st.r.tmu[1].textureMode);
    const TmuParams p = {active0 && (hints & 0x2), active1 && (hints & 0x8), active1 && ((hints & 0x10) || !active0)};
    for (int i = 0; i < n; i++) Convert(v[i], &g_r.batch[at + i], flipY, p);
    if ((st.r.fbzMode & (SST_ENSTIPPLE | SST_ENSTIPPLEPATTERN)) == SST_ENSTIPPLE) {
        // Rotate mode: where the register is, then each triangle's rows (StippleRows).
        if (g_r.stipWrites != g_board.stippleWrites || g_r.stipPattern != st.r.stipple) {
            g_r.stipWrites = g_board.stippleWrites;
            g_r.stipPattern = st.r.stipple;
            g_r.stipCount = 0;
        }
        for (int i = 0; i + 3 <= n; i += 3) {
            float first, scan;
            StippleRows(v + i, st.r, &first, &scan);
            for (int j = 0; j < 3; j++) g_r.batch[at + i + j].st0[3] = first, g_r.batch[at + i + j].st1[3] = scan;
        }
    }
    if (g_r.batch.size() >= 3 * 4096 || g_r.stipRows.size() >= size_t(4) * 2048 * 256) RenderFlush();
}

}  // namespace

// In pieces of 256 triangles: a batch, and rotate mode's row table, stay bounded however
// many a vertex array has.
void RenderTriangles(const RVtx* v, int n) {
    for (int i = 0; i + 3 <= n; i += 3 * 256) DrawTriangles(v + i, std::min(n - i, 3 * 256) / 3 * 3);
}

// A quad over the target rectangle (pixels), at depth z.
void DrawQuad(const RECT& rc, float z, float u0, float v0, float u1, float v1) {
    struct V {
        float x, y, z, rhw, u, v;
    } q[4] = {{float(rc.left) - 0.5f, float(rc.top) - 0.5f, z, 1, u0, v0},
              {float(rc.right) - 0.5f, float(rc.top) - 0.5f, z, 1, u1, v0},
              {float(rc.left) - 0.5f, float(rc.bottom) - 0.5f, z, 1, u0, v1},
              {float(rc.right) - 0.5f, float(rc.bottom) - 0.5f, z, 1, u1, v1}};
    UseFvf(D3DFVF_XYZRHW | D3DFVF_TEX1);
    BeginDraw();
    if (g_r.inScene) g_r.dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
}

void RenderClear(FxU32 argb, FxU8 alpha, FxU16 depthValue) {
    if (!RenderReady()) return;
    RenderFlush();
    if (!DeviceUsable() || !g_r.clearPs) return;
    Dirty();
    const Regs& r = St().r;
    ApplyTargets(r);
    g_r.batchState = false;  // (the state below is the clear's)
    const bool rgb = (r.fbzMode & SST_RGBWRMASK) != 0;
    const bool depth = (r.fbzMode & (SST_ENDEPTHBUFFER | SST_ZAWRMASK)) == (SST_ENDEPTHBUFFER | SST_ZAWRMASK);
    const bool alphaBuf = (r.fbzMode & (SST_ENALPHABUFFER | SST_ZAWRMASK)) == (SST_ENALPHABUFFER | SST_ZAWRMASK);
    DWORD mask = rgb ? D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE : 0;
    if (alphaBuf) mask |= D3DCOLORWRITEENABLE_ALPHA;
    RenderState(D3DRS_COLORWRITEENABLE, mask);
    RenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
    RenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    RenderState(D3DRS_ZWRITEENABLE, depth);
    const float zero = 0;
    RenderState(D3DRS_DEPTHBIAS, FloatBits(zero));
    RenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    RenderState(D3DRS_ALPHATESTENABLE, FALSE);
    UseShader(g_r.clearPs);
    const float c[4] = {((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
                        alpha / 255.0f};
    Constants(0, c, 1);
    DrawQuad(ClipRect(r), ClearDepth(depthValue));
}

}  // namespace gl2d9
