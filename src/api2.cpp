// The Glide 2 entry points: glide2x.dll (Glide 2.x, Voodoo 2) and, built with GLIDE21,
// glide.dll (Glide 2.1, Voodoo Graphics, with its older screen and frame buffer calls). The
// .def files pick what each exports. Every entry point takes the lock and converts its
// arguments for the core.
#include "core.h"
#include "gu.h"
#include "lfb.h"
#include "texmem.h"
#include "window.h"

#include <cmath>

namespace gl2d9 {
#ifdef GLIDE21
GlideVersion g_version = kGlide21;
#else
GlideVersion g_version = kGlide2x;
#endif
}  // namespace gl2d9

using namespace gl2d9;

#define GLAPI extern "C" __attribute__((stdcall))
#define ENTRY(name) \
    Guard guard_;   \
    GL_TRACE("%s", name)

namespace {

inline RVtx V(const GrVertex* g) {
    RVtx v;
    v.x = g->x;
    v.y = g->y;
    v.ooz = g->ooz;
    v.oow = g->oow;
    v.fog = g->oow;
    v.r = g->r;
    v.g = g->g;
    v.b = g->b;
    v.a = g->a;
    for (int t = 0; t < 2; t++) {
        v.sow[t] = g->tmuvtx[t].sow;
        v.tow[t] = g->tmuvtx[t].tow;
        v.oowt[t] = g->tmuvtx[t].oow;
    }
    return v;
}

std::vector<RVtx> g_poly;

// log=2: a draw's first vertex, with every TMU's coordinates.
void TraceVertex(int n, const GrVertex* v) {
    if (LogLevel() < 2 || !v) return;
    GL_TRACE("  %d verts; v0 xy %.1f,%.1f oow %g a %.0f | tmu0 %g,%g,%g | tmu1 %g,%g,%g", n, v->x, v->y, v->oow, v->a,
             v->tmuvtx[0].sow, v->tmuvtx[0].tow, v->tmuvtx[0].oow, v->tmuvtx[1].sow, v->tmuvtx[1].tow,
             v->tmuvtx[1].oow);
}

void Polygon(int n, const int* idx, const GrVertex* list, bool aa = false) {
    if (n < 3 || !list) return;
    g_poly.resize(size_t(n));
    for (int i = 0; i < n; i++) g_poly[i] = V(&list[idx ? idx[i] : i]);
    if (aa) CoreAAFan(g_poly.data(), n);
    else CoreFan(g_poly.data(), n);
}

GrSstType BoardType() { return g_version == kGlide21 ? GR_SSTTYPE_VOODOO : GR_SSTTYPE_Voodoo2; }

// The video timing grSstVRetraceOn and grSstVideoLine answer from: the refresh rate the
// screen was opened with (60 Hz without one), the retrace the last 1/20 of a frame.
double FramePhase() {
    static const int kRates[] = {60, 70, 72, 75, 80, 90, 100, 85, 120};
    const int hz = g_board.refresh >= 0 && g_board.refresh < 9 ? kRates[g_board.refresh] : 60;
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    const double t = double(c.QuadPart) / double(f.QuadPart) * hz;
    return t - std::floor(t);
}

#ifdef GLIDE21
// Glide 2.1's frame buffer calls (grLfbGetWritePtr...), on the locks.
GrLfbWriteMode_t g_lfb21Mode = GR_LFBWRITEMODE_565;
GrOriginLocation_t g_lfb21Origin = GR_ORIGIN_UPPER_LEFT;
bool g_lfb21Bypass = false;
int g_lfb21Read = -1, g_lfb21Write = -1;
#endif

}  // namespace

// ---------------------------------------------------------------- drawing

