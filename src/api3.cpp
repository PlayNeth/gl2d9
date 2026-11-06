// The Glide 3 entry points (glide3x.dll). Glide 3 describes its vertices with a layout
// (grVertexLayout: offsets of each parameter in the game's vertex), draws arrays of them
// (strips, fans, lists), takes window or clip coordinates (grCoordinateSpace, grViewport,
// grDepthRange), and counts LODs and aspects in log2 (8 = 256 ... 0 = 1; 3 = 8x1 ... -3 =
// 1x8). Everything is converted here for the core, which speaks Glide 2.
#include "core.h"
#include "gu.h"
#include "lfb.h"
#include "texmem.h"
#include "window.h"

#include <cmath>
#include <cstdarg>
#include <cstdio>

namespace gl2d9 {
GlideVersion g_version = kGlide3x;
}  // namespace gl2d9

using namespace gl2d9;

#define GLAPI extern "C" __attribute__((stdcall))
#define ENTRY(name) \
    Guard guard_;   \
    GL_TRACE("%s", name)

namespace {

// ---------------------------------------------------------------- conversions

inline GrLOD_t Lod2(GrLOD_t lodLog2) { return std::clamp(8 - lodLog2, 0, 8); }
inline GrAspectRatio_t Aspect2(GrAspectRatio_t aspectLog2) { return std::clamp(3 - aspectLog2, 0, 6); }
inline GrTexInfo Info2(const GrTexInfo* in) {
    GrTexInfo o = *in;
    o.smallLod = Lod2(in->smallLod);
    o.largeLod = Lod2(in->largeLod);
    o.aspectRatio = Aspect2(in->aspectRatio);
    return o;
}
// Glide 3 fog modes: on the fog coordinate, on q (Glide 2's table), on z, on iterated alpha.
GrFogMode_t Fog2(GrFogMode_t m) {
    const GrFogMode_t extra = m & (GR_FOG_MULT2 | GR_FOG_ADD2);
    switch (m & 0xFF) {
    case G3_FOG_WITH_TABLE_ON_FOGCOORD_EXT:
    case G3_FOG_WITH_TABLE_ON_Q: return GR_FOG_WITH_TABLE | extra;
    case G3_FOG_WITH_ITERATED_Z: return GR_FOG_WITH_ITERATED_Z | extra;
    case G3_FOG_WITH_ITERATED_ALPHA_EXT: return GR_FOG_WITH_ITERATED_ALPHA | extra;
    default: return GR_FOG_DISABLE;
    }
}

// ---------------------------------------------------------------- the vertex layout

struct Param {
    FxI32 offset;
    FxU32 mode;
};
struct Layout {
    Param xy{0, GR_PARAM_ENABLE}, z{}, w{}, q{}, fog{}, a{}, rgb{}, pargb{}, st0{}, st1{}, q0{}, q1{};
    bool packedColor = false;  // the last color parameter set was PARGB
};

struct Viewport {
    FxI32 x = 0, y = 0, width = 640, height = 480;
    float ox = 320, oy = 240, hwidth = 320, hheight = 240;
    float n = 0, f = 1, hdepth = 0.5f * 65535.f, oz = 0.5f * 65535.f;
};

// What grGlideGetState saves in Glide 3: the registers and these.
struct State3 {
    GlideState s;
    Layout layout;
    Viewport vp;
    FxU32 coordSpace;
};

Layout g_layout;
Viewport g_vp;
FxU32 g_coordSpace = GR_WINDOW_COORDS;
std::vector<RVtx> g_verts;
// The strip or fan a *_CONTINUE draw goes on with: its last vertices.
RVtx g_cont[2];
int g_contCount = 0;
bool g_contOdd = false;

inline float F(const void* v, FxI32 off) {
    float f;
    memcpy(&f, static_cast<const FxU8*>(v) + off, sizeof(f));
    return f;
}
inline bool On(const Param& p) { return p.mode == GR_PARAM_ENABLE; }

// The texture coordinate scale of clip coordinates (0..1 across the texture): 256 along
// the longer side, as the TMU's aspect makes the shorter one.
void TexScale(int tmu, float* s, float* t) {
    const FxU32 lod = St().r.tmu[tmu].tLOD;
    const float shorter = 256.0f / float(1 << ((lod & SST_LOD_ASPECT) >> SST_LOD_ASPECT_SHIFT));
    const bool sWide = (lod & SST_LOD_S_IS_WIDER) != 0;
    *s = sWide ? 256.0f : shorter;
    *t = sWide ? shorter : 256.0f;
}

// What decoding a vertex needs that is the same for every vertex of a draw (taken once per
// call: the texture scales were worked out again for each vertex).
struct ReadCtx {
    bool clip;
    float ss[2] = {1, 1}, ts[2] = {1, 1};
};
ReadCtx MakeReadCtx() {
    ReadCtx c;
    c.clip = g_coordSpace == GR_CLIP_COORDS;
    if (c.clip) {
        TexScale(0, &c.ss[0], &c.ts[0]);
        if (g_board.numTmu > 1) TexScale(1, &c.ss[1], &c.ts[1]);
    }
    return c;
}

RVtx Read(const void* v, const ReadCtx& ctx) {
    const Layout& L = g_layout;
    RVtx o{};
    const bool clip = ctx.clip;
    float oowc = 1.0f;  // clip coordinates: 1/w
    if (clip) {
        const float w = On(L.w) ? F(v, L.w.offset) : 1.0f;
        oowc = w != 0 ? 1.0f / w : 0.0f;
        o.x = F(v, L.xy.offset) * oowc * g_vp.hwidth + g_vp.ox;
        o.y = F(v, L.xy.offset + 4) * oowc * g_vp.hheight + g_vp.oy;
        o.ooz = On(L.z) ? F(v, L.z.offset) * oowc * g_vp.hdepth + g_vp.oz : 0.0f;
        o.oow = On(L.q) ? F(v, L.q.offset) * oowc : oowc;
    } else {
        o.x = F(v, L.xy.offset);
        o.y = F(v, L.xy.offset + 4);
        o.ooz = On(L.z) ? F(v, L.z.offset) : 0.0f;
        o.oow = On(L.q) ? F(v, L.q.offset) : 1.0f;
    }
    if (L.packedColor && On(L.pargb)) {
        FxU32 c;
        memcpy(&c, static_cast<const FxU8*>(v) + L.pargb.offset, sizeof(c));
        o.a = float(c >> 24), o.r = float((c >> 16) & 0xFF), o.g = float((c >> 8) & 0xFF), o.b = float(c & 0xFF);
    } else {
        const float k = clip ? 255.0f : 1.0f;  // clip coordinates: colors 0..1
        if (On(L.rgb)) {
            o.r = F(v, L.rgb.offset) * k;
            o.g = F(v, L.rgb.offset + 4) * k;
            o.b = F(v, L.rgb.offset + 8) * k;
        }
        if (On(L.a)) o.a = F(v, L.a.offset) * k;
    }
    const float* ss = ctx.ss;
    const float* ts = ctx.ts;
    const float k0 = clip ? oowc : 1.0f;
    if (On(L.st0)) {
        o.sow[0] = F(v, L.st0.offset) * k0 * ss[0];
        o.tow[0] = F(v, L.st0.offset + 4) * k0 * ts[0];
    }
    o.oowt[0] = On(L.q0) ? F(v, L.q0.offset) * k0 : o.oow;
    if (On(L.st1)) {
        o.sow[1] = F(v, L.st1.offset) * k0 * ss[1];
        o.tow[1] = F(v, L.st1.offset + 4) * k0 * ts[1];
    } else {
        o.sow[1] = On(L.st0) && clip ? F(v, L.st0.offset) * k0 * ss[1] : o.sow[0];
        o.tow[1] = On(L.st0) && clip ? F(v, L.st0.offset + 4) * k0 * ts[1] : o.tow[0];
    }
    o.oowt[1] = On(L.q1) ? F(v, L.q1.offset) * k0 : o.oowt[0];
    // The fog coordinate takes the FBI's w iterator (3dfx's _grUpdateParamIndex).
    o.fog = On(L.fog) ? F(v, L.fog.offset) : o.oow;
    return o;
}

// The renderer takes each TMU's own w and s, t as read above.
void SetHints() { St().paramHints = 0x2 | 0x8 | 0x10; }

// grEnable(GR_AA_ORDERED): points, lines and GR_TRIANGLES arrays antialiased (3dfx's Glide 3).
bool g_aa = false;

void DrawArray(FxU32 mode, FxU32 count, const void* const* ptrs, const void* base, FxU32 stride) {
    if (!g_board.open || count == 0 || count > (1u << 24)) return;  // (more than any game draws: a count gone wrong)
    SetHints();
    auto at = [&](FxU32 i) -> const void* {
        return ptrs ? ptrs[i] : static_cast<const FxU8*>(base) + size_t(i) * stride;
    };
    const bool cont = mode == GR_TRIANGLE_STRIP_CONTINUE || mode == GR_TRIANGLE_FAN_CONTINUE;
    const int first = cont ? g_contCount : 0;
    g_verts.resize(size_t(first) + count);
    for (int i = 0; i < first; i++) g_verts[size_t(i)] = g_cont[i];
    const ReadCtx ctx = MakeReadCtx();
    for (FxU32 i = 0; i < count; i++) g_verts[first + i] = Read(at(i), ctx);
    RVtx* v = g_verts.data();
    const int n = int(g_verts.size());
    switch (mode) {
    case GR_POINTS:
        for (int i = 0; i < n; i++) g_aa ? CoreAAPoint(v[i]) : CorePoint(v[i]);
        break;
    case GR_LINE_STRIP:
        for (int i = 0; i + 1 < n; i++) g_aa ? CoreAALine(v[i], v[i + 1]) : CoreLine(v[i], v[i + 1]);
        break;
    case GR_LINES:
        for (int i = 0; i + 1 < n; i += 2) g_aa ? CoreAALine(v[i], v[i + 1]) : CoreLine(v[i], v[i + 1]);
        break;
    case GR_POLYGON:
    case GR_TRIANGLE_FAN:
    case GR_TRIANGLE_FAN_CONTINUE:
        CoreFan(v, n);
        if (n >= 2) {
            g_cont[0] = v[0];
            g_cont[1] = v[n - 1];
            g_contCount = 2;
        }
        break;
    case GR_TRIANGLE_STRIP:
    case GR_TRIANGLE_STRIP_CONTINUE: {
        const bool odd = mode == GR_TRIANGLE_STRIP_CONTINUE ? g_contOdd : false;
        CoreStrip(v, n, odd);
        // The parity the next triangle continues with.
        if (n >= 2) {
            g_contOdd = (odd ? 1 : 0) ^ ((n - 2) & 1);
            g_cont[0] = v[n - 2];
            g_cont[1] = v[n - 1];
            g_contCount = 2;
        }
        break;
    }
    case GR_TRIANGLES:
        if (!g_aa) CoreTriangleList(v, n);
        else
            for (int i = 0; i + 2 < n; i += 3) CoreAATriangle(v[i], v[i + 1], v[i + 2], true, true, true);
        break;
    default: break;
    }
}

void ResetLayout() {
    g_layout = Layout{};
    g_contCount = 0;
}

void SetViewport(FxI32 x, FxI32 y, FxI32 w, FxI32 h) {
    g_vp.x = x, g_vp.y = y, g_vp.width = w, g_vp.height = h;
    g_vp.ox = float(x) + w * 0.5f;
    g_vp.oy = float(y) + h * 0.5f;
    g_vp.hwidth = w * 0.5f;
    g_vp.hheight = h * 0.5f;
}

void SetDepthRange(float n, float f) {
    g_vp.n = n, g_vp.f = f;
    g_vp.hdepth = (f - n) * 0.5f * 65535.f;
    g_vp.oz = (f + n) * 0.5f * 65535.f;
}

// A gamma value from a gamma table (entries 0..255 over 0..1): the one its middle follows.
float GammaOf(const FxU32* t, int n) {
    if (!t || n < 4) return 1.0f;
    double sum = 0;
    int k = 0;
    for (int i = n / 4; i < n - n / 8; i++) {
        const double x = double(i) / (n - 1), y = std::clamp(double(t[i] & 0xFF) / 255.0, 1e-4, 0.9999);
        if (x <= 0 || x >= 1) continue;
        sum += std::log(x) / std::log(y);
        k++;
    }
    return k ? float(sum / k) : 1.0f;
}

}  // namespace

