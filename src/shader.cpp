// The Voodoo's pixel pipeline as Direct3D 9 pixel shaders: the texture combine units of the
// TMUs, the color and alpha combine units, chroma keying and fog, generated in HLSL from the
// register bits that select them and compiled once per combination (d3dcompiler).
#include "shader.h"

#include <d3dcompiler.h>

#include <string>
#include <unordered_map>

namespace gl2d9 {

namespace {

using CompileFn = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const D3D_SHADER_MACRO*, ID3DInclude*, const char*,
                                   const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);

CompileFn Compiler() {
    static const CompileFn fn = [] {
        static const wchar_t* kNames[] = {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll",
                                          L"d3dcompiler_42.dll"};
        for (const wchar_t* n : kNames) {
            if (HMODULE m = LoadLibraryW(n)) {
                if (auto f = reinterpret_cast<CompileFn>(reinterpret_cast<void*>(GetProcAddress(m, "D3DCompile")))) {
                    GL_LOG("shaders: compiled with %ls", n);
                    return f;
                }
            }
        }
        GL_LOG("shaders: no d3dcompiler found: nothing can be drawn");
        return CompileFn(nullptr);
    }();
    return fn;
}

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
        case 1: return alpha ? L + ".a" : L + ".rgb";      // local
        case 2: return otherVar + ".a";                      // other alpha
        case 3: return L + ".a";                             // local alpha
        case 4: return "detail" + std::to_string(t);         // detail factor
        case 5: return "lodfrac" + std::to_string(t);        // LOD fraction
        default: return "0";
        }
    };
    const std::string rgb =
        Unit((otherVar + ".rgb").c_str(), (L + ".rgb").c_str(), (L + ".aaa").c_str(),
             factor((m & SST_TC_MSELECT) >> SST_TC_MSELECT_SHIFT, false), m & SST_TC_ZERO_OTHER, m & SST_TC_SUB_CLOCAL,
             m & SST_TC_REVERSE_BLEND, m & SST_TC_ADD_CLOCAL, m & SST_TC_ADD_ALOCAL, m & SST_TC_INVERT_OUTPUT);
    const std::string a =
        Unit((otherVar + ".a").c_str(), (L + ".a").c_str(), (L + ".a").c_str(),
             factor((m & SST_TCA_MSELECT) >> SST_TCA_MSELECT_SHIFT, true), m & SST_TCA_ZERO_OTHER, m & SST_TCA_SUB_CLOCAL,
             m & SST_TCA_REVERSE_BLEND, m & SST_TCA_ADD_CLOCAL, m & SST_TCA_ADD_ALOCAL, m & SST_TCA_INVERT_OUTPUT);
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
    s += "struct In { float4 col : TEXCOORD0; float4 st0 : TEXCOORD1; float4 st1 : TEXCOORD2; float4 misc : TEXCOORD3; };\n";
    s += "struct Out { float4 c : COLOR; float z : DEPTH; };\n";
    s += k.depthConst ? "Out" : "float4";
    s += (k.stipple || k.blend) ? " main(In i, float2 vpos : VPOS)" : " main(In i)";
    s += k.depthConst ? " {\n" : " : COLOR {\n";
    if (k.stipple) {
        // The pixel's bit of the stipple register (MAME's Voodoo: pattern bit (y & 3) * 8 +
        // (~x & 7)); the rotated mode, which steps through it per pixel drawn, as the pattern
        // along the row.
        s += "  float2 px = floor(vpos * target.x);\n";
        if (k.stipple == 1) {
            s += "  float sbyte = dot(stipple, float4(fmod(px.y, 4) == float4(0, 1, 2, 3)));\n";
            s += "  float sbit = 7 - fmod(px.x, 8);\n";
        } else {
            s += "  float sidx = 31 - fmod(px.x + px.y, 32);\n";
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
            if (UsesLocal(m) || ((m & SST_TC_MSELECT) >> SST_TC_MSELECT_SHIFT) >= 4 ||
                ((m & SST_TCA_MSELECT) >> SST_TCA_MSELECT_SHIFT) >= 4)
                s += "  float4 l" + T + " = tex2D(tex" + T + ", uv" + T + ");\n";
            else
                s += "  float4 l" + T + " = 0;\n";
            // LOD (in levels of the 256-texel space) for the detail and LOD-fraction factors.
            s += "  float2 dx" + T + " = ddx(uv" + T + " * texScale" + T + ".zw), dy" + T + " = ddy(uv" + T + " * texScale" + T +
                 ".zw);\n";
            s += "  float lod" + T + " = 0.5 * log2(max(dot(dx" + T + ", dx" + T + "), dot(dy" + T + ", dy" + T + ")) + 1e-10);\n";
            s += "  float lodfrac" + T + " = frac(lod" + T + ");\n";
            // Detail: (bias - lod) << scale, at most detail max.
            s += "  float detail" + T + " = min(detailK" + T + ".z, saturate((detailK" + T + ".x - lod" + T + ") * detailK" + T + ".y));\n";
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
    s += "  float a = " +
         Unit("ao", "al", "al", caFactor((p & SST_CCA_MSELECT) >> SST_CCA_MSELECT_SHIFT), p & SST_CCA_ZERO_OTHER,
              p & SST_CCA_SUB_CLOCAL, p & SST_CCA_REVERSE_BLEND, p & SST_CCA_ADD_CLOCAL, p & SST_CCA_ADD_ALOCAL,
              p & SST_CCA_INVERT_OUTPUT) +
         ";\n";
    if (k.blend) s += "  float3 prefog = c;\n";
    // Fog: Afog*Cfog + (1-Afog)*color; MULT2 keeps the first term only, ADD2 the second.
    const FxU32 f = k.fogMode;
    if (f & SST_ENFOGGING) {
        if (f & SST_FOG_CONSTANT) {
            s += "  c = saturate(c + fogColor.rgb);\n";
        } else {
            if (f & SST_FOG_ALPHA) s += "  float fa = it.a;\n";
            else if (f & SST_FOG_Z) s += "  float fa = itz;\n";
            else {
                // The 64-entry table over w (Glide's guFogTableIndexToW: entry i at
                // 2^(3+(i>>2)) / (8-(i&3))): w = 2^k * r, r in [1,2), index 4k + 8 - 8/r.
                s += "  float w = 1 / max(i.misc.x, 1e-20);\n";
                s += "  float k = floor(log2(max(w, 1)));\n";
                s += "  float r = max(w, 1) / exp2(k);\n";
                s += "  float idx = clamp(4 * k + 8 - 8 / r, 0, 63);\n";
                s += "  float fa = tex2D(fogTab, float2((idx + 0.5) / 64, 0.5)).a;\n";
            }
            if ((f & SST_FOGMULT) && !(f & SST_FOGADD)) s += "  c = fa * fogColor.rgb;\n";
            else if ((f & SST_FOGADD) && !(f & SST_FOGMULT)) s += "  c = (1 - fa) * c;\n";
            else if (f & SST_FOGMULT) s += "  c = 0;\n";
            else s += "  c = lerp(c, fogColor.rgb, fa);\n";
        }
    }
    if (k.blend) {
        const FxU32 am = k.blend;
        // The alpha test (Direct3D's would test the blended alpha), on the 8-bit alpha.
        const FxU32 func = (am & SST_ALPHAFUNC) >> SST_ALPHAFUNC_SHIFT;
        if (am & SST_ENALPHAFUNC) {
            static const char* kCmp[] = {"false", "<", "==", "<=", ">", "!=", ">=", "true"};
            if (func == 0) s += "  discard;\n";
            else if (func != 7) s += std::string("  if (!(round(a * 255) ") + kCmp[func] + " target.y)) discard;\n";
        }
        s += "  float4 dst = tex2D(dstTex, (vpos + 0.5) * target.zw);\n";
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
    }
    if (k.depthConst) s += "  Out o;\n  o.c = float4(c, a);\n  o.z = depthK.x;\n  return o;\n}\n";
    else s += "  return float4(c, a);\n}\n";
    return s;
}

struct KeyHash {
    size_t operator()(const ShaderKey& k) const {
        size_t h = k.colorPath;
        h = h * 1000003u ^ k.fogMode;
        h = h * 1000003u ^ (k.tmuMode[0] + 7 * k.tmuMode[1]);
        h = h * 1000003u ^ (k.tmus | k.chromaKey << 4 | k.delta0 << 5 | k.stipple << 6 | k.depthConst << 8);
        h = h * 1000003u ^ k.blend;
        return h;
    }
};

std::unordered_map<ShaderKey, IDirect3DPixelShader9*, KeyHash> g_cache;

// The disk cache: compiled shaders by a hash of their source (so a gl2d9 that generates other
// source never takes the old ones), in records of hash, size, bytecode after a header.
constexpr char kDiskMagic[8] = {'G', 'L', '2', 'D', '9', 'S', 'C', '1'};
struct DiskCache {
    bool loaded = false;
    std::wstring path;
    std::unordered_map<uint64_t, std::vector<DWORD>> code;
};
DiskCache g_disk;

uint64_t SourceHash(const std::string& s) {
    uint64_t h = 14695981039346656037ull;
    for (unsigned char c : s) h = (h ^ c) * 1099511628211ull;
    return h;
}

void LoadDiskCache() {
    if (g_disk.loaded) return;
    g_disk.loaded = true;
    g_disk.path = ShaderCachePath();
    if (g_disk.path.empty()) return;
    HANDLE h = CreateFileW(g_disk.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {};
    std::vector<char> data;
    // (Shaders of older gl2d9s pile up in it: past 16 MB it starts over.)
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20)) {
        data.resize(size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, data.data(), DWORD(data.size()), &got, nullptr)) got = 0;
        data.resize(got);
    }
    CloseHandle(h);
    if (data.size() < sizeof(kDiskMagic) || memcmp(data.data(), kDiskMagic, sizeof(kDiskMagic)) != 0) {
        // Another format (or empty): started over.
        DeleteFileW(g_disk.path.c_str());
        return;
    }
    size_t at = sizeof(kDiskMagic);
    while (at + 12 <= data.size()) {  // (a record cut short by a crash ends the reading)
        uint64_t hash;
        uint32_t bytes;
        memcpy(&hash, &data[at], 8);
        memcpy(&bytes, &data[at + 8], 4);
        if (bytes % 4 || bytes > data.size() - at - 12) break;
        std::vector<DWORD> code(bytes / 4);
        memcpy(code.data(), &data[at + 12], bytes);
        g_disk.code[hash] = std::move(code);
        at += 12 + bytes;
    }
    GL_LOG("shaders: %u in the cache (%ls)", unsigned(g_disk.code.size()), g_disk.path.c_str());
}

