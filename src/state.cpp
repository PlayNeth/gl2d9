// Glide's state functions, call for call as 3dfx's Glide 2.x programs the Voodoo registers
// (gglide.c, gtex.c, gsst.c, distate.c): the renderer draws from those registers.
#include "state.h"

#include "texmem.h"

namespace gl2d9 {

Board g_board;

// The aspect bits of tLOD (Glide's _gr_aspect_xlate_table).
FxU32 TLodAspectBits(GrAspectRatio_t a) {
    if (a < 0) a = 0;
    if (a > GR_ASPECT_1x8) a = GR_ASPECT_1x8;
    return a < GR_ASPECT_1x1 ? (FxU32(GR_ASPECT_1x1 - a) << SST_LOD_ASPECT_SHIFT) | SST_LOD_S_IS_WIDER
                             : FxU32(a - GR_ASPECT_1x1) << SST_LOD_ASPECT_SHIFT;
}
// The split bits of tLOD for an even/odd mask (Glide's _gr_evenOdd_xlate_table).
FxU32 TLodEvenOddBits(FxU32 evenOdd) {
    switch (evenOdd) {
    case GR_MIPMAPLEVELMASK_EVEN: return SST_LOD_TSPLIT;
    case GR_MIPMAPLEVELMASK_ODD: return SST_LOD_TSPLIT | SST_LOD_ODD;
    default: return 0;
    }
}

FxU32 SwizzleColor(GrColor_t c) {  // Glide's _grSwizzleColor: into ARGB
    switch (g_board.colorFormat) {
    case GR_COLORFORMAT_ABGR: return (c & 0xFF00FF00u) | (c & 0xFF) << 16 | ((c >> 16) & 0xFF);
    case GR_COLORFORMAT_RGBA:
        return (c & 0xFF) << 24 | (c >> 24) << 16 | ((c >> 16) & 0xFF) << 8 | ((c >> 8) & 0xFF);
    case GR_COLORFORMAT_BGRA:
        return (c & 0xFF) << 24 | ((c >> 8) & 0xFF) << 16 | ((c >> 16) & 0xFF) << 8 | (c >> 24);
    default: return c;
    }
}

void GlideError(const char* msg, bool fatal) {
    GL_LOG("glide error%s: %s", fatal ? " (fatal)" : "", msg);
    if (g_board.errorCallback) g_board.errorCallback(msg, fatal ? FXTRUE : FXFALSE);
}

namespace st {

namespace {
Regs& R() { return St().r; }

// The combine unit's bits for a function (Glide's switch in grColorCombine), for the color
// combine (cc), alpha combine (cca) and texture combines; zero, sub, addC, addA bits given.
FxU32 FunctionBits(GrCombineFunction_t f, FxU32 zero, FxU32 sub, FxU32 addC, FxU32 addA, bool alphaUnit) {
    switch (f) {
    case GR_COMBINE_FUNCTION_ZERO: return zero;
    case GR_COMBINE_FUNCTION_LOCAL: return zero | (alphaUnit ? addA : addC);
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA: return zero | addA;
    case GR_COMBINE_FUNCTION_SCALE_OTHER: return 0;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: return alphaUnit ? addA : addC;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA: return addA;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL: return sub;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: return sub | (alphaUnit ? addA : addC);
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA: return sub | addA;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL: return zero | sub | (alphaUnit ? addA : addC);
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA: return zero | sub | addA;
    default: return 0;
    }
}
}  // namespace

void ColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local,
                  GrCombineOther_t other, FxBool invert) {
    FxU32 p = R().fbzColorPath;
    p &= ~(SST_ENTEXTUREMAP | SST_RGBSELECT | SST_LOCALSELECT | SST_CC_ZERO_OTHER | SST_CC_SUB_CLOCAL | SST_CC_MSELECT |
           SST_CC_REVERSE_BLEND | SST_CC_ADD_CLOCAL | SST_CC_ADD_ALOCAL | SST_CC_INVERT_OUTPUT);
    p |= SST_PARMADJUST;
    if (!(factor & 0x8)) p |= SST_CC_REVERSE_BLEND;
    factor &= 0x7;
    // (The texture's RGB as the factor reads the texel too.)
    St().ccRequiresTexture = factor == GR_COMBINE_FACTOR_TEXTURE_ALPHA || factor == GR_COMBINE_FACTOR_TEXTURE_RGB ||
                             other == GR_COMBINE_OTHER_TEXTURE;
    p |= FxU32(factor) << SST_CC_MSELECT_SHIFT;
    p |= (FxU32(local) << SST_LOCALSELECT_SHIFT) & SST_LOCALSELECT;
    p |= (FxU32(other) << SST_RGBSELECT_SHIFT) & SST_RGBSELECT;
    if (invert) p |= SST_CC_INVERT_OUTPUT;
    p |= FunctionBits(function, SST_CC_ZERO_OTHER, SST_CC_SUB_CLOCAL, SST_CC_ADD_CLOCAL, SST_CC_ADD_ALOCAL, false);
    if (St().ccRequiresTexture || St().acRequiresTexture) p |= SST_ENTEXTUREMAP;
    R().fbzColorPath = p;
}

void AlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local,
                  GrCombineOther_t other, FxBool invert) {
    FxU32 p = R().fbzColorPath;
    p &= ~(SST_ENTEXTUREMAP | SST_ASELECT | SST_ALOCALSELECT | SST_CCA_ZERO_OTHER | SST_CCA_SUB_CLOCAL |
           SST_CCA_MSELECT | SST_CCA_REVERSE_BLEND | SST_CCA_ADD_CLOCAL | SST_CCA_ADD_ALOCAL | SST_CCA_INVERT_OUTPUT);
    if (!(factor & 0x8)) p |= SST_CCA_REVERSE_BLEND;
    factor &= 0x7;
    St().acRequiresTexture = factor == GR_COMBINE_FACTOR_TEXTURE_ALPHA || other == GR_COMBINE_OTHER_TEXTURE;
    p |= FxU32(factor) << SST_CCA_MSELECT_SHIFT;
    p |= (FxU32(local) << SST_ALOCALSELECT_SHIFT) & SST_ALOCALSELECT;
    p |= (FxU32(other) << SST_ASELECT_SHIFT) & SST_ASELECT;
    if (invert) p |= SST_CCA_INVERT_OUTPUT;
    // The alpha unit adds the local alpha for both LOCAL and LOCAL_ALPHA (Glide's switch).
    p |= FunctionBits(function, SST_CCA_ZERO_OTHER, SST_CCA_SUB_CLOCAL, SST_CCA_ADD_CLOCAL, SST_CCA_ADD_ALOCAL, true);
    if (St().ccRequiresTexture || St().acRequiresTexture) p |= SST_ENTEXTUREMAP;
    R().fbzColorPath = p;
}

