// The Voodoo's pixel pipeline as Direct3D 9 pixel shaders: the texture combine units of the
// TMUs, the color and alpha combine units, chroma keying and fog, generated in HLSL from the
// register bits that select them and compiled once per combination (d3dcompiler,
// shadercache.cpp).
#include "shaderint.h"

#include <string>

namespace gl2d9 {

namespace {

// One combine unit (the color, alpha or texture ones share their structure): out =
// (zero_other ? 0 : other) [- local if sub_clocal], times the factor (reversed unless
// reverse_blend), plus local or local alpha, clamped, inverted. `f` is the factor's
// expression.
std::string Unit(const char* other, const char* local, const char* localAlpha, const std::string& f, bool zeroOther,
                 bool subLocal, bool reverseBlend, bool addLocal, bool addLocalAlpha, bool invert) {
    std::string c = zeroOther ? "0" : other;
    if (subLocal) c = "(" + c + " - " + local + ")";
    const std::string fac = reverseBlend ? f : "(1 - " + f + ")";
    c = c + " * " + fac;
    if (addLocal) c += std::string(" + ") + local;
    else if (addLocalAlpha) c += std::string(" + ") + localAlpha;
    c = "saturate(" + c + ")";
    if (invert) c = "(1 - " + c + ")";
    return c;
}

// The texture combine of a TMU: local = its texel, other = the TMU upstream ("o").
std::string TmuCombine(int t, FxU32 m, const std::string& otherVar) {
    const std::string L = "l" + std::to_string(t);  // its texel
    auto factor = [&](FxU32 sel, bool alpha) -> std::string {
        switch (sel) {
        case 0: return "0";
        case 1: return alpha ? L + ".a" : L + ".rgb";  // local
        case 2: return otherVar + ".a";                // other alpha
        case 3: return L + ".a";                       // local alpha
        case 4: return "detail" + std::to_string(t);   // detail factor
        case 5: return "lodfrac" + std::to_string(t);  // LOD fraction
        default: return "0";
        }
    };
    const std::string rgb =
        Unit((otherVar + ".rgb").c_str(), (L + ".rgb").c_str(), (L + ".aaa").c_str(),
             factor((m & SST_TC_MSELECT) >> SST_TC_MSELECT_SHIFT, false), m & SST_TC_ZERO_OTHER, m & SST_TC_SUB_CLOCAL,
             m & SST_TC_REVERSE_BLEND, m & SST_TC_ADD_CLOCAL, m & SST_TC_ADD_ALOCAL, m & SST_TC_INVERT_OUTPUT);
    const std::string a = Unit((otherVar + ".a").c_str(), (L + ".a").c_str(), (L + ".a").c_str(),
                               factor((m & SST_TCA_MSELECT) >> SST_TCA_MSELECT_SHIFT, true), m & SST_TCA_ZERO_OTHER,
                               m & SST_TCA_SUB_CLOCAL, m & SST_TCA_REVERSE_BLEND, m & SST_TCA_ADD_CLOCAL,
                               m & SST_TCA_ADD_ALOCAL, m & SST_TCA_INVERT_OUTPUT);
    return "float4((float3)(" + rgb + "), " + a + ")";  // (the rgb part may come out scalar: "0")
}

// Whether a TMU's combine reads the TMU upstream of it.
bool UsesOther(FxU32 m) {
    const FxU32 tc = (m & SST_TC_MSELECT) >> SST_TC_MSELECT_SHIFT, ta = (m & SST_TCA_MSELECT) >> SST_TCA_MSELECT_SHIFT;
    return !(m & SST_TC_ZERO_OTHER) || !(m & SST_TCA_ZERO_OTHER) || tc == 2 || ta == 2;
}
// Whether it reads its own texel (otherwise its texture needn't be sampled).
bool UsesLocal(FxU32 m) {
    const FxU32 tc = (m & SST_TC_MSELECT) >> SST_TC_MSELECT_SHIFT, ta = (m & SST_TCA_MSELECT) >> SST_TCA_MSELECT_SHIFT;
    return (m & (SST_TC_SUB_CLOCAL | SST_TC_ADD_CLOCAL | SST_TC_ADD_ALOCAL | SST_TCA_SUB_CLOCAL | SST_TCA_ADD_CLOCAL |
                 SST_TCA_ADD_ALOCAL)) ||
           tc == 1 || tc == 3 || ta == 1 || ta == 3;
}

}  // namespace

// The alpha test and blending in the shader (BlendInShader): on c (float3) and a, the color
// and alpha drawn, prefog (float3) the color before fog, and the target's pixel dstExpr.
// The destination as blending reads it: the Voodoo has a destination alpha only with alpha
// planes; without, its blending takes 0xFF (MAME's alpha_blend), not the target's alpha
// channel, which isn't written then.
std::string DstExpr(const char* sample, bool alphaBuffer) {
    return alphaBuffer ? std::string(sample) : "float4(" + std::string(sample) + ".rgb, 1)";
}

// The alpha test on `alpha8`, the 8-bit alpha (0..255), against the reference (target.y).
std::string AlphaTestCode(FxU32 am, const char* alpha8) {
    const FxU32 func = (am & SST_ALPHAFUNC) >> SST_ALPHAFUNC_SHIFT;
    static const char* kCmp[] = {"false", "<", "==", "<=", ">", "!=", ">=", "true"};
    if (!(am & SST_ENALPHAFUNC) || func == 7) return {};
    if (func == 0) return "  discard;\n";
    return std::string("  if (!(") + alpha8 + " " + kCmp[func] + " target.y)) discard;\n";
}

std::string BlendCode(FxU32 am, const char* dstExpr) {
    // The alpha test (Direct3D's would test the blended alpha), on the 8-bit alpha (a is one
    // already: the triangles' ai / 255, the frame buffer writes' read from 8 bits).
    std::string s = AlphaTestCode(am, "round(a * 255)");
    s += std::string("  float4 dst = ") + dstExpr + ";\n";
    auto factor = [](FxU32 f, bool src, bool alpha) -> std::string {
        const char* one = alpha ? "1" : "float3(1, 1, 1)";
        switch (f) {
        case 0: return alpha ? "0" : "float3(0, 0, 0)";
        case 1: return alpha ? "a" : "a.xxx";
        case 2: return src ? (alpha ? "dst.a" : "dst.rgb") : (alpha ? "a" : "c");
        case 3: return alpha ? "dst.a" : "dst.aaa";
        case 4: return one;
        case 5: return alpha ? "(1 - a)" : "(1 - a).xxx";
        case 6: return src ? (alpha ? "(1 - dst.a)" : "(1 - dst.rgb)") : (alpha ? "(1 - a)" : "(1 - c)");
        case 7: return alpha ? "(1 - dst.a)" : "(1 - dst.aaa)";
        case 15:
            if (src) return alpha ? "1" : "min(a, 1 - dst.a).xxx";  // ALPHA_SATURATE
            return alpha ? "a" : "prefog";                          // PREFOG_COLOR
        default: return alpha ? "0" : "float3(0, 0, 0)";
        }
    };
    const std::string sf = factor((am & SST_RGBSRCFACT) >> SST_RGBSRCFACT_SHIFT, true, false);
    const std::string df = factor((am & SST_RGBDSTFACT) >> SST_RGBDSTFACT_SHIFT, false, false);
    const std::string saf = factor((am & SST_ASRCFACT) >> SST_ASRCFACT_SHIFT, true, true);
    const std::string daf = factor((am & SST_ADSTFACT) >> SST_ADSTFACT_SHIFT, false, true);
    s += "  c = saturate(c * " + sf + " + dst.rgb * " + df + ");\n";
    s += "  a = saturate(a * " + saf + " + dst.a * " + daf + ");\n";
    return s;
}

// Fog on c: Afog*Cfog + (1-Afog)*color; MULT2 keeps the first term only, ADD2 the second.
std::string FogCode(FxU32 f, const char* alpha, const char* z, const char* tableIndex) {
    if (!(f & SST_ENFOGGING)) return std::string();
    if (f & SST_FOG_CONSTANT) return "  c = saturate(c + fogColor.rgb);\n";
    std::string s;
    if (f & SST_FOG_ALPHA) s += std::string("  float fa = ") + alpha + ";\n";
    else if (f & SST_FOG_Z) s += std::string("  float fa = ") + z + ";\n";
    else s += std::string(tableIndex) + "  float fa = tex2D(fogTab, float2((idx + 0.5) / 64, 0.5)).a;\n";
    if ((f & SST_FOGMULT) && !(f & SST_FOGADD)) s += "  c = fa * fogColor.rgb;\n";
    else if ((f & SST_FOGADD) && !(f & SST_FOGMULT)) s += "  c = (1 - fa) * c;\n";
    else if (f & SST_FOGMULT) s += "  c = 0;\n";
    else s += "  c = lerp(c, fogColor.rgb, fa);\n";
    return s;
}

std::string Generate(const ShaderKey& k) {
    const FxU32 p = k.colorPath;
    const bool texturing = (p & SST_ENTEXTUREMAP) != 0;
    std::string s;
    s += "sampler2D tex0 : register(s0);\nsampler2D tex1 : register(s1);\nsampler2D fogTab : register(s2);\n";
    s += "float4 color0 : register(c0);\nfloat4 color1 : register(c1);\nfloat4 fogColor : register(c2);\n";
    s += "float4 chromaKey : register(c3);\nfloat4 texScale0 : register(c4);\nfloat4 texScale1 : register(c5);\n";
    s += "float4 detailK0 : register(c6);\nfloat4 detailK1 : register(c7);\nfloat4 delta0 : register(c8);\n";
    s += "float4 stipple : register(c9);\nfloat4 target : register(c10);\nfloat4 depthK : register(c11);\n";
    s += "sampler2D dstTex : register(s3);\n";
    if (k.stipple == 2) s += "sampler2D stipTab : register(s4);\nfloat4 stipRows : register(c12);\n";
    s += "struct In { float4 col : TEXCOORD0; float4 st0 : TEXCOORD1; float4 st1 : TEXCOORD2; float4 misc : TEXCOORD3; "
         "};\n";
    s += "struct Out { float4 c : COLOR; float z : DEPTH; };\n";
    const bool depthOut = k.depthConst || k.depthOut;
    s += depthOut ? "Out" : "float4";
    s += (k.stipple || (k.blend & SST_RGBDSTFACT)) ? " main(In i, float2 vpos : VPOS)" : " main(In i)";
    s += depthOut ? " {\n" : " : COLOR {\n";
    if (k.stipple) {
        // The pixel's bit of the stipple register (MAME's Voodoo: pattern bit (y & 3) * 8 +
        // (~x & 7)); the rotated mode, which steps through it per pixel drawn, as the pattern
        // along the row.
        s += "  float2 px = floor(vpos * target.x);\n";
        if (k.stipple == 1) {
            s += "  float sbyte = dot(stipple, float4(fmod(px.y, 4) == float4(0, 1, 2, 3)));\n";
            s += "  float sbit = 7 - fmod(px.x, 8);\n";
        } else {
            // Rotate mode, exact (render.cpp's StippleRows): the CPU walked the triangle's rows as
            // the Voodoo rasterizes them; each row's span and the register's position at its
            // first pixel are in the table (s4), the triangle's first entry and first scanline
            // in st0.w and st1.w. Pixels outside the Voodoo's span aren't drawn (its edges).
            s += "  float scan = lerp(px.y, stipRows.x - px.y, stipRows.y);\n";
            s += "  float idx = floor(i.st0.w + 0.5) + scan - floor(i.st1.w + 0.5);\n";
            s += "  float4 row = tex2D(stipTab, (float2(fmod(idx, 2048), floor(idx / 2048)) + 0.5) * stipRows.zw);\n";
            s += "  if (px.x < row.x || px.x >= row.y) discard;\n";
            s += "  float sidx = row.z + row.w * (px.x - row.x);\n";  // (z: the first pixel's bit, w: its step)
            s += "  sidx -= 32 * floor(sidx / 32);\n";
            s += "  float sbyte = dot(stipple, float4(floor(sidx / 8) == float4(0, 1, 2, 3)));\n";
            s += "  float sbit = fmod(sidx, 8);\n";
        }
        s += "  if (fmod(floor(sbyte / exp2(sbit)), 2) < 0.5) discard;\n";
    }
    // Iterated color (RGBAZ_CLAMP: clamped); the constant one of ITRGB_DELTA0 replaces it.
    if (k.delta0) s += "  float4 it = float4(saturate(delta0.rgb), saturate(i.col.a / 255));\n";
    else s += "  float4 it = saturate(i.col / 255);\n";
    s += "  float itz = saturate(i.misc.y);\n";  // iterated Z (top bits), 0..1
    s += "  float4 texel = 0;\n";
    if (texturing) {
        const int n = k.tmus;
        // Upstream first: TMU1 feeds TMU0's "other".
        std::string up = "float4(0,0,0,0)";
        for (int t = n - 1; t >= 0; t--) {
            const FxU32 m = k.tmuMode[t];
            const std::string T = std::to_string(t);
            const std::string st = t == 0 ? "i.st0" : "i.st1";
            s += "  float2 uv" + T + " = " + st + ".xy / " + st + ".z * texScale" + T + ".xy;\n";
            if (ShaderUsesTexture(k, t)) s += "  float4 l" + T + " = tex2D(tex" + T + ", uv" + T + ");\n";
            else s += "  float4 l" + T + " = 0;\n";
            // LOD (in levels of the 256-texel space) for the detail and LOD-fraction factors.
            s += "  float2 dx" + T + " = ddx(uv" + T + " * texScale" + T + ".zw), dy" + T + " = ddy(uv" + T +
                 " * texScale" + T + ".zw);\n";
            s += "  float lod" + T + " = 0.5 * log2(max(dot(dx" + T + ", dx" + T + "), dot(dy" + T + ", dy" + T +
                 ")) + 1e-10);\n";
            s += "  float lodfrac" + T + " = frac(lod" + T + ");\n";
            // Detail: (bias - lod) << scale, at most detail max.
            s += "  float detail" + T + " = min(detailK" + T + ".z, saturate((detailK" + T + ".x - lod" + T +
                 ") * detailK" + T + ".y));\n";
            s += "  float4 o" + T + " = " + up + ";\n";
            s += "  float4 r" + T + " = " + TmuCombine(t, m, "o" + T) + ";\n";
            up = "r" + T;
        }
        s += "  texel = r0;\n";
    }
    // Color combine. Local: iterated or color0, or chosen by the texel's alpha (MSB) when
    // the alpha controls the lighting.
    if (p & SST_LOCALSELECT_OVERRIDE_WITH_ATEX) s += "  float3 cl = texel.a >= 0.5 ? color0.rgb : it.rgb;\n";
    else s += std::string("  float3 cl = ") + ((p & SST_LOCALSELECT) ? "color0.rgb" : "it.rgb") + ";\n";
    const FxU32 rgbSel = (p & SST_RGBSELECT) >> SST_RGBSELECT_SHIFT, aSel = (p & SST_ASELECT) >> SST_ASELECT_SHIFT;
    const FxU32 aLocSel = (p & SST_ALOCALSELECT) >> SST_ALOCALSELECT_SHIFT;
    s += std::string("  float3 co = ") + (rgbSel == 0 ? "it.rgb" : rgbSel == 1 ? "texel.rgb" : "color1.rgb") + ";\n";
    s += std::string("  float al = ") + (aLocSel == 0 ? "it.a" : aLocSel == 1 ? "color0.a" : "itz") + ";\n";
    s += std::string("  float ao = ") + (aSel == 0 ? "it.a" : aSel == 1 ? "texel.a" : "color1.a") + ";\n";
    if (k.chromaKey) s += "  if (all(abs(co - chromaKey.rgb) < 0.5 / 255)) discard;\n";
    auto ccFactor = [&](FxU32 sel) -> std::string {
        switch (sel) {
        case 0: return "0";
        case 1: return "cl";
        case 2: return "ao";
        case 3: return "al";
        case 4: return "texel.a";
        case 5: return "texel.rgb";  // Voodoo 2: the texture's RGB
        default: return "0";
        }
    };
    auto caFactor = [&](FxU32 sel) -> std::string {
        switch (sel) {
        case 0: return "0";
        case 1: return "al";
        case 2: return "ao";
        case 3: return "al";
        case 4: return "texel.a";
        default: return "0";
        }
    };
    s += "  float3 c = " +
         Unit("co", "cl", "al.xxx", ccFactor((p & SST_CC_MSELECT) >> SST_CC_MSELECT_SHIFT), p & SST_CC_ZERO_OTHER,
              p & SST_CC_SUB_CLOCAL, p & SST_CC_REVERSE_BLEND, p & SST_CC_ADD_CLOCAL, p & SST_CC_ADD_ALOCAL,
              p & SST_CC_INVERT_OUTPUT) +
         ";\n";
    // The Voodoo's combined alpha is an 8-bit integer, the fraction dropped and then inverted
    // (MAME's CLAMPED_ARGB, then ~a): ai. The alpha test compares it with the reference as
    // integers, here (Direct3D's compares a float with its own reference / 255: equality
    // isn't exact); blending takes it as ai / 255.
    s +=
        "  float ai = floor(" +
        Unit("ao", "al", "al", caFactor((p & SST_CCA_MSELECT) >> SST_CCA_MSELECT_SHIFT), p & SST_CCA_ZERO_OTHER,
             p & SST_CCA_SUB_CLOCAL, p & SST_CCA_REVERSE_BLEND, p & SST_CCA_ADD_CLOCAL, p & SST_CCA_ADD_ALOCAL, false) +
        " * 255 + 1e-3);\n";
    if (p & SST_CCA_INVERT_OUTPUT) s += "  ai = 255 - ai;\n";
    s += "  float a = ai / 255;\n";
    const bool fullBlend = (k.blend & SST_RGBDSTFACT) != 0;  // (blended here: PREFOG_COLOR, BlendInShader)
    if (!fullBlend && (k.blend & SST_ENALPHAFUNC)) s += AlphaTestCode(k.blend, "ai");
    if (fullBlend) s += "  float3 prefog = c;\n";
    // The 64-entry table over w (Glide's guFogTableIndexToW: entry i at 2^(3+(i>>2)) /
    // (8-(i&3))): w = 2^k * r, r in [1,2), index 4k + 8 - 8/r.
    s += FogCode(k.fogMode, "it.a", "itz",
                 "  float w = 1 / max(i.misc.x, 1e-20);\n"
                 "  float k = floor(log2(max(w, 1)));\n"
                 "  float r = max(w, 1) / exp2(k);\n"
                 "  float idx = clamp(4 * k + 8 - 8 / r, 0, 63);\n");
    if (fullBlend) s += BlendCode(k.blend, DstExpr("tex2D(dstTex, (vpos + 0.5) * target.zw)", k.dstAlpha).c_str());
    if (k.depthConst) {
        s += "  Out o;\n  o.c = float4(c, a);\n  o.z = depthK.x;\n  return o;\n}\n";
    } else if (k.depthOut) {
        // The depth buffer's 16-bit value, as the Voodoo's (MAME's): Z, the iterated Z's
        // integer part, clamped; W, from the iterated 1/w (16.32 fixed point on the chip): 0
        // from 1/w >= 1, 0xFFFF below 2^-16, else the leading zeros of the fraction (e) and the
        // 12 bits after its leading one, inverted, plus one. Then the bias, and clamped. It goes
        // to the depth buffer as is (ClearDepth): coplanar passes tie as they did on the chip.
        if (k.depthOut & DEPTH_W) {
            s += "  float oow = i.misc.z;\n";
            s += "  float e = clamp(floor(-log2(max(oow, 1.0 / 65536))), 0, 15);\n";
            s += "  float f = oow * exp2(e + 1);\n";  // in [1, 2) when e is the leading zero count
            s += "  if (f >= 2) { e -= 1; f *= 0.5; }\n  if (f < 1) { e += 1; f *= 2; }\n";
            s += "  float dv = min(e * 4096 + 4095 - floor((f - 1) * 4096) + 1, 65535);\n";
            s += "  dv = oow >= 1 ? 0 : (oow < 1.0 / 65536 ? 65535 : dv);\n";
        } else {
            s += "  float dv = clamp(floor(i.misc.w), 0, 65535);\n";
        }
        if (k.depthOut & DEPTH_BIAS) s += "  dv = clamp(dv + depthK.y, 0, 65535);\n";
        // overlaybias (dd2d9's): pulled forward N steps and the depth's change over a pixel
        // (its slope), so that rounding the surface and the overlay differently (cut into
        // other polygons) can't fail the depth test on half its pixels in a fine hatch.
        if (k.depthOut & DEPTH_OVERLAY) s += "  dv = max(dv - depthK.z - max(abs(ddx(dv)), abs(ddy(dv))), 0);\n";
        s += "  Out o;\n  o.c = float4(c, a);\n  o.z = (dv + 0.5) / 65536;\n  return o;\n}\n";
    } else s += "  return float4(c, a);\n}\n";
    return s;
}

bool ShaderKey::operator==(const ShaderKey& o) const {
    return colorPath == o.colorPath && fogMode == o.fogMode && tmuMode[0] == o.tmuMode[0] &&
           tmuMode[1] == o.tmuMode[1] && tmus == o.tmus && chromaKey == o.chromaKey && delta0 == o.delta0 &&
           stipple == o.stipple && blend == o.blend && depthConst == o.depthConst && depthOut == o.depthOut &&
           dstAlpha == o.dstAlpha;
}

namespace {
// The depth the shader writes, the pixel's own (ShaderKey::depthOut).
FxU8 DepthOutBits(const Regs& r) {
    FxU8 bits = (r.fbzMode & SST_WBUFFER) ? DEPTH_W : DEPTH_Z;
    if (r.fbzMode & SST_ENZBIAS) bits |= DEPTH_BIAS;  // (not by its value: one shader for all)
    if (IsOverlay(r)) bits |= DEPTH_OVERLAY;
    return bits;
}
}  // namespace

// The bits that select the pipeline (the rest are drawn through render states and constants).
ShaderKey MakeShaderKey(const GlideState& st, int numTmu) {
    ShaderKey k{};
    const Regs& r = st.r;
    k.colorPath = r.fbzColorPath & kColorPathKeyBits;
    k.fogMode = r.fogMode & kFogModeKeyBits;
    k.chromaKey = (r.fbzMode & SST_ENCHROMAKEY) ? 1 : 0;
    k.delta0 = st.ccDelta0 ? 1 : 0;
    if (r.fbzMode & SST_ENSTIPPLE) k.stipple = (r.fbzMode & SST_ENSTIPPLEPATTERN) ? 1 : 2;
    const FxU32 zc = SST_ENDEPTHBUFFER | SST_ZCOMPARE_TO_ZACOLOR;
    k.depthConst = (r.fbzMode & zc) == zc ? 1 : 0;
    if (!k.depthConst && (r.fbzMode & SST_ENDEPTHBUFFER)) k.depthOut = DepthOutBits(r);
    if (BlendInShader(r)) {
        k.blend = r.alphaMode & kBlendKeyBits;
        k.dstAlpha = (r.fbzMode & SST_ENALPHABUFFER) ? 1 : 0;
    } else if ((r.alphaMode & SST_ENALPHAFUNC) && (r.alphaMode & SST_ALPHAFUNC) != SST_ALPHAFUNC) {
        k.blend = r.alphaMode & (SST_ENALPHAFUNC | SST_ALPHAFUNC);  // the alpha test alone, in the shader
    }
    if (r.fbzColorPath & SST_ENTEXTUREMAP) {
        const FxU32 m0 = r.tmu[0].textureMode & kTmuModeKeyBits;
        k.tmuMode[0] = m0;
        k.tmus = 1;
        if (numTmu > 1 && UsesOther(m0)) {
            k.tmuMode[1] = r.tmu[1].textureMode & kTmuModeKeyBits;
            k.tmus = 2;
        }
    }
    return k;
}

ShaderKey DepthWriteKey(ShaderKey k, const Regs& r) {
    k.depthConst = 0;
    k.depthOut = DepthOutBits(r);
    return k;
}

bool TmuActive(FxU32 textureMode) { return UsesLocal(textureMode); }

// PREFOG_COLOR differs from the source color only with fog on.
// dd2d9's overlaybias, on Glide's blend factors: no depth write, or blending that adds or
// multiplies (the destination's factor ONE, SRC_COLOR, ONE_MINUS_SRC_COLOR or PREFOG_COLOR;
// the source's ZERO or DST_COLOR). Ordinary alpha blending doesn't count (NFS III blends
// everything).
bool IsOverlay(const Regs& r) {
    if (g_config.overlayBias <= 0 || !(r.fbzMode & SST_ENDEPTHBUFFER)) return false;
    if (!(r.fbzMode & SST_ZAWRMASK)) return true;
    if (!(r.alphaMode & SST_ENALPHABLEND)) return false;
    const FxU32 src = (r.alphaMode & SST_RGBSRCFACT) >> SST_RGBSRCFACT_SHIFT;
    const FxU32 dst = (r.alphaMode & SST_RGBDSTFACT) >> SST_RGBDSTFACT_SHIFT;
    return dst == 4 || dst == 2 || dst == 6 || dst == 15 || src == 0 || src == 2;
}

bool BlendInShader(const Regs& r) {
    return (r.alphaMode & SST_ENALPHABLEND) && ((r.alphaMode & SST_RGBDSTFACT) >> SST_RGBDSTFACT_SHIFT) == 15 &&
           (r.fogMode & SST_ENFOGGING);
}

// (The detail and LOD fraction factors need only the coordinates' derivatives, not the texel.)
bool ShaderUsesTexture(const ShaderKey& k, int tmu) { return tmu < k.tmus && UsesLocal(k.tmuMode[tmu]); }

}  // namespace gl2d9