// ---------------------------------------------------------------- the board

GLAPI void grGlideInit() try {
    ENTRY("grGlideInit");
    GL_LOG("grGlideInit (Glide 3.x)");
    CoreInit();
} GL_FAIL()
GLAPI void grGlideShutdown() try {
    ENTRY("grGlideShutdown");
    CoreShutdown();
} GL_FAIL()
GLAPI void grSstSelect(int) {}
GLAPI GrContext_t grSstWinOpen(FxU32 hwnd, GrScreenResolution_t res, GrScreenRefresh_t refresh, GrColorFormat_t format,
                               GrOriginLocation_t origin, int nColBuffers, int nAuxBuffers) try {
    ENTRY("grSstWinOpen");
    if (!CoreWinOpen(reinterpret_cast<HWND>(uintptr_t(hwnd)), res, refresh, format, origin, nColBuffers, nAuxBuffers))
        return 0;
    ResetLayout();
    g_aa = false;
    g_coordSpace = GR_WINDOW_COORDS;
    SetViewport(0, 0, g_board.screenW, g_board.screenH);
    SetDepthRange(0, 1);
    return 1;
} GL_FAIL_RETURN(0)
GLAPI FxBool grSstWinClose(GrContext_t) try {
    ENTRY("grSstWinClose");
    CoreWinClose();
    return FXTRUE;
} GL_FAIL_RETURN(FXFALSE)
GLAPI FxBool grSelectContext(GrContext_t ctx) try { return ctx != 0 ? FXTRUE : FXFALSE; } GL_FAIL_RETURN(FXFALSE)
GLAPI void grSetNumPendingBuffers(FxI32) {}
GLAPI void grSstOrigin(GrOriginLocation_t o) try {
    ENTRY("grSstOrigin");
    st::SstOrigin(o);
} GL_FAIL()
GLAPI void grSstVidMode(FxU32, void*) {}
GLAPI void grSstConfigPipeline(GrChipID_t, FxU32, FxU32) {}
GLAPI void grSplash(float, float, float, float, FxU32) {}
GLAPI void grErrorSetCallback(GrErrorCallbackFnc_t fn) try {
    ENTRY("grErrorSetCallback");
    g_board.errorCallback = fn;
} GL_FAIL()
GLAPI void grFinish() try {
    ENTRY("grFinish");
    RenderFlush();
} GL_FAIL()
GLAPI void grFlush() try {
    ENTRY("grFlush");
    RenderFlush();
} GL_FAIL()
GLAPI FxI32 grQueryResolutions(const GrResolution* tmpl, GrResolution* out) try {
    if (!tmpl) return 0;
    const FxI32 any = FxI32(0xFFFFFFFF);
    FxI32 size = 0;
    for (int r = GR_RESOLUTION_320x200; r <= GR_RESOLUTION_1600x1200; r++) {
        if (tmpl->resolution != any && tmpl->resolution != r) continue;
        // Only what the monitor has (a game listing them offers no others); 640x480 always,
        // as every Glide game can run in it.
        int w, h;
        if (!ResolutionSize(r, &w, &h) || (r != GR_RESOLUTION_640x480 && !MonitorHasMode(w, h))) continue;
        if (tmpl->refresh != any && tmpl->refresh != GR_REFRESH_60Hz) continue;
        for (int c = 2; c <= 3; c++) {
            if (tmpl->numColorBuffers != any && tmpl->numColorBuffers != c) continue;
            for (int a = 0; a <= 1; a++) {
                if (tmpl->numAuxBuffers != any && tmpl->numAuxBuffers != a) continue;
                size += FxI32(sizeof(GrResolution));
                if (out) *out++ = GrResolution{r, GR_REFRESH_60Hz, c, a};
            }
        }
    }
    return size;
} GL_FAIL_RETURN(0)
GLAPI FxBool grReset(FxU32 what) try {
    ENTRY("grReset");
    switch (what) {
    case GR_STATS_POINTS: g_board.pointsDrawn = 0; return FXTRUE;
    case GR_STATS_LINES: g_board.linesDrawn = 0; return FXTRUE;
    case GR_STATS_TRIANGLES: g_board.trisIn = g_board.trisOut = 0; return FXTRUE;
    case GR_STATS_PIXELS: return FXTRUE;
    case GR_VERTEX_PARAMETER: ResetLayout(); return FXTRUE;
    default: return FXFALSE;
    }
} GL_FAIL_RETURN(FXFALSE)
GLAPI GrProc grGetProcAddress(char*) try { return nullptr; } GL_FAIL_RETURN(nullptr)
GLAPI const char* grGetString(FxU32 what) try {
    switch (what) {
    case GR_EXTENSION: return "FOGCOORD";
    case GR_HARDWARE: return "Voodoo2";
    case GR_RENDERER: return "Glide";
    case GR_VENDOR: return "3Dfx Interactive";
    case GR_VERSION: return "3.03";
    default: return nullptr;
    }
} GL_FAIL_RETURN(nullptr)
GLAPI FxU32 grGet(FxU32 what, FxU32 len, FxI32* p) try {
    ENTRY("grGet");
    if (!p) return 0;
    if (g_texMem[0].bytes.empty()) CoreInit();
    auto one = [&](FxI32 v) -> FxU32 {
        if (len != 4) return 0;
        *p = v;
        return 4;
    };
    auto two = [&](FxI32 a, FxI32 b) -> FxU32 {
        if (len != 8) return 0;
        p[0] = a, p[1] = b;
        return 8;
    };
    switch (what) {
    case GR_BITS_DEPTH: return one(16);
    case GR_BITS_RGBA:
        if (len != 16) return 0;
        p[0] = 5, p[1] = 6, p[2] = 5, p[3] = 0;
        return 16;
    case GR_BITS_GAMMA: return one(8);
    case GR_FIFO_FULLNESS: return two(0, 0);
    case GR_FOG_TABLE_ENTRIES: return one(GR_FOG_TABLE_SIZE);
    case GR_GAMMA_TABLE_ENTRIES: return one(32);
    case GR_GLIDE_STATE_SIZE: return one(FxI32(sizeof(State3)));
    case GR_GLIDE_VERTEXLAYOUT_SIZE: return one(FxI32(sizeof(Layout)));
    case GR_IS_BUSY: return one(FXFALSE);
    case GR_LFB_PIXEL_PIPE: return one(FXTRUE);
    case GR_MAX_TEXTURE_SIZE: return one(256);
    case GR_MAX_TEXTURE_ASPECT_RATIO: return one(3);
    case GR_MEMORY_FB: return one(std::clamp(g_config.fbMB, 1, 16) << 20);
    case GR_MEMORY_TMU: return one(FxI32(g_board.tmuMem));
    case GR_MEMORY_UMA: return one(0);
    case GR_NUM_BOARDS: return one(1);
    case GR_NON_POWER_OF_TWO_TEXTURES: return one(FXFALSE);
    case GR_NUM_FB: return one(1);
    case GR_NUM_TMU: return one(g_board.numTmu);
    case GR_PENDING_BUFFERSWAPS: return one(0);
    case GR_REVISION_FB: return one(2 + (g_board.numTmu == 1 ? 0x80 : 0x100));
    case GR_REVISION_TMU: return one(4);
    case GR_STATS_LINES: return one(FxI32(g_board.linesDrawn));
    case GR_STATS_POINTS: return one(FxI32(g_board.pointsDrawn));
    case GR_STATS_TRIANGLES_IN: return one(FxI32(g_board.trisIn));
    case GR_STATS_TRIANGLES_OUT: return one(FxI32(g_board.trisOut));
    case GR_STATS_PIXELS_AFUNC_FAIL:
    case GR_STATS_PIXELS_CHROMA_FAIL:
    case GR_STATS_PIXELS_DEPTHFUNC_FAIL:
    case GR_STATS_PIXELS_IN:
    case GR_STATS_PIXELS_OUT: return one(0);
    case GR_NUM_SWAP_HISTORY_BUFFER: return one(8);
    case GR_SWAP_HISTORY:
        if (len != 32) return 0;
        for (int i = 0; i < 8; i++) p[i] = 0;
        return 32;
    case GR_SUPPORTS_PASSTHRU: return one(FXTRUE);
    case GR_TEXTURE_ALIGN: return one(8);
    case GR_VIDEO_POSITION: return two(0, 0);
    case GR_VIEWPORT:
        if (len != 16) return 0;
        p[0] = g_vp.x, p[1] = g_vp.y, p[2] = g_vp.width, p[3] = g_vp.height;
        return 16;
    case GR_WDEPTH_MIN_MAX: return two(GR_WDEPTHVALUE_NEAREST, GR_WDEPTHVALUE_FARTHEST);
    case GR_ZDEPTH_MIN_MAX: return two(GR_ZDEPTHVALUE_NEAREST, GR_ZDEPTHVALUE_FARTHEST);
    default: return 0;
    }
} GL_FAIL_RETURN(0)
GLAPI void grEnable(GrEnableMode_t mode) try {
    ENTRY("grEnable");
    if (mode == GR_ALLOW_MIPMAP_DITHER) st::Hints(GR_HINT_ALLOW_MIPMAP_DITHER, 1);
    else if (mode == GR_AA_ORDERED) g_aa = true;
} GL_FAIL()
GLAPI void grDisable(GrEnableMode_t mode) try {
    ENTRY("grDisable");
    if (mode == GR_ALLOW_MIPMAP_DITHER) st::Hints(GR_HINT_ALLOW_MIPMAP_DITHER, 0);
    else if (mode == GR_AA_ORDERED) g_aa = false;
} GL_FAIL()
GLAPI void grCoordinateSpace(GrCoordinateSpaceMode_t mode) try {
    ENTRY("grCoordinateSpace");
    g_coordSpace = mode == GR_CLIP_COORDS ? GR_CLIP_COORDS : GR_WINDOW_COORDS;
} GL_FAIL()
GLAPI void grViewport(FxI32 x, FxI32 y, FxI32 w, FxI32 h) try {
    ENTRY("grViewport");
    SetViewport(x, y, w, h);
} GL_FAIL()
GLAPI void grDepthRange(float n, float f) try {
    ENTRY("grDepthRange");
    SetDepthRange(n, f);
} GL_FAIL()
GLAPI void grGlideGetState(void* state) try {
    ENTRY("grGlideGetState");
    if (!state) return;
    State3 s{St(), g_layout, g_vp, g_coordSpace};
    memcpy(state, &s, sizeof(s));
} GL_FAIL()
GLAPI void grGlideSetState(const void* state) try {
    ENTRY("grGlideSetState");
    if (!state) return;
    State3 s;
    memcpy(&s, state, sizeof(s));
    St() = s.s;
    g_layout = s.layout;
    g_vp = s.vp;
    g_coordSpace = s.coordSpace;
} GL_FAIL()
GLAPI void grGlideGetVertexLayout(void* layout) try {
    if (layout) memcpy(layout, &g_layout, sizeof(g_layout));
} GL_FAIL()
GLAPI void grGlideSetVertexLayout(const void* layout) try {
    if (layout) memcpy(&g_layout, layout, sizeof(g_layout));
} GL_FAIL()
GLAPI void grVertexLayout(FxU32 param, FxI32 offset, FxU32 mode) try {
    ENTRY("grVertexLayout");
    Param* p = nullptr;
    switch (param) {
    case GR_PARAM_XY: p = &g_layout.xy; break;
    case GR_PARAM_Z: p = &g_layout.z; break;
    case GR_PARAM_W: p = &g_layout.w; break;
    case GR_PARAM_Q: p = &g_layout.q; break;
    case GR_PARAM_FOG_EXT: p = &g_layout.fog; break;
    case GR_PARAM_A: p = &g_layout.a; break;
    case GR_PARAM_RGB: p = &g_layout.rgb; break;
    case GR_PARAM_PARGB: p = &g_layout.pargb; break;
    case GR_PARAM_ST0: p = &g_layout.st0; break;
    case GR_PARAM_ST1: p = &g_layout.st1; break;
    case GR_PARAM_Q0: p = &g_layout.q0; break;
    case GR_PARAM_Q1: p = &g_layout.q1; break;
    default: return;
    }
    p->offset = offset;
    p->mode = mode;
    // (Glide: the color type follows the last color parameter enabled.)
    if (mode == GR_PARAM_ENABLE) {
        if (param == GR_PARAM_PARGB) g_layout.packedColor = true;
        else if (param == GR_PARAM_RGB || param == GR_PARAM_A) g_layout.packedColor = false;
    }
} GL_FAIL()