GLAPI void grDrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    ENTRY("grDrawTriangle");
    TraceVertex(3, a);
    if (a && b && c) CoreTriangle(V(a), V(b), V(c));
}
GLAPI void grDrawPoint(const GrVertex* p) {
    ENTRY("grDrawPoint");
    if (p) CorePoint(V(p));
}
GLAPI void grDrawLine(const GrVertex* a, const GrVertex* b) {
    ENTRY("grDrawLine");
    if (a && b) CoreLine(V(a), V(b));
}
GLAPI void grDrawPolygon(int n, const int* idx, const GrVertex* list) {
    ENTRY("grDrawPolygon");
    if (idx) Polygon(n, idx, list);
}
GLAPI void grDrawPolygonVertexList(int n, const GrVertex* list) {
    ENTRY("grDrawPolygonVertexList");
    TraceVertex(n, list);
    Polygon(n, nullptr, list);
}
GLAPI void grDrawPlanarPolygon(int n, const int* idx, const GrVertex* list) {
    ENTRY("grDrawPlanarPolygon");
    if (idx) Polygon(n, idx, list);
}
GLAPI void grDrawPlanarPolygonVertexList(int n, const GrVertex* list) {
    ENTRY("grDrawPlanarPolygonVertexList");
    TraceVertex(n, list);
    Polygon(n, nullptr, list);
}
// Antialiased primitives: with alpha-blended fringes on the edges, as 3dfx's Glide draws them.
GLAPI void grAADrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c, FxBool ab, FxBool bc, FxBool ca) {
    ENTRY("grAADrawTriangle");
    if (a && b && c) CoreAATriangle(V(a), V(b), V(c), ab != FXFALSE, bc != FXFALSE, ca != FXFALSE);
}
GLAPI void grAADrawPoint(const GrVertex* p) {
    ENTRY("grAADrawPoint");
    if (p) CoreAAPoint(V(p));
}
GLAPI void grAADrawLine(const GrVertex* a, const GrVertex* b) {
    ENTRY("grAADrawLine");
    if (a && b) CoreAALine(V(a), V(b));
}
GLAPI void grAADrawPolygon(int n, const int* idx, const GrVertex* list) {
    ENTRY("grAADrawPolygon");
    if (idx) Polygon(n, idx, list, true);
}
GLAPI void grAADrawPolygonVertexList(int n, const GrVertex* list) {
    ENTRY("grAADrawPolygonVertexList");
    Polygon(n, nullptr, list, true);
}
// The clip window is the hardware's here (scissoring): the gu clippers draw directly.
GLAPI void guDrawTriangleWithClip(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    ENTRY("guDrawTriangleWithClip");
    if (a && b && c) CoreTriangle(V(a), V(b), V(c));
}
GLAPI void guAADrawTriangleWithClip(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    ENTRY("guAADrawTriangleWithClip");
    if (a && b && c) CoreAATriangle(V(a), V(b), V(c), true, true, true);
}
GLAPI void guDrawPolygonVertexListWithClip(int n, const GrVertex* list) {
    ENTRY("guDrawPolygonVertexListWithClip");
    Polygon(n, nullptr, list);
}

// ---------------------------------------------------------------- buffers

GLAPI void grBufferClear(GrColor_t color, GrAlpha_t alpha, FxU16 depth) {
    ENTRY("grBufferClear");
    CoreClear(color, alpha, depth);
}
GLAPI void grBufferSwap(int interval) {
    ENTRY("grBufferSwap");
    CoreSwap(interval);
}
GLAPI int grBufferNumPending() { return 0; }
GLAPI void grRenderBuffer(GrBuffer_t b) {
    ENTRY("grRenderBuffer");
    st::RenderBuffer(b);
}

// ---------------------------------------------------------------- state