void SaveToDisk(uint64_t hash, const void* code, uint32_t bytes) {
    if (g_disk.path.empty()) return;
    HANDLE h = CreateFileW(g_disk.path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<char> rec;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(h, &size) && size.QuadPart == 0) rec.insert(rec.end(), kDiskMagic, kDiskMagic + sizeof(kDiskMagic));
    const size_t at = rec.size();
    rec.resize(at + 12 + bytes);
    memcpy(&rec[at], &hash, 8);
    memcpy(&rec[at + 8], &bytes, 4);
    memcpy(&rec[at + 12], code, bytes);
    DWORD put = 0;
    WriteFile(h, rec.data(), DWORD(rec.size()), &put, nullptr);
    CloseHandle(h);
}

}  // namespace

bool ShaderKey::operator==(const ShaderKey& o) const {
    return colorPath == o.colorPath && fogMode == o.fogMode && tmuMode[0] == o.tmuMode[0] && tmuMode[1] == o.tmuMode[1] &&
           tmus == o.tmus && chromaKey == o.chromaKey && delta0 == o.delta0 && stipple == o.stipple && blend == o.blend &&
           depthConst == o.depthConst;
}

// The bits that select the pipeline (the rest are drawn through render states and constants).
ShaderKey MakeShaderKey(const GlideState& st, int numTmu) {
    ShaderKey k{};
    const Regs& r = st.r;
    k.colorPath = r.fbzColorPath & (SST_RGBSELECT | SST_ASELECT | SST_LOCALSELECT | SST_ALOCALSELECT |
                                    SST_LOCALSELECT_OVERRIDE_WITH_ATEX | 0x03FFFF00u | SST_ENTEXTUREMAP);
    k.fogMode = r.fogMode & (SST_ENFOGGING | SST_FOGADD | SST_FOGMULT | SST_FOG_ALPHA | SST_FOG_Z | SST_FOG_CONSTANT);
    k.chromaKey = (r.fbzMode & SST_ENCHROMAKEY) ? 1 : 0;
    k.delta0 = st.ccDelta0 ? 1 : 0;
    if (r.fbzMode & SST_ENSTIPPLE) k.stipple = (r.fbzMode & SST_ENSTIPPLEPATTERN) ? 1 : 2;
    const FxU32 zc = SST_ENDEPTHBUFFER | SST_ZCOMPARE_TO_ZACOLOR;
    k.depthConst = (r.fbzMode & zc) == zc ? 1 : 0;
    if (BlendInShader(r))
        k.blend = r.alphaMode & (SST_ENALPHAFUNC | SST_ALPHAFUNC | SST_RGBSRCFACT | SST_RGBDSTFACT | SST_ASRCFACT | SST_ADSTFACT);
    if (r.fbzColorPath & SST_ENTEXTUREMAP) {
        const FxU32 m0 = r.tmu[0].textureMode & (SST_TCOMBINE | SST_TACOMBINE);
        k.tmuMode[0] = m0;
        k.tmus = 1;
        if (numTmu > 1 && UsesOther(m0)) {
            k.tmuMode[1] = r.tmu[1].textureMode & (SST_TCOMBINE | SST_TACOMBINE);
            k.tmus = 2;
        }
    }
    return k;
}