// ---------------------------------------------------------------- drawing

GLAPI void grDrawPoint(const void* p) try {
    ENTRY("grDrawPoint");
    if (!p) return;
    SetHints();
    const ReadCtx ctx = MakeReadCtx();
    if (g_aa) CoreAAPoint(Read(p, ctx));
    else CorePoint(Read(p, ctx));
} GL_FAIL()
GLAPI void grDrawLine(const void* a, const void* b) try {
    ENTRY("grDrawLine");
    if (!a || !b) return;
    SetHints();
    const ReadCtx ctx = MakeReadCtx();
    if (g_aa) CoreAALine(Read(a, ctx), Read(b, ctx));
    else CoreLine(Read(a, ctx), Read(b, ctx));
} GL_FAIL()
GLAPI void grDrawTriangle(const void* a, const void* b, const void* c) try {
    ENTRY("grDrawTriangle");
    if (!a || !b || !c) return;
    SetHints();
    const ReadCtx ctx = MakeReadCtx();
    CoreTriangle(Read(a, ctx), Read(b, ctx), Read(c, ctx));
} GL_FAIL()
GLAPI void grAADrawTriangle(const void* a, const void* b, const void* c, FxBool ab, FxBool bc, FxBool ca) try {
    ENTRY("grAADrawTriangle");
    if (!a || !b || !c) return;
    SetHints();
    const ReadCtx ctx = MakeReadCtx();
    CoreAATriangle(Read(a, ctx), Read(b, ctx), Read(c, ctx), ab != FXFALSE, bc != FXFALSE, ca != FXFALSE);
} GL_FAIL()
GLAPI void grDrawVertexArray(FxU32 mode, FxU32 count, void* ptrs) try {
    ENTRY("grDrawVertexArray");
    if (ptrs) DrawArray(mode, count, static_cast<const void* const*>(ptrs), nullptr, 0);
} GL_FAIL()
GLAPI void grDrawVertexArrayContiguous(FxU32 mode, FxU32 count, void* verts, FxU32 stride) try {
    ENTRY("grDrawVertexArrayContiguous");
    if (verts) DrawArray(mode, count, nullptr, verts, stride);
} GL_FAIL()