GLAPI void grAlphaBlendFunction(GrAlphaBlendFnc_t rs, GrAlphaBlendFnc_t rd, GrAlphaBlendFnc_t as,
                                GrAlphaBlendFnc_t ad) {
    ENTRY("grAlphaBlendFunction");
    GL_TRACE("  (%ld, %ld, %ld, %ld)", long(rs), long(rd), long(as), long(ad));
    st::AlphaBlendFunction(rs, rd, as, ad);
}
GLAPI void grAlphaCombine(GrCombineFunction_t f, GrCombineFactor_t k, GrCombineLocal_t l, GrCombineOther_t o,
                          FxBool inv) {
    ENTRY("grAlphaCombine");
    GL_TRACE("  (%ld, %ld, %ld, %ld, %d)", long(f), long(k), long(l), long(o), inv);
    st::AlphaCombine(f, k, l, o, inv);
}
GLAPI void grAlphaControlsITRGBLighting(FxBool e) {
    ENTRY("grAlphaControlsITRGBLighting");
    st::AlphaControlsITRGBLighting(e);
}
GLAPI void grAlphaTestFunction(GrCmpFnc_t f) {
    ENTRY("grAlphaTestFunction");
    st::AlphaTestFunction(f);
}
GLAPI void grAlphaTestReferenceValue(GrAlpha_t v) {
    ENTRY("grAlphaTestReferenceValue");
    st::AlphaTestReferenceValue(v);
}
GLAPI void grChromakeyMode(GrChromakeyMode_t m) {
    ENTRY("grChromakeyMode");
    st::ChromakeyMode(m);
}
GLAPI void grChromakeyValue(GrColor_t c) {
    ENTRY("grChromakeyValue");
    st::ChromakeyValue(c);
}
GLAPI void grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy) {
    ENTRY("grClipWindow");
    st::ClipWindow(minx, miny, maxx, maxy);
}
GLAPI void grColorCombine(GrCombineFunction_t f, GrCombineFactor_t k, GrCombineLocal_t l, GrCombineOther_t o,
                          FxBool inv) {
    ENTRY("grColorCombine");
    GL_TRACE("  (%ld, %ld, %ld, %ld, %d)", long(f), long(k), long(l), long(o), inv);
    st::ColorCombine(f, k, l, o, inv);
}
GLAPI void grColorMask(FxBool rgb, FxBool a) {
    ENTRY("grColorMask");
    st::ColorMask(rgb, a);
}
GLAPI void grConstantColorValue(GrColor_t c) {
    ENTRY("grConstantColorValue");
    st::ConstantColorValue(c);
}
GLAPI void grConstantColorValue4(float a, float r, float g, float b) {
    ENTRY("grConstantColorValue4");
    st::ConstantColorValue4(a, r, g, b);
}
GLAPI void grCullMode(GrCullMode_t m) {
    ENTRY("grCullMode");
    st::CullMode(m);
}
GLAPI void grDepthBiasLevel(FxI16 level) {
    ENTRY("grDepthBiasLevel");
    st::DepthBiasLevel(level);
}
GLAPI void grDepthBufferFunction(GrCmpFnc_t f) {
    ENTRY("grDepthBufferFunction");
    st::DepthBufferFunction(f);
}
GLAPI void grDepthBufferMode(GrDepthBufferMode_t m) {
    ENTRY("grDepthBufferMode");
    st::DepthBufferMode(m);
}
GLAPI void grDepthMask(FxBool e) {
    ENTRY("grDepthMask");
    st::DepthMask(e);
}
GLAPI void grDisableAllEffects() {
    ENTRY("grDisableAllEffects");
    st::DisableAllEffects();
}
GLAPI void grDitherMode(GrDitherMode_t m) {
    ENTRY("grDitherMode");
    st::DitherMode(m);
}
GLAPI void grFogColorValue(GrColor_t c) {
    ENTRY("grFogColorValue");
    st::FogColorValue(c);
}
GLAPI void grFogMode(GrFogMode_t m) {
    ENTRY("grFogMode");
    GL_TRACE("  (0x%lx)", long(m));
    st::FogMode(m);
}
GLAPI void grFogTable(const GrFog_t* t) {
    ENTRY("grFogTable");
    if (t) st::FogTable(t);
}
GLAPI void grGammaCorrectionValue(float v) {
    ENTRY("grGammaCorrectionValue");
    RenderSetGamma(v);
}
GLAPI void grHints(GrHint_t type, FxU32 hints) {
    ENTRY("grHints");
    GL_TRACE("  (%lu, 0x%lx)", static_cast<unsigned long>(type), static_cast<unsigned long>(hints));
    st::Hints(type, hints);
}
GLAPI void grSstOrigin(GrOriginLocation_t o) {
    ENTRY("grSstOrigin");
    st::SstOrigin(o);
}
GLAPI void grLfbConstantAlpha(GrAlpha_t a) {
    ENTRY("grLfbConstantAlpha");
    st::LfbConstantAlpha(a);
}
GLAPI void grLfbConstantDepth(FxU16 d) {
    ENTRY("grLfbConstantDepth");
    st::LfbConstantDepth(d);
}
GLAPI void grGlideGetState(void* state) {
    ENTRY("grGlideGetState");
    if (state) memcpy(state, &St(), sizeof(GlideState));
}
GLAPI void grGlideSetState(const void* state) {
    ENTRY("grGlideSetState");
    if (state) memcpy(&St(), state, sizeof(GlideState));
}

// ---------------------------------------------------------------- textures