void AlphaControlsITRGBLighting(FxBool enable) {
    if (enable) R().fbzColorPath |= SST_LOCALSELECT_OVERRIDE_WITH_ATEX;
    else R().fbzColorPath &= ~SST_LOCALSELECT_OVERRIDE_WITH_ATEX;
}

void AlphaBlendFunction(GrAlphaBlendFnc_t rgbSf, GrAlphaBlendFnc_t rgbDf, GrAlphaBlendFnc_t aSf,
                        GrAlphaBlendFnc_t aDf) {
    FxU32 a = R().alphaMode;
    // The alpha factors can only be one or zero (Glide forces anything else).
    if (aSf != GR_BLEND_ONE && aSf != GR_BLEND_ZERO) aSf = GR_BLEND_ONE;
    if (aDf != GR_BLEND_ONE && aDf != GR_BLEND_ZERO) aDf = GR_BLEND_ZERO;
    if (rgbSf == GR_BLEND_ONE && rgbDf == GR_BLEND_ZERO && aSf == GR_BLEND_ONE && aDf == GR_BLEND_ZERO)
        a &= ~SST_ENALPHABLEND;
    else
        a |= SST_ENALPHABLEND;
    a &= ~(SST_RGBSRCFACT | SST_RGBDSTFACT | SST_ASRCFACT | SST_ADSTFACT);
    a |= (FxU32(rgbSf) & 0xF) << SST_RGBSRCFACT_SHIFT | (FxU32(rgbDf) & 0xF) << SST_RGBDSTFACT_SHIFT |
         (FxU32(aSf) & 0xF) << SST_ASRCFACT_SHIFT | (FxU32(aDf) & 0xF) << SST_ADSTFACT_SHIFT;
    R().alphaMode = a;
}