// ---------------------------------------------------------------- buffers

GLAPI void grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth) try {
    ENTRY("grBufferClear");
    CoreClear(color, alpha, depth);
} GL_FAIL()
GLAPI void grBufferSwap(FxU32 interval) try {
    ENTRY("grBufferSwap");
    CoreSwap(int(interval));
} GL_FAIL()
GLAPI FxI32 grBufferNumPending() try { return 0; } GL_FAIL_RETURN(0)
GLAPI void grRenderBuffer(GrBuffer_t b) try {
    ENTRY("grRenderBuffer");
    st::RenderBuffer(b == GR_BUFFER_TRIPLEBUFFER ? GR_BUFFER_BACKBUFFER : b);
} GL_FAIL()
GLAPI void grCheckForRoom(FxI32) {}

// ---------------------------------------------------------------- state

GLAPI void grAlphaBlendFunction(GrAlphaBlendFnc_t rs, GrAlphaBlendFnc_t rd, GrAlphaBlendFnc_t as,
                                GrAlphaBlendFnc_t ad) try {
    ENTRY("grAlphaBlendFunction");
    st::AlphaBlendFunction(rs, rd, as, ad);
} GL_FAIL()
GLAPI void grAlphaCombine(GrCombineFunction_t f, GrCombineFactor_t k, GrCombineLocal_t l, GrCombineOther_t o,
                          FxBool inv) try {
    ENTRY("grAlphaCombine");
    st::AlphaCombine(f, k, l, o, inv);
} GL_FAIL()
GLAPI void grAlphaControlsITRGBLighting(FxBool e) try {
    ENTRY("grAlphaControlsITRGBLighting");
    st::AlphaControlsITRGBLighting(e);
} GL_FAIL()
GLAPI void grAlphaTestFunction(GrCmpFnc_t f) try {
    ENTRY("grAlphaTestFunction");
    st::AlphaTestFunction(f);
} GL_FAIL()
GLAPI void grAlphaTestReferenceValue(GrAlpha_t v) try {
    ENTRY("grAlphaTestReferenceValue");
    st::AlphaTestReferenceValue(v);
} GL_FAIL()
GLAPI void grChromakeyMode(GrChromakeyMode_t m) try {
    ENTRY("grChromakeyMode");
    st::ChromakeyMode(m);
} GL_FAIL()
GLAPI void grChromakeyValue(GrColor_t c) try {
    ENTRY("grChromakeyValue");
    st::ChromakeyValue(c);
} GL_FAIL()
GLAPI void grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy) try {
    ENTRY("grClipWindow");
    st::ClipWindow(minx, miny, maxx, maxy);
} GL_FAIL()
GLAPI void grColorCombine(GrCombineFunction_t f, GrCombineFactor_t k, GrCombineLocal_t l, GrCombineOther_t o,
                          FxBool inv) try {
    ENTRY("grColorCombine");
    st::ColorCombine(f, k, l, o, inv);
} GL_FAIL()
GLAPI void grColorMask(FxBool rgb, FxBool a) try {
    ENTRY("grColorMask");
    st::ColorMask(rgb, a);
} GL_FAIL()
GLAPI void grConstantColorValue(GrColor_t c) try {
    ENTRY("grConstantColorValue");
    st::ConstantColorValue(c);
} GL_FAIL()
GLAPI void grCullMode(GrCullMode_t m) try {
    ENTRY("grCullMode");
    st::CullMode(m);
} GL_FAIL()
GLAPI void grDepthBiasLevel(FxI32 level) try {
    ENTRY("grDepthBiasLevel");
    st::DepthBiasLevel(FxI16(level));
} GL_FAIL()
GLAPI void grDepthBufferFunction(GrCmpFnc_t f) try {
    ENTRY("grDepthBufferFunction");
    st::DepthBufferFunction(f);
} GL_FAIL()
GLAPI void grDepthBufferMode(GrDepthBufferMode_t m) try {
    ENTRY("grDepthBufferMode");
    st::DepthBufferMode(m);
} GL_FAIL()
GLAPI void grDepthMask(FxBool e) try {
    ENTRY("grDepthMask");
    st::DepthMask(e);
} GL_FAIL()
GLAPI void grDisableAllEffects() try {
    ENTRY("grDisableAllEffects");
    st::DisableAllEffects();
} GL_FAIL()
GLAPI void grDitherMode(GrDitherMode_t m) try {
    ENTRY("grDitherMode");
    st::DitherMode(m);
} GL_FAIL()
GLAPI void grFogColorValue(GrColor_t c) try {
    ENTRY("grFogColorValue");
    st::FogColorValue(c);
} GL_FAIL()
GLAPI void grFogMode(GrFogMode_t m) try {
    ENTRY("grFogMode");
    st::FogMode(Fog2(m));
} GL_FAIL()
GLAPI void grFogTable(const GrFog_t* t) try {
    ENTRY("grFogTable");
    if (t) st::FogTable(t);
} GL_FAIL()
GLAPI void grStippleMode(FxI32 mode) try {
    ENTRY("grStippleMode");
    st::StippleMode(mode);
} GL_FAIL()
GLAPI void grStipplePattern(FxU32 pattern) try {
    ENTRY("grStipplePattern");
    st::StipplePattern(pattern);
} GL_FAIL()
GLAPI void grLfbConstantAlpha(GrAlpha_t a) try {
    ENTRY("grLfbConstantAlpha");
    st::LfbConstantAlpha(a);
} GL_FAIL()
GLAPI void grLfbConstantDepth(FxU32 d) try {
    ENTRY("grLfbConstantDepth");
    st::LfbConstantDepth(d);
} GL_FAIL()
GLAPI void grGammaCorrectionValue(float v) try {
    ENTRY("grGammaCorrectionValue");
    RenderSetGamma(v);
} GL_FAIL()
GLAPI void guGammaCorrectionRGB(float r, float g, float b) try {
    ENTRY("guGammaCorrectionRGB");
    RenderSetGamma((r + g + b) / 3.0f);
} GL_FAIL()
GLAPI void grLoadGammaTable(FxU32 n, FxU32* r, FxU32* g, FxU32* b) try {
    ENTRY("grLoadGammaTable");
    if (!r || !g || !b) return;
    RenderSetGamma((GammaOf(r, int(n)) + GammaOf(g, int(n)) + GammaOf(b, int(n))) / 3.0f);
} GL_FAIL()