GLAPI void grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s, GrTextureClampMode_t t) {
    ENTRY("grTexClampMode");
    GL_TRACE("  (%ld, %ld, %ld)", long(tmu), long(s), long(t));
    st::TexClampMode(tmu, s, t);
}
GLAPI void grTexCombine(GrChipID_t tmu, GrCombineFunction_t rf, GrCombineFactor_t rk, GrCombineFunction_t af,
                        GrCombineFactor_t ak, FxBool ri, FxBool ai) {
    ENTRY("grTexCombine");
    GL_TRACE("  (%ld, %ld, %ld, %ld, %ld, %d, %d)", long(tmu), long(rf), long(rk), long(af), long(ak), ri, ai);
    st::TexCombine(tmu, rf, rk, af, ak, ri, ai);
}
GLAPI void grTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t tc) {
    ENTRY("grTexCombineFunction");
    GuTexCombineFunction(tmu, tc);
}
GLAPI void guTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t tc) {
    ENTRY("guTexCombineFunction");
    GL_TRACE("  (%ld, %ld)", long(tmu), long(tc));
    GuTexCombineFunction(tmu, tc);
}
GLAPI void grTexDetailControl(GrChipID_t tmu, int bias, FxU8 scale, float max) {
    ENTRY("grTexDetailControl");
    st::TexDetailControl(tmu, bias, scale, max);
}
GLAPI void grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t mi, GrTextureFilterMode_t ma) {
    ENTRY("grTexFilterMode");
    st::TexFilterMode(tmu, mi, ma);
}
GLAPI void grTexLodBiasValue(GrChipID_t tmu, float bias) {
    ENTRY("grTexLodBiasValue");
    st::TexLodBiasValue(tmu, bias);
}
GLAPI void grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t m, FxBool blend) {
    ENTRY("grTexMipMapMode");
    GL_TRACE("  (%ld, %ld, %d)", long(tmu), long(m), blend);
    st::TexMipMapMode(tmu, m, blend);
}
GLAPI void grTexNCCTable(GrChipID_t tmu, GrNCCTable_t t) {
    ENTRY("grTexNCCTable");
    st::TexNCCTable(tmu, t);
}
GLAPI void grTexSource(GrChipID_t tmu, FxU32 start, FxU32 evenOdd, const GrTexInfo* info) {
    ENTRY("grTexSource");
    if (info)
        GL_TRACE("  (%ld, 0x%lx, %lu, lod %ld-%ld aspect %ld fmt %ld)", long(tmu), static_cast<unsigned long>(start),
                 static_cast<unsigned long>(evenOdd), long(info->largeLod), long(info->smallLod),
                 long(info->aspectRatio), long(info->format));
    st::TexSource(tmu, start, evenOdd, info);
}
GLAPI void grTexMultibase(GrChipID_t tmu, FxBool e) {
    ENTRY("grTexMultibase");
    st::TexMultibase(tmu, e);
}
GLAPI void grTexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 start, FxU32 evenOdd,
                                 const GrTexInfo* info) {
    ENTRY("grTexMultibaseAddress");
    st::TexMultibaseAddress(tmu, range, start, evenOdd, info);
}
GLAPI FxU32 grTexMinAddress(GrChipID_t) { return 0; }
GLAPI FxU32 grTexMaxAddress(GrChipID_t tmu) {
    ENTRY("grTexMaxAddress");
    return CoreTexMaxAddress(tmu);
}
GLAPI FxU32 grTexCalcMemRequired(GrLOD_t small, GrLOD_t large, GrAspectRatio_t aspect, GrTextureFormat_t fmt) {
    return TexMemRequired(small, large, aspect, fmt, GR_MIPMAPLEVELMASK_BOTH);
}
GLAPI FxU32 grTexTextureMemRequired(FxU32 evenOdd, const GrTexInfo* info) {
    if (!info) return 0;
    return TexMemRequired(info->smallLod, info->largeLod, info->aspectRatio, info->format, evenOdd);
}
GLAPI void grTexDownloadMipMap(GrChipID_t tmu, FxU32 start, FxU32 evenOdd, const GrTexInfo* info) {
    ENTRY("grTexDownloadMipMap");
    if (info)
        GL_TRACE("  (%ld, 0x%lx, %lu, lod %ld-%ld aspect %ld fmt %ld)", long(tmu), static_cast<unsigned long>(start),
                 static_cast<unsigned long>(evenOdd), long(info->largeLod), long(info->smallLod),
                 long(info->aspectRatio), long(info->format));
    CoreTexDownloadMipMap(tmu, start, evenOdd, info);
}
GLAPI void grTexDownloadMipMapLevel(GrChipID_t tmu, FxU32 start, GrLOD_t thisLod, GrLOD_t largeLod,
                                    GrAspectRatio_t aspect, GrTextureFormat_t fmt, FxU32 evenOdd, const void* data) {
    ENTRY("grTexDownloadMipMapLevel");
    GL_TRACE("  (%ld, 0x%lx, lod %ld of %ld, aspect %ld, fmt %ld, %lu)", long(tmu), static_cast<unsigned long>(start),
             long(thisLod), long(largeLod), long(aspect), long(fmt), static_cast<unsigned long>(evenOdd));
    int w, h;
    LevelSize(thisLod, aspect, &w, &h);
    CoreTexDownloadLevel(tmu, start, thisLod, largeLod, aspect, fmt, evenOdd, data, 0, h - 1);
}
GLAPI void grTexDownloadMipMapLevelPartial(GrChipID_t tmu, FxU32 start, GrLOD_t thisLod, GrLOD_t largeLod,
                                           GrAspectRatio_t aspect, GrTextureFormat_t fmt, FxU32 evenOdd,
                                           const void* data, int t, int maxT) {
    ENTRY("grTexDownloadMipMapLevelPartial");
    GL_TRACE("  (%ld, 0x%lx, lod %ld of %ld, aspect %ld, fmt %ld, %lu, rows %d-%d)", long(tmu),
             static_cast<unsigned long>(start), long(thisLod), long(largeLod), long(aspect), long(fmt),
             static_cast<unsigned long>(evenOdd), t, maxT);
    CoreTexDownloadLevel(tmu, start, thisLod, largeLod, aspect, fmt, evenOdd, data, t, maxT);
}
GLAPI void grTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, const void* data) {
    ENTRY("grTexDownloadTable");
    GL_TRACE("  (%ld, %lu)", long(tmu), static_cast<unsigned long>(type));
    CoreTexDownloadTable(tmu, type, data, 0, type == GR_TEXTABLE_PALETTE ? 255 : 11);
}
GLAPI void grTexDownloadTablePartial(GrChipID_t tmu, GrTexTable_t type, const void* data, int start, int end) {
    ENTRY("grTexDownloadTablePartial");
    CoreTexDownloadTable(tmu, type, data, start, end);
}
// A run-length encoded level download. 3dfx's Windows Glide never decoded it (its
// rle_decode_line_asm is empty there: the format was the DOS build's assembly): it writes
// its line buffer, never filled, so zeros: rows 0 to height - 1 (one more if dest_height is
// larger) of the level. The same here.
GLAPI void ConvertAndDownloadRle(GrChipID_t tmu, FxU32 start, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                                 GrTextureFormat_t fmt, FxU32 evenOdd, FxU8* data, long, FxU32, FxU32, FxU32,
                                 FxU32 height, FxU32, FxU32 destHeight, FxU16*) {
    ENTRY("ConvertAndDownloadRle");
    if (!data || height == 0) return;
    int lw, lh;
    LevelSize(thisLod, aspect, &lw, &lh);  // (Glide 2 values)
    const int rows = std::min(lh, int(height) + (destHeight > height ? 1 : 0));
    static std::vector<FxU16> zeros;
    zeros.assign(size_t(lw) * rows, 0);
    CoreTexDownloadLevel(tmu, start, thisLod, largeLod, aspect, fmt, evenOdd, zeros.data(), 0, rows - 1);
}