void AlphaTestFunction(GrCmpFnc_t fnc) {
    FxU32 a = R().alphaMode & ~(SST_ALPHAFUNC | SST_ENALPHAFUNC);
    if (fnc != GR_CMP_ALWAYS) a |= ((FxU32(fnc) & 7) << SST_ALPHAFUNC_SHIFT) | SST_ENALPHAFUNC;
    R().alphaMode = a;
}

void AlphaTestReferenceValue(GrAlpha_t v) {
    R().alphaMode = (R().alphaMode & ~SST_ALPHAREF) | FxU32(v) << SST_ALPHAREF_SHIFT;
}

void ChromakeyMode(GrChromakeyMode_t mode) {
    if (mode == GR_CHROMAKEY_ENABLE) R().fbzMode |= SST_ENCHROMAKEY;
    else R().fbzMode &= ~SST_ENCHROMAKEY;
}

void ChromakeyValue(GrColor_t c) { R().chromaKey = SwizzleColor(c); }

void ClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy) {
    // Glide's _grClipNormalizeAndGenerateRegValues: no bogus clip coordinates.
    if (maxx > FxU32(g_board.screenW)) maxx = g_board.screenW;
    if (maxy > FxU32(g_board.screenH)) maxy = g_board.screenH;
    if (minx > maxx) minx = maxx;
    if (miny > maxy) miny = maxy;
    R().clipLeftRight = minx << SST_CLIPLEFT_SHIFT | maxx << SST_CLIPRIGHT_SHIFT;
    R().clipBottomTop = miny << SST_CLIPBOTTOM_SHIFT | maxy << SST_CLIPTOP_SHIFT;
}

void ColorMask(FxBool rgb, FxBool alpha) {
    FxU32 m = R().fbzMode;
    if (rgb) m |= SST_RGBWRMASK;
    else m &= ~SST_RGBWRMASK;
    // The alpha buffer shares the depth buffer's memory: only without depth buffering.
    if (!(m & SST_ENDEPTHBUFFER) && g_board.numColorBuffers != 3) {
        if (alpha) m |= SST_ENALPHABUFFER | SST_ZAWRMASK;
        else m &= ~(SST_ENALPHABUFFER | SST_ZAWRMASK);
    }
    R().fbzMode = m;
}

void ConstantColorValue(GrColor_t c) {
    R().color0 = R().color1 = SwizzleColor(c);
}

// Glide's _grColorCombineDelta0Mode: the iterated color replaced by grConstantColorValue4's.
void ColorCombineDelta0Mode(FxBool enable) { St().ccDelta0 = enable ? 1 : 0; }

void ConstantColorValue4(float a, float r, float g, float b) {
    St().ccA = a;
    St().ccR = r;
    St().ccG = g;
    St().ccB = b;
}

void CullMode(GrCullMode_t mode) { St().cullMode = mode; }

void DepthBiasLevel(FxI16 level) {
    R().zaColor = (R().zaColor & ~SST_ZACOLOR_DEPTH) | (FxU32(level) & SST_ZACOLOR_DEPTH);
}

void DepthBufferFunction(GrCmpFnc_t fnc) {
    R().fbzMode = (R().fbzMode & ~SST_ZFUNC) | (FxU32(fnc) & 7) << SST_ZFUNC_SHIFT;
}

void DepthBufferMode(GrDepthBufferMode_t mode) {
    FxU32 m = R().fbzMode & ~(SST_ENDEPTHBUFFER | SST_WBUFFER | SST_ENZBIAS | SST_ZCOMPARE_TO_ZACOLOR);
    switch (mode) {
    case GR_DEPTHBUFFER_ZBUFFER: m |= SST_ENDEPTHBUFFER | SST_ENZBIAS; break;
    case GR_DEPTHBUFFER_WBUFFER: m |= SST_ENDEPTHBUFFER | SST_WBUFFER | SST_ENZBIAS; break;
    case GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS: m |= SST_ENDEPTHBUFFER | SST_ZCOMPARE_TO_ZACOLOR; break;
    case GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS: m |= SST_ENDEPTHBUFFER | SST_WBUFFER | SST_ZCOMPARE_TO_ZACOLOR; break;
    default: break;
    }
    R().fbzMode = m;
}