// ---------------------------------------------------------------- textures

GLAPI void grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s, GrTextureClampMode_t t) try {
    ENTRY("grTexClampMode");
    st::TexClampMode(tmu, s, t);
} GL_FAIL()
GLAPI void grTexCombine(GrChipID_t tmu, GrCombineFunction_t rf, GrCombineFactor_t rk, GrCombineFunction_t af,
                        GrCombineFactor_t ak, FxBool ri, FxBool ai) try {
    ENTRY("grTexCombine");
    st::TexCombine(tmu, rf, rk, af, ak, ri, ai);
} GL_FAIL()
GLAPI void grTexDetailControl(GrChipID_t tmu, int bias, FxU8 scale, float max) try {
    ENTRY("grTexDetailControl");
    st::TexDetailControl(tmu, bias, scale, max);
} GL_FAIL()
GLAPI void grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t mi, GrTextureFilterMode_t ma) try {
    ENTRY("grTexFilterMode");
    st::TexFilterMode(tmu, mi, ma);
} GL_FAIL()
GLAPI void grTexLodBiasValue(GrChipID_t tmu, float bias) try {
    ENTRY("grTexLodBiasValue");
    st::TexLodBiasValue(tmu, bias);
} GL_FAIL()
GLAPI void grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t m, FxBool blend) try {
    ENTRY("grTexMipMapMode");
    st::TexMipMapMode(tmu, m, blend);
} GL_FAIL()
GLAPI void grTexNCCTable(GrNCCTable_t t) try {
    ENTRY("grTexNCCTable");
    for (int i = 0; i < g_board.numTmu; i++) st::TexNCCTable(i, t);
} GL_FAIL()
GLAPI void grTexSource(GrChipID_t tmu, FxU32 start, FxU32 evenOdd, const GrTexInfo* info) try {
    ENTRY("grTexSource");
    if (!info) return;
    const GrTexInfo i2 = Info2(info);
    st::TexSource(tmu, start, evenOdd, &i2);
} GL_FAIL()
GLAPI void grTexMultibase(GrChipID_t tmu, FxBool e) try {
    ENTRY("grTexMultibase");
    st::TexMultibase(tmu, e);
} GL_FAIL()
GLAPI void grTexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 start, FxU32 evenOdd,
                                 const GrTexInfo* info) try {
    ENTRY("grTexMultibaseAddress");
    if (!info || range > G3_TEXBASE_256) return;
    const GrTexInfo i2 = Info2(info);
    st::TexMultibaseAddress(tmu, G3_TEXBASE_256 - range, start, evenOdd, &i2);
} GL_FAIL()
GLAPI FxU32 grTexMinAddress(GrChipID_t) try { return 0; } GL_FAIL_RETURN(0)
GLAPI FxU32 grTexMaxAddress(GrChipID_t tmu) try {
    ENTRY("grTexMaxAddress");
    return CoreTexMaxAddress(tmu);
} GL_FAIL_RETURN(0)
GLAPI FxU32 grTexCalcMemRequired(GrLOD_t small, GrLOD_t large, GrAspectRatio_t aspect, GrTextureFormat_t fmt) try {
    return TexMemRequired(Lod2(small), Lod2(large), Aspect2(aspect), fmt, GR_MIPMAPLEVELMASK_BOTH);
} GL_FAIL_RETURN(0)
GLAPI FxU32 grTexTextureMemRequired(FxU32 evenOdd, const GrTexInfo* info) try {
    if (!info) return 0;
    return TexMemRequired(Lod2(info->smallLod), Lod2(info->largeLod), Aspect2(info->aspectRatio), info->format,
                          evenOdd);
} GL_FAIL_RETURN(0)
GLAPI void grTexDownloadMipMap(GrChipID_t tmu, FxU32 start, FxU32 evenOdd, const GrTexInfo* info) try {
    ENTRY("grTexDownloadMipMap");
    if (!info) return;
    const GrTexInfo i2 = Info2(info);
    CoreTexDownloadMipMap(tmu, start, evenOdd, &i2);
}
GL_FAIL()
GLAPI FxBool grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 start, GrLOD_t thisLod, GrLOD_t largeLod,
                                      GrAspectRatio_t aspect, GrTextureFormat_t fmt, FxU32 evenOdd,
                                      const void* data) try {
    ENTRY("grTexDownloadMipMapLevel");
    int w, h;
    LevelSize(Lod2(thisLod), Aspect2(aspect), &w, &h);
    CoreTexDownloadLevel(tmu, start, Lod2(thisLod), Lod2(largeLod), Aspect2(aspect), fmt, evenOdd, data, 0, h - 1);
    return FXTRUE;
}
GL_FAIL_RETURN(FXFALSE)
GLAPI FxBool grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 start, GrLOD_t thisLod, GrLOD_t largeLod,
                                             GrAspectRatio_t aspect, GrTextureFormat_t fmt, FxU32 evenOdd,
                                             const void* data, int t, int maxT) try {
    ENTRY("grTexDownloadMipMapLevelPartial");
    CoreTexDownloadLevel(tmu, start, Lod2(thisLod), Lod2(largeLod), Aspect2(aspect), fmt, evenOdd, data, t, maxT);
    return FXTRUE;
} GL_FAIL_RETURN(FXFALSE)
// Glide 3 has one table for the board (the download reaches every TMU).
GLAPI void grTexDownloadTable(GrTexTable_t type, const void* data) try {
    ENTRY("grTexDownloadTable");
    const bool pal = type == GR_TEXTABLE_PALETTE || type == GR_TEXTABLE_PALETTE_6666_EXT;
    CoreTexDownloadTable(GR_TMU0, type, data, 0, pal ? 255 : 11);
} GL_FAIL()
GLAPI void grTexDownloadTablePartial(GrTexTable_t type, const void* data, int start, int end) try {
    ENTRY("grTexDownloadTablePartial");
    CoreTexDownloadTable(GR_TMU0, type, data, start, end);
} GL_FAIL()