// ---------------------------------------------------------------- the linear frame buffer

GLAPI FxBool grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t mode, GrOriginLocation_t origin,
                       FxBool pipeline, GrLfbInfo_t* info) {
    ENTRY("grLfbLock");
    return LfbLock(type, buffer, mode, origin, pipeline, info);
}
GLAPI FxBool grLfbUnlock(GrLock_t type, GrBuffer_t buffer) {
    ENTRY("grLfbUnlock");
    return LfbUnlock(type, buffer);
}
GLAPI FxBool grLfbReadRegion(GrBuffer_t src, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 stride, void* dst) {
    ENTRY("grLfbReadRegion");
    return LfbReadRegion(src, x, y, w, h, stride, dst);
}
GLAPI FxBool grLfbWriteRegion(GrBuffer_t dst, FxU32 x, FxU32 y, GrLfbSrcFmt_t fmt, FxU32 w, FxU32 h, FxI32 stride,
                              const void* data) {
    ENTRY("grLfbWriteRegion");
    return LfbWriteRegion(dst, x, y, fmt, w, h, FXFALSE, stride, data);
}
GLAPI void grLfbWriteColorFormat(GrColorFormat_t f) {
    ENTRY("grLfbWriteColorFormat");
    LfbWriteColorFormat(f);
}
GLAPI void grLfbWriteColorSwizzle(FxBool, FxBool) {}  // (byte and word swapping: big-endian hosts)
GLAPI void guFbReadRegion(int x, int y, int w, int h, void* dst, int stride) {
    ENTRY("guFbReadRegion");
    if (x >= 0 && y >= 0 && w > 0 && h > 0)
        LfbReadRegion(GR_BUFFER_BACKBUFFER, FxU32(x), FxU32(y), FxU32(w), FxU32(h), FxU32(stride), dst);
}
GLAPI void guFbWriteRegion(int x, int y, int w, int h, const void* src, int stride) {
    ENTRY("guFbWriteRegion");
    if (x >= 0 && y >= 0 && w > 0 && h > 0)
        LfbWriteRegion(GR_BUFFER_BACKBUFFER, FxU32(x), FxU32(y), GR_LFBWRITEMODE_565, FxU32(w), FxU32(h), FXFALSE,
                       stride, src);
}