void DepthMask(FxBool enable) {
    if (enable) R().fbzMode |= SST_ZAWRMASK;
    else R().fbzMode &= ~SST_ZAWRMASK;
}

void DisableAllEffects() {
    AlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    AlphaTestFunction(GR_CMP_ALWAYS);
    ChromakeyMode(GR_CHROMAKEY_DISABLE);
    DepthBiasLevel(0);
    DepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    FogMode(GR_FOG_DISABLE);
}

void DitherMode(GrDitherMode_t mode) {
    FxU32 m = R().fbzMode & ~(SST_ENDITHER | SST_DITHER2x2 | SST_ENDITHERSUBTRACT);
    if (mode == GR_DITHER_2x2) m |= SST_ENDITHER | SST_DITHER2x2;
    else if (mode == GR_DITHER_4x4) m |= SST_ENDITHER | SST_ENDITHERSUBTRACT;
    R().fbzMode = m;
}

// Glide 2's modes (Glide 3's are mapped onto them by its layer).
void FogMode(GrFogMode_t mode) {
    FxU32 f = R().fogMode & ~(SST_ENFOGGING | SST_FOGADD | SST_FOGMULT | SST_FOG_ALPHA | SST_FOG_Z | SST_FOG_CONSTANT);
    switch (mode & 0xFF) {
    case GR_FOG_WITH_ITERATED_ALPHA: f |= SST_ENFOGGING | SST_FOG_ALPHA; break;
    case GR_FOG_WITH_ITERATED_Z: f |= SST_ENFOGGING | SST_FOG_Z; break;
    case GR_FOG_WITH_TABLE: f |= SST_ENFOGGING; break;
    default: break;
    }
    if (mode & GR_FOG_MULT2) f |= SST_FOGMULT;
    if (mode & GR_FOG_ADD2) f |= SST_FOGADD;
    R().fogMode = f;
}

void FogColorValue(GrColor_t c) { R().fogColor = SwizzleColor(c); }

void FogTable(const GrFog_t* table) {
    if (table) memcpy(R().fogTable, table, GR_FOG_TABLE_SIZE);
}

void RenderBuffer(GrBuffer_t buffer) {
    R().fbzMode = (R().fbzMode & ~SST_DRAWBUFFER) |
                  (buffer == GR_BUFFER_FRONTBUFFER ? SST_DRAWBUFFER_FRONT : SST_DRAWBUFFER_BACK);
}

void SstOrigin(GrOriginLocation_t origin) {
    if (origin == GR_ORIGIN_LOWER_LEFT) R().fbzMode |= SST_YORIGIN;
    else R().fbzMode &= ~SST_YORIGIN;
}

void Hints(GrHint_t type, FxU32 hints) {
    switch (type) {
    case GR_HINT_STWHINT: St().paramHints = hints; break;
    case GR_HINT_ALLOW_MIPMAP_DITHER: St().allowLodDither = hints != 0; break;
    default: break;
    }
}

void LfbConstantAlpha(GrAlpha_t a) { St().lfbConstantAlpha = a; }
void LfbConstantDepth(FxU32 d) { St().lfbConstantDepth = d; }

void StippleMode(FxI32 mode) {
    FxU32& m = R().fbzMode;
    m &= ~(SST_ENSTIPPLE | SST_ENSTIPPLEPATTERN);
    if (mode == GR_STIPPLE_PATTERN) m |= SST_ENSTIPPLE | SST_ENSTIPPLEPATTERN;
    else if (mode == GR_STIPPLE_ROTATE) m |= SST_ENSTIPPLE;
}
void StipplePattern(FxU32 pattern) {
    R().stipple = pattern;
    g_board.stippleWrites++;  // (the register written: rotate mode's position starts over)
}

// ---------------------------------------------------------------- textures