// ---------------------------------------------------------------- the linear frame buffer

GLAPI FxBool grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t mode, GrOriginLocation_t origin,
                       FxBool pipeline, GrLfbInfo_t* info) try {
    ENTRY("grLfbLock");
    return LfbLock(type, buffer, mode, origin, pipeline, info);
} GL_FAIL_RETURN(FXFALSE)
GLAPI FxBool grLfbUnlock(GrLock_t type, GrBuffer_t buffer) try {
    ENTRY("grLfbUnlock");
    return LfbUnlock(type, buffer);
} GL_FAIL_RETURN(FXFALSE)
GLAPI FxBool grLfbReadRegion(GrBuffer_t src, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 stride, void* dst) try {
    ENTRY("grLfbReadRegion");
    return LfbReadRegion(src, x, y, w, h, stride, dst);
} GL_FAIL_RETURN(FXFALSE)
GLAPI FxBool grLfbWriteRegion(GrBuffer_t dst, FxU32 x, FxU32 y, GrLfbSrcFmt_t fmt, FxU32 w, FxU32 h, FxBool pipeline,
                              FxI32 stride, const void* data) try {
    ENTRY("grLfbWriteRegion");
    return LfbWriteRegion(dst, x, y, fmt, w, h, pipeline, stride, data);
} GL_FAIL_RETURN(FXFALSE)
GLAPI void grLfbWriteColorFormat(GrColorFormat_t f) try {
    ENTRY("grLfbWriteColorFormat");
    LfbWriteColorFormat(f);
} GL_FAIL()
GLAPI void grLfbWriteColorSwizzle(FxBool, FxBool) {}