#ifdef GLIDE21
GLAPI void grLfbBegin() {}
GLAPI void grLfbEnd() {
    ENTRY("grLfbEnd");
    if (g_lfb21Write >= 0) LfbUnlock(GR_LFB_WRITE_ONLY, g_lfb21Write);
    if (g_lfb21Read >= 0) LfbUnlock(GR_LFB_READ_ONLY, g_lfb21Read);
    g_lfb21Read = g_lfb21Write = -1;
}
GLAPI const FxU32* grLfbGetReadPtr(GrBuffer_t buffer) {
    ENTRY("grLfbGetReadPtr");
    if (g_lfb21Read >= 0) LfbUnlock(GR_LFB_READ_ONLY, g_lfb21Read);
    g_lfb21Read = -1;
    GrLfbInfo_t info = {};
    info.size = sizeof(info);
    if (!LfbLock(GR_LFB_READ_ONLY, buffer, GR_LFBWRITEMODE_ANY, g_lfb21Origin, FXFALSE, &info)) return nullptr;
    g_lfb21Read = buffer;
    return static_cast<const FxU32*>(info.lfbPtr);
}
GLAPI void* grLfbGetWritePtr(GrBuffer_t buffer) {
    ENTRY("grLfbGetWritePtr");
    if (g_lfb21Write >= 0) LfbUnlock(GR_LFB_WRITE_ONLY, g_lfb21Write);
    g_lfb21Write = -1;
    GrLfbInfo_t info = {};
    info.size = sizeof(info);
    if (!LfbLock(GR_LFB_WRITE_ONLY, buffer, g_lfb21Mode, g_lfb21Origin, g_lfb21Bypass ? FXFALSE : FXTRUE, &info))
        return nullptr;
    g_lfb21Write = buffer;
    return info.lfbPtr;
}
GLAPI void grLfbWriteMode(GrLfbWriteMode_t mode) { g_lfb21Mode = mode; }
GLAPI void grLfbOrigin(GrOriginLocation_t origin) { g_lfb21Origin = origin; }
GLAPI void grLfbBypassMode(GrLfbBypassMode_t mode) { g_lfb21Bypass = mode == GR_LFBBYPASS_ENABLE; }
GLAPI void grSstPassthruMode(GrPassthruMode_t) {}
GLAPI void grSplash() {}
GLAPI FxBool grSstOpen(GrScreenResolution_t res, GrScreenRefresh_t refresh, GrColorFormat_t format,
                       GrOriginLocation_t origin, GrSmoothingMode_t, int numBuffers) {
    ENTRY("grSstOpen");
    return CoreWinOpen(nullptr, res, refresh, format, origin, numBuffers, 1);
}
#else
GLAPI void grSplash(float, float, float, float, FxU32) {}
#endif

// ---------------------------------------------------------------- the board

GLAPI void grGlideInit() {
    ENTRY("grGlideInit");
    GL_LOG("grGlideInit (%s)", g_version == kGlide21 ? "Glide 2.1" : "Glide 2.x");
    CoreInit();
    FilterGameResolutionLists();
}
GLAPI void grGlideShutdown() {
    ENTRY("grGlideShutdown");
    CoreShutdown();
}
GLAPI void grGlideGetVersion(char version[80]) {
    if (!version) return;
    lstrcpynA(version, g_version == kGlide21 ? "Glide Version 2.1.1" : "Glide Version 2.56", 80);
}
GLAPI void grGlideShamelessPlug(FxBool) {}
GLAPI void grErrorSetCallback(GrErrorCallbackFnc_t fn) {
    ENTRY("grErrorSetCallback");
    g_board.errorCallback = fn;
}
GLAPI FxBool grSstQueryHardware(GrHwConfiguration* hw) {
    ENTRY("grSstQueryHardware");
    if (g_texMem[0].bytes.empty()) CoreInit();
    CoreHwConfig(hw, BoardType());
    return hw ? FXTRUE : FXFALSE;
}
GLAPI FxBool grSstQueryBoards(GrHwConfiguration* hw) {
    if (!hw) return FXFALSE;
    memset(hw, 0, sizeof(*hw));
    hw->num_sst = 1;
    return FXTRUE;
}
GLAPI void grSstSelect(int) {}
GLAPI FxBool grSstWinOpen(FxU32 hwnd, GrScreenResolution_t res, GrScreenRefresh_t refresh, GrColorFormat_t format,
                          GrOriginLocation_t origin, int nColBuffers, int nAuxBuffers) {
    ENTRY("grSstWinOpen");
    return CoreWinOpen(reinterpret_cast<HWND>(uintptr_t(hwnd)), res, refresh, format, origin, nColBuffers, nAuxBuffers);
}
GLAPI void grSstWinClose() {
    ENTRY("grSstWinClose");
    CoreWinClose();
}
GLAPI FxBool grSstControl(GrControl_t) { return FXTRUE; }
GLAPI void grSstConfigPipeline(GrChipID_t, FxU32, FxU32) {}
GLAPI void grSstVidMode(FxU32, void*) {}
GLAPI void grSstIdle() {
    ENTRY("grSstIdle");
    RenderFlush();
}
GLAPI FxBool grSstIsBusy() { return FXFALSE; }
GLAPI FxU32 grSstStatus() { return 0x0FFFF03Fu | (FramePhase() >= 0.95 ? 0x40u : 0u); }  // FIFOs free, idle; vretrace
GLAPI FxBool grSstVRetraceOn() { return FramePhase() >= 0.95 ? FXTRUE : FXFALSE; }
GLAPI FxU32 grSstVideoLine() { return FxU32(FramePhase() * (g_board.screenH + 45)); }
GLAPI FxU32 grSstScreenWidth() { return FxU32(g_board.screenW); }
GLAPI FxU32 grSstScreenHeight() { return FxU32(g_board.screenH); }
GLAPI void grSstPerfStats(GrSstPerfStats_t* s) {
    if (s) memset(s, 0, sizeof(*s));
}
GLAPI void grSstResetPerfStats() {}
GLAPI void grTriStats(FxU32* in, FxU32* out) {
    if (in) *in = g_board.trisIn;
    if (out) *out = g_board.trisOut;
}
GLAPI void grResetTriStats() { g_board.trisIn = g_board.trisOut = 0; }
GLAPI void grCheckForRoom(FxI32) {}