namespace {
bool TmuOk(GrChipID_t tmu) { return tmu >= 0 && tmu < g_board.numTmu; }
}  // namespace

void TexClampMode(GrChipID_t tmu, GrTextureClampMode_t s, GrTextureClampMode_t t) {
    if (!TmuOk(tmu)) return;
    FxU32& m = R().tmu[tmu].textureMode;
    m &= ~(SST_TCLAMPS | SST_TCLAMPT);
    m |= (s == GR_TEXTURECLAMP_CLAMP ? SST_TCLAMPS : 0) | (t == GR_TEXTURECLAMP_CLAMP ? SST_TCLAMPT : 0);
    FxU32& lod = R().tmu[tmu].tLOD;
    lod &= ~(SST_LOD_TMIRRORS | SST_LOD_TMIRRORT);
    lod |= (s == GR_TEXTURECLAMP_MIRROR_EXT ? SST_LOD_TMIRRORS : 0) |
           (t == GR_TEXTURECLAMP_MIRROR_EXT ? SST_LOD_TMIRRORT : 0);
}

void TexCombine(GrChipID_t tmu, GrCombineFunction_t rgbFn, GrCombineFactor_t rgbFactor, GrCombineFunction_t aFn,
                GrCombineFactor_t aFactor, FxBool rgbInvert, FxBool aInvert) {
    if (!TmuOk(tmu)) return;
    FxU32 m = R().tmu[tmu].textureMode & ~(SST_TCOMBINE | SST_TACOMBINE);
    FxU32 lod = R().tmu[tmu].tLOD & ~SST_LOD_ODD;
    m |= (FxU32(rgbFactor) & 7) << SST_TC_MSELECT_SHIFT;
    if (!(rgbFactor & 8)) m |= SST_TC_REVERSE_BLEND;
    m |= (FxU32(aFactor) & 7) << SST_TCA_MSELECT_SHIFT;
    if (!(aFactor & 8)) m |= SST_TCA_REVERSE_BLEND;
    if (rgbInvert) m |= SST_TC_INVERT_OUTPUT;
    if (aInvert) m |= SST_TCA_INVERT_OUTPUT;
    m |= FunctionBits(rgbFn, SST_TC_ZERO_OTHER, SST_TC_SUB_CLOCAL, SST_TC_ADD_CLOCAL, SST_TC_ADD_ALOCAL, false);
    // The texture alpha unit has its own LOCAL (add clocal) unlike the FBI's alpha unit.
    switch (aFn) {
    case GR_COMBINE_FUNCTION_ZERO: m |= SST_TCA_ZERO_OTHER; break;
    case GR_COMBINE_FUNCTION_LOCAL: m |= SST_TCA_ZERO_OTHER | SST_TCA_ADD_CLOCAL; break;
    case GR_COMBINE_FUNCTION_LOCAL_ALPHA: m |= SST_TCA_ZERO_OTHER | SST_TCA_ADD_ALOCAL; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER: break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL: m |= SST_TCA_ADD_CLOCAL; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA: m |= SST_TCA_ADD_ALOCAL; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL: m |= SST_TCA_SUB_CLOCAL; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL: m |= SST_TCA_SUB_CLOCAL | SST_TCA_ADD_CLOCAL; break;
    case GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        m |= SST_TCA_SUB_CLOCAL | SST_TCA_ADD_ALOCAL;
        break;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL:
        m |= SST_TCA_ZERO_OTHER | SST_TCA_SUB_CLOCAL | SST_TCA_ADD_CLOCAL;
        break;
    case GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA:
        m |= SST_TCA_ZERO_OTHER | SST_TCA_SUB_CLOCAL | SST_TCA_ADD_ALOCAL;
        break;
    default: break;
    }
    // Two-pass trilinear: multiply by the LOD fraction without reverse blend is the odd pass.
    if ((m & SST_TRILINEAR) && (m & SST_TC_ZERO_OTHER) && (m & SST_TC_MSELECT) == SST_TC_BLEND_LODFRAC &&
        !(m & SST_TC_REVERSE_BLEND))
        lod |= SST_LOD_ODD;
    lod |= TLodEvenOddBits(St().tmu[tmu].evenOdd);
    R().tmu[tmu].textureMode = m;
    R().tmu[tmu].tLOD = lod;
}

