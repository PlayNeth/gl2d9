// The entry points Glide 2 and Glide 3 share, argument for argument (compiled into each of the
// three DLLs; the .def files pick what each exports). See api2.cpp and api3.cpp for the rest.
#include "core.h"
#include "gu.h"
#include "lfb.h"

using namespace gl2d9;

// ---------------------------------------------------------------- state

GLAPI void grAlphaBlendFunction(GrAlphaBlendFnc_t rs, GrAlphaBlendFnc_t rd, GrAlphaBlendFnc_t as,
                                GrAlphaBlendFnc_t ad) try {
    ENTRY("grAlphaBlendFunction");
    GL_TRACE("  (%ld, %ld, %ld, %ld)", long(rs), long(rd), long(as), long(ad));
    st::AlphaBlendFunction(rs, rd, as, ad);
} catch (...) { CallFailed(__func__); }
GLAPI void grAlphaCombine(GrCombineFunction_t f, GrCombineFactor_t k, GrCombineLocal_t l, GrCombineOther_t o,
                          FxBool inv) try {
    ENTRY("grAlphaCombine");
    GL_TRACE("  (%ld, %ld, %ld, %ld, %d)", long(f), long(k), long(l), long(o), inv);
    st::AlphaCombine(f, k, l, o, inv);
} catch (...) { CallFailed(__func__); }
GLAPI void grAlphaControlsITRGBLighting(FxBool e) try {
    ENTRY("grAlphaControlsITRGBLighting");
    st::AlphaControlsITRGBLighting(e);
} catch (...) { CallFailed(__func__); }
GLAPI void grAlphaTestFunction(GrCmpFnc_t f) try {
    ENTRY("grAlphaTestFunction");
    st::AlphaTestFunction(f);
} catch (...) { CallFailed(__func__); }
GLAPI void grAlphaTestReferenceValue(GrAlpha_t v) try {
    ENTRY("grAlphaTestReferenceValue");
    st::AlphaTestReferenceValue(v);
} catch (...) { CallFailed(__func__); }
GLAPI void grChromakeyMode(GrChromakeyMode_t m) try {
    ENTRY("grChromakeyMode");
    st::ChromakeyMode(m);
} catch (...) { CallFailed(__func__); }
GLAPI void grChromakeyValue(GrColor_t c) try {
    ENTRY("grChromakeyValue");
    st::ChromakeyValue(c);
} catch (...) { CallFailed(__func__); }
GLAPI void grClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy) try {
    ENTRY("grClipWindow");
    st::ClipWindow(minx, miny, maxx, maxy);
} catch (...) { CallFailed(__func__); }
GLAPI void grColorCombine(GrCombineFunction_t f, GrCombineFactor_t k, GrCombineLocal_t l, GrCombineOther_t o,
                          FxBool inv) try {
    ENTRY("grColorCombine");
    GL_TRACE("  (%ld, %ld, %ld, %ld, %d)", long(f), long(k), long(l), long(o), inv);
    st::ColorCombine(f, k, l, o, inv);
} catch (...) { CallFailed(__func__); }
GLAPI void grColorMask(FxBool rgb, FxBool a) try {
    ENTRY("grColorMask");
    st::ColorMask(rgb, a);
} catch (...) { CallFailed(__func__); }
GLAPI void grConstantColorValue(GrColor_t c) try {
    ENTRY("grConstantColorValue");
    st::ConstantColorValue(c);
} catch (...) { CallFailed(__func__); }
GLAPI void grCullMode(GrCullMode_t m) try {
    ENTRY("grCullMode");
    st::CullMode(m);
} catch (...) { CallFailed(__func__); }
GLAPI void grDepthBufferFunction(GrCmpFnc_t f) try {
    ENTRY("grDepthBufferFunction");
    st::DepthBufferFunction(f);
} catch (...) { CallFailed(__func__); }
GLAPI void grDepthBufferMode(GrDepthBufferMode_t m) try {
    ENTRY("grDepthBufferMode");
    st::DepthBufferMode(m);
} catch (...) { CallFailed(__func__); }
GLAPI void grDepthMask(FxBool e) try {
    ENTRY("grDepthMask");
    st::DepthMask(e);
} catch (...) { CallFailed(__func__); }
GLAPI void grDisableAllEffects() try {
    ENTRY("grDisableAllEffects");
    st::DisableAllEffects();
} catch (...) { CallFailed(__func__); }
GLAPI void grDitherMode(GrDitherMode_t m) try {
    ENTRY("grDitherMode");
    st::DitherMode(m);
} catch (...) { CallFailed(__func__); }
GLAPI void grFogColorValue(GrColor_t c) try {
    ENTRY("grFogColorValue");
    st::FogColorValue(c);
} catch (...) { CallFailed(__func__); }
GLAPI void grFogTable(const GrFog_t* t) try {
    ENTRY("grFogTable");
    if (t) st::FogTable(t);
} catch (...) { CallFailed(__func__); }
GLAPI void grGammaCorrectionValue(float v) try {
    ENTRY("grGammaCorrectionValue");
    RenderSetGamma(v);
} catch (...) { CallFailed(__func__); }
GLAPI void grSstOrigin(GrOriginLocation_t o) try {
    ENTRY("grSstOrigin");
    st::SstOrigin(o);
} catch (...) { CallFailed(__func__); }
GLAPI void grLfbConstantAlpha(GrAlpha_t a) try {
    ENTRY("grLfbConstantAlpha");
    st::LfbConstantAlpha(a);
} catch (...) { CallFailed(__func__); }

// ---------------------------------------------------------------- textures