// ---------------------------------------------------------------- gu

GLAPI FxBool gu3dfGetInfo(const char* name, Gu3dfInfo* info) { return Gu3dfGetInfo(name, info); }
GLAPI FxBool gu3dfLoad(const char* name, Gu3dfInfo* info) { return Gu3dfLoad(name, info); }
GLAPI void guAlphaSource(GrAlphaSource_t m) {
    ENTRY("guAlphaSource");
    GuAlphaSource(m);
}
GLAPI void guColorCombineFunction(GrColorCombineFnc_t f) {
    ENTRY("guColorCombineFunction");
    GuColorCombineFunction(f);
}
// Runs of equal 16-bit pixels over the whole image as one row: count << 16 | pixel, 4 bytes
// each; the bytes it takes (dst null: only counted). As 3dfx's gu.c (counts up to 32767).
GLAPI int guEncodeRLE16(void* dst, void* src, FxU32 width, FxU32 height) {
    if (!src) return 0;
    const FxU16* px = static_cast<const FxU16*>(src);
    FxU32* out = static_cast<FxU32*>(dst);
    const size_t n = size_t(width) * height;
    int bytes = 0;
    for (size_t i = 0; i < n;) {
        size_t len = 1;
        while (i + len < n && len < 0x7FFF && px[i + len] == px[i]) len++;
        if (out) *out++ = FxU32(len) << 16 | px[i];
        bytes += 4;
        i += len;
    }
    return bytes;
}
GLAPI FxU16 guEndianSwapBytes(FxU16 v) { return FxU16((v >> 8) | (v << 8)); }
GLAPI FxU32 guEndianSwapWords(FxU32 v) { return (v >> 16) | (v << 16); }
GLAPI float guFogTableIndexToW(int i) { return GuFogTableIndexToW(i); }
GLAPI void guFogGenerateExp(GrFog_t* t, float d) { GuFogGenerateExp(t, d); }
GLAPI void guFogGenerateExp2(GrFog_t* t, float d) { GuFogGenerateExp2(t, d); }
GLAPI void guFogGenerateLinear(GrFog_t* t, float n, float f) { GuFogGenerateLinear(t, n, f); }
// Multipass texturing: two textures with one TMU, as 3dfx's gump.c does it.
GLAPI void guMPInit() {
    ENTRY("guMPInit");
    GuMPInit();
}
GLAPI void guMPTexCombineFunction(GrTextureCombineFnc_t tc) {
    ENTRY("guMPTexCombineFunction");
    GuMPTexCombineFunction(int(tc));
}
GLAPI void guMPTexSource(GrChipID_t tmu, GrMipMapId_t mmid) {
    ENTRY("guMPTexSource");
    GuMPTexSource(tmu, mmid);
}
GLAPI void guMPDrawTriangle(const GrVertex* a, const GrVertex* b, const GrVertex* c) {
    ENTRY("guMPDrawTriangle");
    if (a && b && c) GuMPDrawTriangle(V(a), V(b), V(c));
}
GLAPI void guMovieSetName(const char*) {}
GLAPI void guMovieStart() {}
GLAPI void guMovieStop() {}
GLAPI GrMipMapId_t guTexAllocateMemory(GrChipID_t tmu, FxU8 oddEven, int w, int h, GrTextureFormat_t fmt,
                                       GrMipMapMode_t mm, GrLOD_t small, GrLOD_t large, GrAspectRatio_t aspect,
                                       GrTextureClampMode_t sc, GrTextureClampMode_t tc, GrTextureFilterMode_t minf,
                                       GrTextureFilterMode_t magf, float bias, FxBool trilinear) {
    ENTRY("guTexAllocateMemory");
    return GuTexAllocateMemory(tmu, oddEven, w, h, fmt, mm, small, large, aspect, sc, tc, minf, magf, bias, trilinear);
}
GLAPI FxBool guTexChangeAttributes(GrMipMapId_t id, int w, int h, GrTextureFormat_t fmt, GrMipMapMode_t mm,
                                   GrLOD_t small, GrLOD_t large, GrAspectRatio_t aspect, GrTextureClampMode_t sc,
                                   GrTextureClampMode_t tc, GrTextureFilterMode_t minf, GrTextureFilterMode_t magf) {
    ENTRY("guTexChangeAttributes");
    return GuTexChangeAttributes(id, w, h, fmt, mm, small, large, aspect, sc, tc, minf, magf);
}
GLAPI FxU16* guTexCreateColorMipMap() { return GuTexCreateColorMipMap(); }
GLAPI void guTexDownloadMipMap(GrMipMapId_t id, const void* src, const GuNccTable* ncc) {
    ENTRY("guTexDownloadMipMap");
    GuTexDownloadMipMap(id, src, ncc);
}
GLAPI void guTexDownloadMipMapLevel(GrMipMapId_t id, GrLOD_t lod, const void** src) {
    ENTRY("guTexDownloadMipMapLevel");
    GuTexDownloadMipMapLevel(id, lod, src);
}
GLAPI GrMipMapId_t guTexGetCurrentMipMap(GrChipID_t tmu) { return GuTexGetCurrentMipMap(tmu); }
GLAPI GrMipMapInfo* guTexGetMipMapInfo(GrMipMapId_t id) { return GuTexGetMipMapInfo(id); }
GLAPI FxU32 guTexMemQueryAvail(GrChipID_t tmu) { return GuTexMemQueryAvail(tmu); }
GLAPI void guTexMemReset() {
    ENTRY("guTexMemReset");
    GuTexMemReset();
}
GLAPI void guTexSource(GrMipMapId_t id) {
    ENTRY("guTexSource");
    GuTexSource(id);
}