void TexDetailControl(GrChipID_t tmu, int lodBias, FxU8 detailScale, float detailMax) {
    if (!TmuOk(tmu)) return;
    const FxU32 dmax = FxU32(detailMax * 255.0f);
    FxU32 d = (FxU32(lodBias) << SST_DETAIL_BIAS_SHIFT) & SST_DETAIL_BIAS;
    d |= (dmax << SST_DETAIL_MAX_SHIFT) & SST_DETAIL_MAX;
    d |= (FxU32(detailScale) << SST_DETAIL_SCALE_SHIFT) & SST_DETAIL_SCALE;
    R().tmu[tmu].tDetail = d;
}

void TexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minf, GrTextureFilterMode_t magf) {
    if (!TmuOk(tmu)) return;
    FxU32& m = R().tmu[tmu].textureMode;
    m &= ~(SST_TMINFILTER | SST_TMAGFILTER);
    m |= (minf == GR_TEXTUREFILTER_BILINEAR ? SST_TMINFILTER : 0) |
         (magf == GR_TEXTUREFILTER_BILINEAR ? SST_TMAGFILTER : 0);
}

void TexLodBiasValue(GrChipID_t tmu, float bias) {
    if (!TmuOk(tmu)) return;
    // Glide's _grTexFloatLODToFixedLOD: quarters, rounded, 6 bits.
    const int q = int((bias + 0.125f) / 0.25f) & 0x3F;
    R().tmu[tmu].tLOD = (R().tmu[tmu].tLOD & ~SST_LODBIAS) | FxU32(q) << SST_LODBIAS_SHIFT;
}

void TexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend) {
    if (!TmuOk(tmu)) return;
    FxU32 lod = R().tmu[tmu].tLOD & ~(SST_LODMIN | SST_LODMAX | SST_LOD_ODD);
    FxU32 m = R().tmu[tmu].textureMode & ~(SST_TLODDITHER | SST_TRILINEAR);
    const TmuConfig& c = St().tmu[tmu];
    switch (mode) {
    case GR_MIPMAP_DISABLE: lod |= SST_TLOD_MINMAX_INT(FxU32(c.largeLod), FxU32(c.largeLod)); break;
    case GR_MIPMAP_NEAREST_DITHER:
        if (St().allowLodDither) m |= SST_TLODDITHER;
        [[fallthrough]];
    case GR_MIPMAP_NEAREST: lod |= SST_TLOD_MINMAX_INT(FxU32(c.largeLod), FxU32(c.smallLod)); break;
    default: GlideError("grTexMipMapMode:  invalid mode passed", false); break;
    }
    St().tmu[tmu].mmMode = mode;
    if (lodBlend) {
        m |= SST_TRILINEAR;
        if ((m & SST_TC_ZERO_OTHER) && (m & SST_TC_MSELECT) == SST_TC_BLEND_LODFRAC && !(m & SST_TC_REVERSE_BLEND))
            lod |= SST_LOD_ODD;
    }
    lod |= TLodEvenOddBits(c.evenOdd);
    R().tmu[tmu].tLOD = lod;
    R().tmu[tmu].textureMode = m;
}

void TexNCCTable(GrChipID_t tmu, GrNCCTable_t table) {
    if (!TmuOk(tmu)) return;
    St().tmu[tmu].nccTable = table;
    if (table) R().tmu[tmu].textureMode |= SST_TNCCSELECT;
    else R().tmu[tmu].textureMode &= ~SST_TNCCSELECT;
}

void TexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, const GrTexInfo* info) {
    if (!TmuOk(tmu) || !info) return;
    TmuConfig& c = St().tmu[tmu];
    c.smallLod = info->smallLod;
    c.largeLod = info->largeLod;
    c.evenOdd = evenOdd;
    const FxU32 base = TexCalcBaseAddress(startAddress, info->largeLod, info->aspectRatio, info->format, evenOdd) >> 3;
    FxU32 m = R().tmu[tmu].textureMode & ~SST_TFORMAT;
    m |= (FxU32(info->format) << SST_TFORMAT_SHIFT) | SST_TPERSP_ST | SST_TCLAMPW;
    FxU32 lod = R().tmu[tmu].tLOD;
    lod &= ~(SST_LODMIN | SST_LODMAX | SST_LOD_ASPECT | SST_LOD_TSPLIT | SST_LOD_ODD | SST_LOD_S_IS_WIDER);
    lod |= SST_TLOD_MINMAX_INT(FxU32(info->largeLod),
                               FxU32(c.mmMode == GR_MIPMAP_DISABLE ? info->largeLod : info->smallLod));
    lod |= TLodEvenOddBits(evenOdd);
    lod |= TLodAspectBits(info->aspectRatio);
    R().tmu[tmu].texBaseAddr = base;
    R().tmu[tmu].textureMode = m;
    R().tmu[tmu].tLOD = lod;
}

void TexMultibase(GrChipID_t tmu, FxBool enable) {
    if (!TmuOk(tmu)) return;
    if (enable) R().tmu[tmu].tLOD |= SST_TMULTIBASEADDR;
    else R().tmu[tmu].tLOD &= ~SST_TMULTIBASEADDR;
}

void TexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 startAddress, FxU32 evenOdd,
                         const GrTexInfo* info) {
    if (!TmuOk(tmu) || !info) return;
    static const GrLOD_t kLod[] = {GR_LOD_256, GR_LOD_128, GR_LOD_64, GR_LOD_32};
    if (range > GR_TEXBASE_32_TO_1) return;
    const FxU32 base = TexCalcBaseAddress(startAddress, kLod[range], info->aspectRatio, info->format, evenOdd) >> 3;
    TmuRegs& t = R().tmu[tmu];
    (range == GR_TEXBASE_256 ? t.texBaseAddr : range == GR_TEXBASE_128 ? t.texBaseAddr1
                                             : range == GR_TEXBASE_64 ? t.texBaseAddr2 : t.texBaseAddr38) = base;
}

// ---------------------------------------------------------------- the state grSstWinOpen sets up

void InitState(GrOriginLocation_t origin) {
    GlideState& s = St();
    s = GlideState{};
    s.r.fbzMode = SST_ENRECTCLIP | SST_ENZBIAS | SST_DRAWBUFFER_BACK;
    Hints(GR_HINT_ALLOW_MIPMAP_DITHER, 0);
    SstOrigin(origin);
    AlphaBlendFunction(GR_BLEND_ONE, GR_BLEND_ZERO, GR_BLEND_ONE, GR_BLEND_ZERO);
    AlphaTestFunction(GR_CMP_ALWAYS);
    AlphaTestReferenceValue(0);
    ChromakeyMode(GR_CHROMAKEY_DISABLE);
    ConstantColorValue(~0u);
    ClipWindow(0, 0, g_board.screenW, g_board.screenH);
    s.ccDelta0 = 0;
    ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED,
                 GR_COMBINE_OTHER_ITERATED, FXFALSE);
    AlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_CONSTANT,
                 GR_COMBINE_OTHER_CONSTANT, FXFALSE);
    ColorMask(FXTRUE, FXFALSE);
    CullMode(GR_CULL_DISABLE);
    DepthBiasLevel(0);
    DepthMask(FXFALSE);
    DepthBufferMode(GR_DEPTHBUFFER_DISABLE);
    DepthBufferFunction(GR_CMP_LESS);
    DitherMode(GR_DITHER_4x4);
    FogMode(GR_FOG_DISABLE);
    FogColorValue(0);
    for (int t = g_board.numTmu - 1; t >= 0; t--) {
        TexClampMode(t, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_CLAMP);
        TexDetailControl(t, 0, 1, 1.0f);
        TexFilterMode(t, GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_POINT_SAMPLED);
        TexLodBiasValue(t, 0.0f);
        TexMipMapMode(t, GR_MIPMAP_DISABLE, FXFALSE);
        TexCombine(t, GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO, GR_COMBINE_FUNCTION_ZERO,
                   GR_COMBINE_FACTOR_ZERO, FXFALSE, FXFALSE);
    }
    LfbConstantAlpha(0xFF);
    LfbConstantDepth(0);
}

}  // namespace st
}  // namespace gl2d9
