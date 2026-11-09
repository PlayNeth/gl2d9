// The renderer's side of the linear frame buffer: reading the buffers, writing pixels.
#include "renderint.h"

#include <emmintrin.h>
#include <algorithm>
#include <string>

#include "shader.h"

namespace gl2d9 {

// ---------------------------------------------------------------- the linear frame buffer

namespace {

// Glide's 16-bit depth value from a depth as drawn (ClearDepth): Z or W, the same.
FxU16 GlideDepth(float z) { return FxU16(std::clamp(z * 65536.0f, 0.0f, 65535.0f)); }

// A plain color write: alpha carries "written".
const char* kCopyPs =
    "sampler2D src : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { float4 c = tex2D(src, uv); clip(c.a - 0.5); return c; }\n";

const char* kAuxReadPs =
    "sampler2D depth : register(s0);\n"
    "float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(depth, uv).r; }\n";

// The shader of an LFB write: the color (s0) and the depth (s1) as uploaded, the pixels not
// written discarded; through the pixel pipeline, chroma keying and fog as the registers say
// (the color combine is bypassed, as the Voodoo's LFB writes do).
enum : unsigned { kLfbDepthOut = 1u << 16, kLfbChroma = 1u << 17, kLfbPipeline = 1u << 18, kLfbDstAlpha = 1u << 19 };
IDirect3DPixelShader9* LfbShader(uint64_t key) {
    auto it = g_r.lfbShaders.find(key);
    if (it != g_r.lfbShaders.end()) return it->second;
    const FxU32 f = key & 0xFFFF;          // the fog bits (fogMode's low ones)
    const FxU32 blend = FxU32(key >> 32);  // alphaMode's test and factors: blended here (or tested only)
    std::string s =
        "sampler2D col : register(s0);\nsampler2D aux : register(s1);\nsampler2D fogTab : register(s2);\n"
        "sampler2D dstTex : register(s3);\n"
        "float4 fogColor : register(c2);\nfloat4 chromaKey : register(c3);\nfloat4 target : register(c10);\n"
        "struct Out { float4 c : COLOR; float z : DEPTH; };\n";
    const bool fullBlend = (blend & SST_RGBDSTFACT) != 0;  // (else the alpha test alone, or nothing)
    s += fullBlend ? "Out main(float2 uv : TEXCOORD0, float2 vpos : VPOS) {\n" : "Out main(float2 uv : TEXCOORD0) {\n";
    s += "  Out o;\n"
         "  float4 d = tex2D(aux, uv);\n"
         "  clip(d.a - 0.5);\n"
         "  float4 t = tex2D(col, uv);\n"
         "  float3 c = t.rgb, prefog = c;\n"
         "  float a = t.a;\n";
    if (key & kLfbChroma) s += "  if (all(abs(c - chromaKey.rgb) < 0.5 / 255)) discard;\n";
    // The fog table indexed with the pixel's depth value: its top 6 bits the entry, the rest
    // the step to the next (MAME's LFB pipeline: wfloat is the written depth). For a W buffer
    // that is the inverse of the triangles' 1/w to depth encoding.
    if (key & kLfbPipeline) s += FogCode(f, "a", "d.g", "  float idx = clamp(d.b / 1024, 0, 63);\n");
    if (fullBlend)
        s += BlendCode(blend, DstExpr("tex2D(dstTex, (vpos + 0.5) * target.zw)", (key & kLfbDstAlpha) != 0).c_str());
    else if (blend) s += AlphaTestCode(blend, "round(a * 255)");
    s += "  o.c = float4(c, a);\n  o.z = d.r;\n  return o;\n}\n";
    IDirect3DPixelShader9* ps = CompileFixed(g_r.dev, s.c_str());
    g_r.lfbShaders[key] = ps;
    return ps;
}

}  // namespace

bool RenderReadBuffer(int buffer, std::vector<FxU32>* out) {
    if (!RenderReady()) return false;
    RenderFlush();
    if (!DeviceUsable()) return false;
    const int W = g_board.screenW, H = g_board.screenH, s = g_r.scale;
    IDirect3DDevice9* d = g_r.dev;
    IDirect3DSurface9* rt = Target(buffer);
    if (!rt) return false;
    EndDraw();
    if (g_r.readGen == g_r.gen && g_r.readSurf == rt && g_r.readCache.size() == size_t(W) * H) {
        *out = g_r.readCache;
        return true;
    }
    // Scaled: shrunk to the game's size on the GPU (point: each pixel's middle sample), so the
    // copy back is the game's size, not scale^2 times it.
    IDirect3DSurface9* src = rt;
    IDirect3DSurface9* sys = nullptr;
    if (s > 1) {
        if (!g_r.readSmall && FAILED(d->CreateRenderTarget(W, H, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE,
                                                           &g_r.readSmall, nullptr)))
            g_r.readSmall = nullptr;
        if (!g_r.readSmallSys && FAILED(d->CreateOffscreenPlainSurface(W, H, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                                       &g_r.readSmallSys, nullptr)))
            g_r.readSmallSys = nullptr;
        if (g_r.readSmall && g_r.readSmallSys &&
            SUCCEEDED(d->StretchRect(rt, nullptr, g_r.readSmall, nullptr, D3DTEXF_POINT))) {
            src = g_r.readSmall;
            sys = g_r.readSmallSys;
        }
    }
    if (!sys) {
        if (!g_r.readback && FAILED(d->CreateOffscreenPlainSurface(g_r.w, g_r.h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM,
                                                                   &g_r.readback, nullptr))) {
            g_r.readback = nullptr;
            return false;
        }
        sys = g_r.readback;
    }
    if (FAILED(d->GetRenderTargetData(src, sys))) return false;
    D3DLOCKED_RECT lr;
    if (FAILED(sys->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    const int step = sys == g_r.readback ? s : 1;  // (the full-size copy: each pixel's middle)
    out->resize(size_t(W) * H);
    for (int y = 0; y < H; y++) {
        const FxU32* row = reinterpret_cast<const FxU32*>(static_cast<const uint8_t*>(lr.pBits) +
                                                          size_t(y * step + step / 2) * lr.Pitch);
        FxU32* o = out->data() + size_t(y) * W;
        if (step == 1) memcpy(o, row, size_t(W) * 4);
        else
            for (int x = 0; x < W; x++) o[x] = row[x * step + step / 2];
    }
    sys->UnlockRect();
    g_r.readCache = *out;
    g_r.readGen = g_r.gen;
    g_r.readSurf = rt;
    return true;
}

bool RenderReadAux(std::vector<FxU16>* out) {
    if (!RenderReady()) return false;
    const int W = g_board.screenW, H = g_board.screenH;
    const FxU32 fbz = St().r.fbzMode;
    if (fbz & SST_ENALPHABUFFER) {  // the alpha buffer: the back buffer's alpha
        std::vector<FxU32> px;
        if (!RenderReadBuffer(GR_BUFFER_BACKBUFFER, &px)) return false;
        out->resize(px.size());
        for (size_t i = 0; i < px.size(); i++) (*out)[i] = FxU16(px[i] >> 24);
        return true;
    }
    RenderFlush();
    if (!DeviceUsable()) return false;
    if (g_r.auxGen == g_r.gen && g_r.auxCache.size() == size_t(W) * H) {
        *out = g_r.auxCache;
        return true;
    }
    out->assign(size_t(W) * H, 0);
    IDirect3DDevice9* d = g_r.dev;
    if (!g_r.depthTex) {
        GL_STUB("reading the depth buffer (no INTZ depth textures: read as 0)");
        return true;
    }
    if (!g_r.auxReadPs) g_r.auxReadPs = CompileFixed(d, kAuxReadPs);
    // Drawn into floats at the game's size (point sampling: each pixel's middle sample).
    if (!g_r.auxReadPs || !MakeTexture(&g_r.auxRead, W, H, D3DUSAGE_RENDERTARGET, D3DFMT_R32F, D3DPOOL_DEFAULT))
        return false;
    if (!g_r.auxReadback &&
        FAILED(d->CreateOffscreenPlainSurface(W, H, D3DFMT_R32F, D3DPOOL_SYSTEMMEM, &g_r.auxReadback, nullptr))) {
        g_r.auxReadback = nullptr;
        return false;
    }
    // The depth texture drawn into floats (it can't be the depth buffer meanwhile).
    IDirect3DSurface9* rt = nullptr;
    g_r.auxRead->GetSurfaceLevel(0, &rt);
    UseTarget(rt);
    UseDepth(nullptr);
    const D3DVIEWPORT9 vp = {0, 0, DWORD(W), DWORD(H), 0.0f, 1.0f};
    UseViewport(vp);
    SetPassState();
    UseShader(g_r.auxReadPs);
    BindStage(0, g_r.depthTex);
    PointSampler(0);
    DrawQuad(RECT{0, 0, W, H}, 0.0f);
    BindStage(0, nullptr);
    g_r.batchState = false;
    EndDraw();
    const HRESULT hr = rt ? d->GetRenderTargetData(rt, g_r.auxReadback) : E_FAIL;
    if (rt) rt->Release();
    if (FAILED(hr)) return false;
    D3DLOCKED_RECT lr;
    if (FAILED(g_r.auxReadback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return false;
    for (int y = 0; y < H; y++) {
        const float* row = reinterpret_cast<const float*>(static_cast<const uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch);
        FxU16* o = out->data() + size_t(y) * W;
        for (int x = 0; x < W; x++) o[x] = GlideDepth(row[x]);
    }
    g_r.auxReadback->UnlockRect();
    g_r.auxCache = *out;
    g_r.auxGen = g_r.gen;
    return true;
}

void RenderWritePixels(int buffer, int x, int y, int w, int h, const FxU32* argb, const FxU16* aux, const FxU8* mask,
                       bool pipeline) {
    if (!RenderReady() || w <= 0 || h <= 0 || (!argb && !aux)) return;
    RenderFlush();
    if (!DeviceUsable()) return;
    Dirty();
    IDirect3DDevice9* d = g_r.dev;
    const Regs& r = St().r;
    const bool alphaBuf = (r.fbzMode & SST_ENALPHABUFFER) != 0;
    if (!argb) pipeline = false;  // (the aux buffer alone: written directly)
    // The depth each pixel is tested and written with: its own, or (the pipeline without
    // one) the constant depth of grLfbConstantDepth.
    const bool depthData = aux && !alphaBuf;
    // A plain color write (movies, menus: often every frame): the color alone, "written" in
    // its alpha. Otherwise the depth (and "written") beside it in floats.
    const bool plain = !pipeline && !aux;
    if (!g_r.upload || g_r.uploadW < w || g_r.uploadH < h) {
        if (g_r.upload) g_r.upload->Release(), g_r.upload = nullptr;
        if (g_r.uploadAux) g_r.uploadAux->Release(), g_r.uploadAux = nullptr;
        g_r.uploadW = std::max(w, g_board.screenW);
        g_r.uploadH = std::max(h, g_board.screenH);
    }
    if (!MakeTexture(&g_r.upload, g_r.uploadW, g_r.uploadH, D3DUSAGE_DYNAMIC, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT) ||
        (!plain && !MakeTexture(&g_r.uploadAux, g_r.uploadW, g_r.uploadH, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F,
                                D3DPOOL_DEFAULT)))
        return;
    D3DLOCKED_RECT lc, la = {};
    if (FAILED(g_r.upload->LockRect(0, &lc, nullptr, D3DLOCK_DISCARD))) return;
    if (!plain && FAILED(g_r.uploadAux->LockRect(0, &la, nullptr, D3DLOCK_DISCARD))) {
        g_r.upload->UnlockRect(0);
        return;
    }
    const FxU16 constDepth = FxU16(St().lfbConstantDepth);
    const float constAux[3] = {ClearDepth(constDepth), constDepth / 65535.0f, float(constDepth)};
    for (int j = 0; j < h; j++) {
        FxU32* crow = reinterpret_cast<FxU32*>(static_cast<uint8_t*>(lc.pBits) + size_t(j) * lc.Pitch);
        const size_t row = size_t(j) * w;
        if (plain) {  // (four at a time: a movie writes the whole frame every frame)
            const FxU32* src = argb + row;
            const FxU8* m = mask ? mask + row : nullptr;
            const __m128i opaque = _mm_set1_epi32(int(0xFF000000u)), zero = _mm_setzero_si128();
            int i = 0;
            for (; i + 4 <= w; i += 4) {
                __m128i v = _mm_or_si128(_mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i)), opaque);
                if (m) {
                    FxU32 m4;
                    memcpy(&m4, m + i, 4);
                    const __m128i written =
                        _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(int(m4)), zero), zero);
                    v = _mm_andnot_si128(_mm_cmpeq_epi32(written, zero), v);  // not written: 0
                }
                _mm_storeu_si128(reinterpret_cast<__m128i*>(crow + i), v);
            }
            for (; i < w; i++) crow[i] = m && !m[i] ? 0 : src[i] | 0xFF000000u;
            continue;
        }
        float* arow = reinterpret_cast<float*>(static_cast<uint8_t*>(la.pBits) + size_t(j) * la.Pitch);
        for (int i = 0; i < w; i++) {
            const size_t k = row + i;
            FxU32 c = argb ? argb[k] : 0;
            if (aux && alphaBuf) c = (c & 0xFFFFFF) | FxU32(aux[k] & 0xFF) << 24;
            crow[i] = c;
            float* a = arow + size_t(i) * 4;
            if (depthData) {
                const FxU16 z = aux[k];
                a[0] = ClearDepth(z), a[1] = z / 65535.0f, a[2] = float(z);
            } else {
                a[0] = constAux[0], a[1] = constAux[1], a[2] = constAux[2];
            }
            a[3] = mask && !mask[k] ? 0.0f : 1.0f;
        }
    }
    if (!plain) g_r.uploadAux->UnlockRect(0);
    g_r.upload->UnlockRect(0);
    if (pipeline) ApplyFixedState();  // the registers' state (the pipeline's), the rest set below
    else ApplyTargets(r);
    g_r.batchState = false;
    UseTarget(Target(buffer == GR_BUFFER_FRONTBUFFER ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER));
    DWORD colorMask = 0;
    if (argb) colorMask |= D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
    if (aux && alphaBuf) colorMask |= D3DCOLORWRITEENABLE_ALPHA;
    uint64_t key = 0;
    bool shaderBlend = false;
    if (pipeline) {
        // The registers decide: blending, the alpha and depth tests, the write masks, clipping.
        DWORD m = 0;
        d->GetRenderState(D3DRS_COLORWRITEENABLE, &m);
        RenderState(D3DRS_COLORWRITEENABLE, m & (colorMask | D3DCOLORWRITEENABLE_ALPHA));
        // PREFOG_COLOR with fog: blended in the shader against a copy of the target under the
        // rectangle (ApplyFixedState made the copy texture, and turned Direct3D's blending and alpha
        // test off), as triangles are.
        shaderBlend = BlendInShader(r) && g_r.dstCopy;
        if (shaderBlend) {
            key |= uint64_t(r.alphaMode & (SST_ENALPHAFUNC | SST_ALPHAFUNC | SST_RGBSRCFACT | SST_RGBDSTFACT |
                                           SST_ASRCFACT | SST_ADSTFACT))
                       << 32 |
                   (alphaBuf ? kLfbDstAlpha : 0u);
        } else if ((r.alphaMode & SST_ENALPHAFUNC) && (r.alphaMode & SST_ALPHAFUNC) != SST_ALPHAFUNC) {
            key |= uint64_t(r.alphaMode & (SST_ENALPHAFUNC | SST_ALPHAFUNC)) << 32;  // the alpha test alone, here
        }
        RenderState(D3DRS_DEPTHBIAS, 0);
        key |= kLfbPipeline | kLfbDepthOut | (r.fogMode & 0xFFFF) |
               ((r.fbzMode & SST_ENCHROMAKEY) ? unsigned(kLfbChroma) : 0u);
    } else {
        RenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        RenderState(D3DRS_ALPHATESTENABLE, FALSE);
        RenderState(D3DRS_COLORWRITEENABLE, colorMask);
        RenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        RenderState(D3DRS_ZENABLE, depthData ? D3DZB_TRUE : D3DZB_FALSE);
        RenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
        RenderState(D3DRS_ZWRITEENABLE, depthData);
        RenderState(D3DRS_DEPTHBIAS, 0);
        key = kLfbDepthOut;
    }
    if (plain && !g_r.copyPs) g_r.copyPs = CompileFixed(d, kCopyPs);
    IDirect3DPixelShader9* ps = plain ? g_r.copyPs : LfbShader(key);
    if (!ps) return;  // (fixed function would draw the upload as it is)
    UseShader(ps);
    BindStage(0, g_r.upload);
    BindStage(1, plain ? nullptr : g_r.uploadAux);
    PointSampler(0);
    PointSampler(1);
    const int s = g_r.scale;
    const RECT rc = {x * s, y * s, (x + w) * s, (y + h) * s};
    if (shaderBlend) {
        IDirect3DSurface9* t = Target(buffer == GR_BUFFER_FRONTBUFFER ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER);
        IDirect3DSurface9* copy = nullptr;
        EndDraw();
        if (t && SUCCEEDED(g_r.dstCopy->GetSurfaceLevel(0, &copy))) {
            d->StretchRect(t, &rc, copy, &rc, D3DTEXF_NONE);
            copy->Release();
        }
        BindStage(3, g_r.dstCopy);
        PointSampler(3);
    } else {
        BindStage(3, nullptr);
    }
    DrawQuad(rc, 0.0f, 0, 0, float(w) / g_r.uploadW, float(h) / g_r.uploadH);
    BindStage(0, nullptr);
    BindStage(1, nullptr);
    BindStage(3, nullptr);
    // Shown at once when it went to the front buffer.
    if (buffer == GR_BUFFER_FRONTBUFFER) Present();
}

}  // namespace gl2d9