// ---------------------------------------------------------------- PCI access (the 3dfx library's own)

// Games never needed these; 3dfx's tools did. Nothing is there to find.
GLAPI FxBool pciOpen() { return FXFALSE; }
GLAPI FxBool pciOpenEx(FxU32) { return FXFALSE; }
GLAPI FxBool pciClose() { return FXTRUE; }
GLAPI FxBool pciDeviceExists(FxU32) { return FXFALSE; }
GLAPI FxBool pciFindCard(FxU32, FxU32, FxU32*) { return FXFALSE; }
GLAPI FxBool pciFindCardMulti(FxU32, FxU32, FxU32*, FxU32) { return FXFALSE; }
GLAPI FxBool pciFindCardMultiFunc(FxU32, FxU32, FxU32*, FxU32) { return FXFALSE; }
GLAPI FxBool pciFindFreeMTRR(FxU32*) { return FXFALSE; }
GLAPI FxBool pciFindMTRRMatch(FxU32, FxU32, FxU32, FxU32*) { return FXFALSE; }
GLAPI FxBool pciGetConfigData(FxU32, FxU32, FxU32, FxU32, FxU32*) { return FXFALSE; }
GLAPI FxBool pciSetConfigData(FxU32, FxU32, FxU32, FxU32, FxU32*) { return FXFALSE; }
GLAPI FxU32 pciGetErrorCode() { return 0; }
GLAPI const char* pciGetErrorString() { return "no PCI access (gl2d9)"; }
GLAPI FxBool pciLinearRangeSetPermission(FxU32, FxU32, FxBool) { return FXFALSE; }
GLAPI FxU32* pciMapCard(FxU32, FxU32, FxU32, FxU32, FxU32*) { return nullptr; }
GLAPI FxU32* pciMapCardMulti(FxU32, FxU32, FxU32, FxU32, FxU32, FxU32*) { return nullptr; }
GLAPI FxBool pciMapPhysicalDeviceToLinear(FxU32*, FxU32, FxU32, FxU32*) { return FXFALSE; }
GLAPI FxBool pciMapPhysicalToLinear(FxU32*, FxU32, FxU32*) { return FXFALSE; }
GLAPI void pciOutputDebugString(const char*) {}
GLAPI FxBool pciSetMTRR(FxU32, FxU32, FxU32, FxU32) { return FXFALSE; }
GLAPI FxBool pciSetMTRRAmdK6(FxU32, FxU32, FxU32, FxU32) { return FXFALSE; }
GLAPI FxBool pciSetPassThroughBase(FxU32*, FxU32) { return FXFALSE; }
GLAPI FxBool pciUnmapPhysical(FxU32, FxU32) { return FXFALSE; }
GLAPI FxU32 _pciFetchRegister(FxU32, FxU32, FxU32, FxU32) { return 0; }
GLAPI void _pciUpdateRegister(FxU32, FxU32, FxU32, FxU32, FxU32) {}
GLAPI FxU8 pioInByte(unsigned short) { return 0; }
GLAPI FxU16 pioInWord(unsigned short) { return 0; }
GLAPI FxU32 pioInLong(unsigned short) { return 0; }
GLAPI FxBool pioOutByte(unsigned short, FxU8) { return FXFALSE; }
GLAPI FxBool pioOutWord(unsigned short, FxU16) { return FXFALSE; }
GLAPI FxBool pioOutLong(unsigned short, FxU32) { return FXFALSE; }