bool TmuActive(FxU32 textureMode) { return UsesLocal(textureMode); }

// PREFOG_COLOR differs from the source color only with fog on.
bool BlendInShader(const Regs& r) {
    return (r.alphaMode & SST_ENALPHABLEND) && ((r.alphaMode & SST_RGBDSTFACT) >> SST_RGBDSTFACT_SHIFT) == 15 &&
           (r.fogMode & SST_ENFOGGING);
}

bool ShaderUsesTexture(const ShaderKey& k, int tmu) {
    if (tmu >= k.tmus) return false;
    const FxU32 m = k.tmuMode[tmu];
    return UsesLocal(m) || ((m & SST_TC_MSELECT) >> SST_TC_MSELECT_SHIFT) >= 4 ||
           ((m & SST_TCA_MSELECT) >> SST_TCA_MSELECT_SHIFT) >= 4;
}

namespace {
// Bytecode for HLSL source: from the disk cache, or compiled (and added to it). Errors logged.
bool Bytecode(const std::string& src, UINT flags, std::vector<DWORD>* out) {
    LoadDiskCache();
    const uint64_t hash = SourceHash(src);
    auto cached = g_disk.code.find(hash);
    if (cached != g_disk.code.end()) {
        *out = cached->second;
        return true;
    }
    CompileFn compile = Compiler();
    if (!compile) return false;
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr = compile(src.data(), src.size(), "gl2d9", nullptr, nullptr, "main", "ps_3_0", flags, 0, &code, &errors);
    const bool ok = SUCCEEDED(hr) && code;
    if (ok) {
        const DWORD* words = static_cast<const DWORD*>(code->GetBufferPointer());
        out->assign(words, words + code->GetBufferSize() / 4);
        g_disk.code[hash] = *out;
        SaveToDisk(hash, words, uint32_t(code->GetBufferSize()));
    } else {
        GL_LOG("shader compile failed (0x%08lx): %s\n%s", static_cast<unsigned long>(hr),
               errors ? static_cast<const char*>(errors->GetBufferPointer()) : "", src.c_str());
    }
    if (code) code->Release();
    if (errors) errors->Release();
    return ok;
}

IDirect3DPixelShader9* MakeShader(IDirect3DDevice9* dev, const std::string& src, UINT flags) {
    std::vector<DWORD> code;
    IDirect3DPixelShader9* ps = nullptr;
    if (Bytecode(src, flags, &code) && FAILED(dev->CreatePixelShader(code.data(), &ps))) ps = nullptr;
    return ps;
}
}  // namespace

IDirect3DPixelShader9* GetShader(IDirect3DDevice9* dev, const ShaderKey& k) {
    auto it = g_cache.find(k);
    if (it != g_cache.end()) return it->second;
    IDirect3DPixelShader9* ps = MakeShader(dev, Generate(k), D3DCOMPILE_OPTIMIZATION_LEVEL3);
    GL_TRACE("shader %08lx %08lx %08lx %08lx tmus %d: %s", static_cast<unsigned long>(k.colorPath),
             static_cast<unsigned long>(k.fogMode), static_cast<unsigned long>(k.tmuMode[0]),
             static_cast<unsigned long>(k.tmuMode[1]), k.tmus, ps ? "made" : "failed");
    g_cache[k] = ps;  // (a failed one stays null: not compiled again)
    return ps;
}

void ReleaseShaders() {
    for (auto& e : g_cache)
        if (e.second) e.second->Release();
    g_cache.clear();
}

// A fixed shader (the presentation passes, the LFB's), through the same cache.
IDirect3DPixelShader9* CompileFixed(IDirect3DDevice9* dev, const char* src) { return MakeShader(dev, src, 0); }

}  // namespace gl2d9