// ---------------------------------------------------------------- statistics

GLAPI void grTriStats(FxU32* in, FxU32* out) try {
    if (in) *in = g_board.trisIn;
    if (out) *out = g_board.trisOut;
} GL_FAIL()
GLAPI void grResetTriStats() try { g_board.trisIn = g_board.trisOut = 0; } GL_FAIL()

// ---------------------------------------------------------------- gu

// .3df files: Glide 3's header holds log2 LODs and aspect.
GLAPI FxBool gu3dfGetInfo(const char* name, Gu3dfInfo* info) try {
    if (!Gu3dfGetInfo(name, info)) return FXFALSE;
    info->header.small_lod = 8 - info->header.small_lod;
    info->header.large_lod = 8 - info->header.large_lod;
    info->header.aspect_ratio = 3 - info->header.aspect_ratio;
    return FXTRUE;
} GL_FAIL_RETURN(FXFALSE)
GLAPI FxBool gu3dfLoad(const char* name, Gu3dfInfo* info) try {
    if (!info) return FXFALSE;
    Gu3dfInfo i2 = *info;
    i2.header.small_lod = Lod2(info->header.small_lod);
    i2.header.large_lod = Lod2(info->header.large_lod);
    i2.header.aspect_ratio = Aspect2(info->header.aspect_ratio);
    if (!Gu3dfLoad(name, &i2)) return FXFALSE;
    info->table = i2.table;
    return FXTRUE;
} GL_FAIL_RETURN(FXFALSE)
GLAPI float guFogTableIndexToW(int i) try { return GuFogTableIndexToW(i); } GL_FAIL_RETURN(0.0f)
GLAPI void guFogGenerateExp(GrFog_t* t, float d) try { GuFogGenerateExp(t, d); } GL_FAIL()
GLAPI void guFogGenerateExp2(GrFog_t* t, float d) try { GuFogGenerateExp2(t, d); } GL_FAIL()
GLAPI void guFogGenerateLinear(GrFog_t* t, float n, float f) try { GuFogGenerateLinear(t, n, f); } GL_FAIL()