GLAPI void grTexClampMode(GrChipID_t tmu, GrTextureClampMode_t s, GrTextureClampMode_t t) try {
    ENTRY("grTexClampMode");
    GL_TRACE("  (%ld, %ld, %ld)", long(tmu), long(s), long(t));
    st::TexClampMode(tmu, s, t);
} catch (...) { CallFailed(__func__); }
GLAPI void grTexCombine(GrChipID_t tmu, GrCombineFunction_t rf, GrCombineFactor_t rk, GrCombineFunction_t af,
                        GrCombineFactor_t ak, FxBool ri, FxBool ai) try {
    ENTRY("grTexCombine");
    GL_TRACE("  (%ld, %ld, %ld, %ld, %ld, %d, %d)", long(tmu), long(rf), long(rk), long(af), long(ak), ri, ai);
    st::TexCombine(tmu, rf, rk, af, ak, ri, ai);
} catch (...) { CallFailed(__func__); }
GLAPI void grTexDetailControl(GrChipID_t tmu, int bias, FxU8 scale, float max) try {
    ENTRY("grTexDetailControl");
    st::TexDetailControl(tmu, bias, scale, max);
} catch (...) { CallFailed(__func__); }
GLAPI void grTexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t mi, GrTextureFilterMode_t ma) try {
    ENTRY("grTexFilterMode");
    st::TexFilterMode(tmu, mi, ma);
} catch (...) { CallFailed(__func__); }
GLAPI void grTexLodBiasValue(GrChipID_t tmu, float bias) try {
    ENTRY("grTexLodBiasValue");
    st::TexLodBiasValue(tmu, bias);
} catch (...) { CallFailed(__func__); }
GLAPI void grTexMipMapMode(GrChipID_t tmu, GrMipMapMode_t m, FxBool blend) try {
    ENTRY("grTexMipMapMode");
    GL_TRACE("  (%ld, %ld, %d)", long(tmu), long(m), blend);
    st::TexMipMapMode(tmu, m, blend);
} catch (...) { CallFailed(__func__); }
GLAPI void grTexMultibase(GrChipID_t tmu, FxBool e) try {
    ENTRY("grTexMultibase");
    st::TexMultibase(tmu, e);
} catch (...) { CallFailed(__func__); }
GLAPI FxU32 grTexMinAddress(GrChipID_t) { return 0; }
GLAPI FxU32 grTexMaxAddress(GrChipID_t tmu) try {
    ENTRY("grTexMaxAddress");
    return CoreTexMaxAddress(tmu);
} catch (...) { return CallFailed(__func__, 0); }

// ---------------------------------------------------------------- the linear frame buffer

GLAPI FxBool grLfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t mode, GrOriginLocation_t origin,
                       FxBool pipeline, GrLfbInfo_t* info) try {
    ENTRY("grLfbLock");
    return LfbLock(type, buffer, mode, origin, pipeline, info);
} catch (...) { return CallFailed(__func__, FXFALSE); }
GLAPI FxBool grLfbUnlock(GrLock_t type, GrBuffer_t buffer) try {
    ENTRY("grLfbUnlock");
    return LfbUnlock(type, buffer);
} catch (...) { return CallFailed(__func__, FXFALSE); }
GLAPI FxBool grLfbReadRegion(GrBuffer_t src, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 stride, void* dst) try {
    ENTRY("grLfbReadRegion");
    return LfbReadRegion(src, x, y, w, h, stride, dst);
} catch (...) { return CallFailed(__func__, FXFALSE); }
GLAPI void grLfbWriteColorFormat(GrColorFormat_t f) try {
    ENTRY("grLfbWriteColorFormat");
    LfbWriteColorFormat(f);
} catch (...) { CallFailed(__func__); }
GLAPI void grLfbWriteColorSwizzle(FxBool, FxBool) {}
// ---------------------------------------------------------------- the board

GLAPI void grGlideShutdown() try {
    ENTRY("grGlideShutdown");
    CoreShutdown();
} catch (...) { CallFailed(__func__); }
GLAPI void grErrorSetCallback(GrErrorCallbackFnc_t fn) try {
    ENTRY("grErrorSetCallback");
    g_board.errorCallback = fn;
} catch (...) { CallFailed(__func__); }
GLAPI void grSstSelect(int) {}
GLAPI void grSstConfigPipeline(GrChipID_t, FxU32, FxU32) {}
GLAPI void grSstVidMode(FxU32, void*) {}
GLAPI void grCheckForRoom(FxI32) {}

// ---------------------------------------------------------------- statistics

GLAPI void grTriStats(FxU32* in, FxU32* out) try {
    ENTRY("grTriStats");
    if (in) *in = g_board.trisIn;
    if (out) *out = g_board.trisOut;
} catch (...) { CallFailed(__func__); }
GLAPI void grResetTriStats() try {
    ENTRY("grResetTriStats");
    g_board.trisIn = g_board.trisOut = 0;
} catch (...) { CallFailed(__func__); }

// ---------------------------------------------------------------- gu

GLAPI float guFogTableIndexToW(int i) try { return GuFogTableIndexToW(i); } catch (...) {
    return CallFailed(__func__, 0.0f);
}
GLAPI void guFogGenerateExp(GrFog_t* t, float d) try { GuFogGenerateExp(t, d); } catch (...) {
    CallFailed(__func__);
}
GLAPI void guFogGenerateExp2(GrFog_t* t, float d) try { GuFogGenerateExp2(t, d); } catch (...) {
    CallFailed(__func__);
}
GLAPI void guFogGenerateLinear(GrFog_t* t, float n, float f) try { GuFogGenerateLinear(t, n, f); } catch (...) {
    CallFailed(__func__);
}