// ---------------------------------------------------------------- 3dfx's debug library (exported by Glide 3)

static GrErrorCallbackFnc_t g_gdbgCallback;
GLAPI void gdbg_init() {}
GLAPI void gdbg_shutdown() {}
GLAPI const char* gdbg_parse(const char*) try { return nullptr; } GL_FAIL_RETURN(nullptr)
GLAPI FxBool gdbg_set_file(const char*) try { return FXTRUE; } GL_FAIL_RETURN(FXFALSE)
GLAPI int gdbg_set_debuglevel(int, int) try { return 0; } GL_FAIL_RETURN(0)
GLAPI int gdbg_get_debuglevel(int) try { return 0; } GL_FAIL_RETURN(0)
GLAPI int gdbg_get_errors() try { return 0; } GL_FAIL_RETURN(0)
GLAPI void gdbg_set_keepalive(FxBool) {}
GLAPI void gdbg_error_set_callback(GrErrorCallbackFnc_t fn) try { g_gdbgCallback = fn; } GL_FAIL()
GLAPI void gdbg_error_clear_callback(GrErrorCallbackFnc_t) try { g_gdbgCallback = nullptr; } GL_FAIL()
GLAPI void gdbg_vprintf(const char* fmt, va_list args) try {
    if (!fmt || LogLevel() < 2) return;
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, args);
    GL_TRACE("gdbg: %s", buf);
} GL_FAIL()
extern "C" int gdbg_printf(const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    gdbg_vprintf(fmt, a);
    va_end(a);
    return 1;
}
extern "C" int gdbg_info(int, const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    gdbg_vprintf(fmt, a);
    va_end(a);
    return 1;
}
extern "C" int gdbg_info_more(int, const char* fmt, ...) {
    va_list a;
    va_start(a, fmt);
    gdbg_vprintf(fmt, a);
    va_end(a);
    return 1;
}
extern "C" void gdbg_error(const char* kind, const char* fmt, ...) {
    char buf[512];
    va_list a;
    va_start(a, fmt);
    vsnprintf(buf, sizeof(buf), fmt ? fmt : "", a);
    va_end(a);
    GL_LOG("gdbg error (%s): %s", kind ? kind : "", buf);
}
