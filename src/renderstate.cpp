// The renderer's device state: what the registers describe, set on the device (and only
// what changed).
#include "renderint.h"

#include <algorithm>

#include "shader.h"
#include "texmem.h"

namespace gl2d9 {

namespace {

// What the device was last given, so that what it has already isn't sent again: a register
// change used to re-send the whole state (some seventy calls, each to the driver), where a
// game typically changes one texture or factor between batches. Everything the renderer
// sets goes through these. Forgotten where the device's state is reset or not known (a device
// made, Reset, the targets made again).
struct DeviceState {
    DWORD rs[256];
    bool rsKnown[256];
    DWORD ss[5][14];
    bool ssKnown[5][14];
    IDirect3DBaseTexture9* tex[5];
    bool texKnown[5];
    IDirect3DPixelShader9* ps;
    bool psKnown;
    float c[16][4];
    bool cKnown[16];
    IDirect3DSurface9* rt;
    IDirect3DSurface9* ds;
    bool rtKnown, dsKnown;
    D3DVIEWPORT9 vp;
    RECT scissor;
    bool vpKnown, scissorKnown;
    DWORD fvf;
    bool fvfKnown, vsKnown;
};
DeviceState g_ds;

}  // namespace

void ForgetDeviceState() { g_ds = DeviceState{}; }
void RenderState(D3DRENDERSTATETYPE s, DWORD v) {
    if (s < 256 && g_ds.rsKnown[s] && g_ds.rs[s] == v) return;
    g_r.dev->SetRenderState(s, v);
    if (s < 256) g_ds.rs[s] = v, g_ds.rsKnown[s] = true;
}
void SamplerState(DWORD stage, D3DSAMPLERSTATETYPE t, DWORD v) {
    const bool cached = stage < 5 && t < 14;
    if (cached && g_ds.ssKnown[stage][t] && g_ds.ss[stage][t] == v) return;
    g_r.dev->SetSamplerState(stage, t, v);
    if (cached) g_ds.ss[stage][t] = v, g_ds.ssKnown[stage][t] = true;
}
// (The device holds what is bound: a pointer known bound can't have been freed and reused.)
void BindStage(DWORD stage, IDirect3DBaseTexture9* tex) {
    if (stage < 5 && g_ds.texKnown[stage] && g_ds.tex[stage] == tex) return;
    g_r.dev->SetTexture(stage, tex);
    if (stage < 5) g_ds.tex[stage] = tex, g_ds.texKnown[stage] = true;
}
void UseShader(IDirect3DPixelShader9* ps) {
    if (g_ds.psKnown && g_ds.ps == ps) return;
    g_r.dev->SetPixelShader(ps);
    g_ds.ps = ps, g_ds.psKnown = true;
}
void Constants(UINT start, const float* v, UINT n) {
    if (start + n > 16) {
        g_r.dev->SetPixelShaderConstantF(start, v, n);
        for (UINT i = start; i < 16; i++) g_ds.cKnown[i] = false;
        return;
    }
    bool same = true;
    for (UINT i = 0; i < n && same; i++) same = g_ds.cKnown[start + i] && !memcmp(g_ds.c[start + i], v + i * 4, 16);
    if (same) return;
    g_r.dev->SetPixelShaderConstantF(start, v, n);
    memcpy(g_ds.c[start], v, size_t(n) * 16);
    for (UINT i = 0; i < n; i++) g_ds.cKnown[start + i] = true;
}
// (Setting render target 0 resets the viewport and the scissor rectangle to the target's.)
void UseTarget(IDirect3DSurface9* rt) {
    if (g_ds.rtKnown && g_ds.rt == rt) return;
    g_r.dev->SetRenderTarget(0, rt);
    g_ds.rt = rt, g_ds.rtKnown = true;
    g_ds.vpKnown = g_ds.scissorKnown = false;
}
void UseDepth(IDirect3DSurface9* ds) {
    if (g_ds.dsKnown && g_ds.ds == ds) return;
    g_r.dev->SetDepthStencilSurface(ds);
    g_ds.ds = ds, g_ds.dsKnown = true;
}
void UseViewport(const D3DVIEWPORT9& vp) {
    if (g_ds.vpKnown && !memcmp(&g_ds.vp, &vp, sizeof(vp))) return;
    g_r.dev->SetViewport(&vp);
    g_ds.vp = vp, g_ds.vpKnown = true;
}
void UseScissor(const RECT& rc) {
    if (g_ds.scissorKnown && EqualRect(&g_ds.scissor, &rc)) return;
    g_r.dev->SetScissorRect(&rc);
    g_ds.scissor = rc, g_ds.scissorKnown = true;
}
void UseFvf(DWORD fvf) {
    if (!g_ds.vsKnown) g_r.dev->SetVertexShader(nullptr), g_ds.vsKnown = true;
    if (g_ds.fvfKnown && g_ds.fvf == fvf) return;
    g_r.dev->SetFVF(fvf);
    g_ds.fvf = fvf, g_ds.fvfKnown = true;
}

// One scene from the first draw to whatever needs none open: presenting, reading or copying
// the targets, a Reset (a scene per draw made some drivers flush at each).
void BeginDraw() {
    if (!g_r.inScene) g_r.inScene = SUCCEEDED(g_r.dev->BeginScene());
}
void EndDraw() {
    if (g_r.inScene) g_r.dev->EndScene();
    g_r.inScene = false;
}

// A sampler stage reading texels as they are (copies, tables).
void PointSampler(int stage) {
    SamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    SamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    SamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    SamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    SamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

// A pass over a whole target of gl2d9's own (presenting, reading the depth): nothing of the
// game's state (clip window, depth, blending, alpha test, write masks) applies.
void SetPassState() {
    RenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    RenderState(D3DRS_ZENABLE, FALSE);
    RenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    RenderState(D3DRS_ALPHATESTENABLE, FALSE);
    RenderState(D3DRS_COLORWRITEENABLE, 0xF);
}

// ---------------------------------------------------------------- state from the registers

namespace {

// Glide's blend factors. The destination's alpha exists only with alpha planes (the alpha
// buffer); without, the Voodoo blends with 0xFF there (MAME's alpha_blend), and the target's
// alpha channel, never written then, mustn't be read: DST_ALPHA is one, ONE_MINUS_DST_ALPHA
// zero, ALPHA_SATURATE min(as, 1 - 1) zero.
D3DBLEND SrcBlend(FxU32 f, bool alphaBuffer) {
    switch (f) {
    case 0: return D3DBLEND_ZERO;
    case 1: return D3DBLEND_SRCALPHA;
    case 2: return D3DBLEND_DESTCOLOR;  // as a source factor 2 is the destination's color
    case 3: return alphaBuffer ? D3DBLEND_DESTALPHA : D3DBLEND_ONE;
    case 4: return D3DBLEND_ONE;
    case 5: return D3DBLEND_INVSRCALPHA;
    case 6: return D3DBLEND_INVDESTCOLOR;
    case 7: return alphaBuffer ? D3DBLEND_INVDESTALPHA : D3DBLEND_ZERO;
    case 15: return alphaBuffer ? D3DBLEND_SRCALPHASAT : D3DBLEND_ZERO;
    default: return D3DBLEND_ZERO;
    }
}
D3DBLEND DstBlend(FxU32 f, bool alphaBuffer) {
    switch (f) {
    case 0: return D3DBLEND_ZERO;
    case 1: return D3DBLEND_SRCALPHA;
    case 2: return D3DBLEND_SRCCOLOR;  // as a destination factor 2 is the source's color
    case 3: return alphaBuffer ? D3DBLEND_DESTALPHA : D3DBLEND_ONE;
    case 4: return D3DBLEND_ONE;
    case 5: return D3DBLEND_INVSRCALPHA;
    case 6: return D3DBLEND_INVSRCCOLOR;
    case 7: return alphaBuffer ? D3DBLEND_INVDESTALPHA : D3DBLEND_ZERO;
    case 15: return D3DBLEND_SRCCOLOR;  // PREFOG_COLOR: the same without fog (with fog: BlendInShader)
    default: return D3DBLEND_ZERO;
    }
}

// A color register as a shader constant (r, g, b, a in 0..1).
void ColorConstant(float* out, FxU32 argb) {
    out[0] = ((argb >> 16) & 0xFF) / 255.0f;
    out[1] = ((argb >> 8) & 0xFF) / 255.0f;
    out[2] = (argb & 0xFF) / 255.0f;
    out[3] = (argb >> 24) / 255.0f;
}

}  // namespace

// The clip window, in target pixels (the origin flipped when it is the lower left).
RECT ClipRect(const Regs& r) {
    const int s = g_r.scale;
    LONG l = LONG((r.clipLeftRight >> SST_CLIPLEFT_SHIFT) & 0xFFF), rr = LONG(r.clipLeftRight & 0xFFF);
    LONG t = LONG((r.clipBottomTop >> SST_CLIPBOTTOM_SHIFT) & 0xFFF), b = LONG(r.clipBottomTop & 0xFFF);
    if (r.fbzMode & SST_YORIGIN) {
        const LONG H = g_board.screenH, nt = H - b, nb = H - t;
        t = nt;
        b = nb;
    }
    RECT c = {l * s, t * s, rr * s, b * s};
    c.left = std::clamp<LONG>(c.left, 0, g_r.w);
    c.right = std::clamp<LONG>(c.right, c.left, g_r.w);
    c.top = std::clamp<LONG>(c.top, 0, g_r.h);
    c.bottom = std::clamp<LONG>(c.bottom, c.top, g_r.h);
    return c;
}

// The draw buffer, the depth buffer, the viewport and the clip window (always on: Glide
// enables it with grSstWinOpen), and the fixed states.
void ApplyTargets(const Regs& r) {
    IDirect3DSurface9* rt =
        Target((r.fbzMode & SST_DRAWBUFFER) == SST_DRAWBUFFER_FRONT ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER);
    UseTarget(rt);
    UseDepth(g_r.depth);
    const D3DVIEWPORT9 vp = {0, 0, DWORD(g_r.w), DWORD(g_r.h), 0.0f, 1.0f};
    UseViewport(vp);
    const RECT clip = ClipRect(r);
    UseScissor(clip);
    RenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    RenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    RenderState(D3DRS_LIGHTING, FALSE);
    RenderState(D3DRS_CLIPPING, FALSE);
    RenderState(D3DRS_STENCILENABLE, FALSE);
    RenderState(D3DRS_FOGENABLE, FALSE);
}

// The registers' state that frame buffer writes through the pixel pipeline share with
// triangles: the targets, depth, the write masks, blending, the alpha test, the pixel shader
// constants c0-c11, the fog table (s2), and the copy blending in the shader reads.
void ApplyFixedState() {
    IDirect3DDevice9* d = g_r.dev;
    const GlideState& st = St();
    const Regs& r = st.r;
    ApplyTargets(r);
    // Depth.
    const bool depth = (r.fbzMode & SST_ENDEPTHBUFFER) != 0;
    RenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
    const DWORD zfunc = D3DCMP_NEVER + ((r.fbzMode & SST_ZFUNC) >> SST_ZFUNC_SHIFT);
    RenderState(D3DRS_ZFUNC, zfunc);
    // (An overlay, IsOverlay, doesn't write depth: the next laid on the same spot would fight
    // the first's as the first fought the surface's.)
    const bool zwrite = depth && (r.fbzMode & SST_ZAWRMASK) && !IsOverlay(r);
    RenderState(D3DRS_ZWRITEENABLE, zwrite ? TRUE : FALSE);
    RenderState(D3DRS_DEPTHBIAS, 0);  // (the shader writes the depth, the bias in it)
    // Writes: RGB, and alpha where the aux buffer is an alpha buffer.
    DWORD mask = (r.fbzMode & SST_RGBWRMASK)
                     ? D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE
                     : 0;
    if ((r.fbzMode & (SST_ENALPHABUFFER | SST_ZAWRMASK)) == (SST_ENALPHABUFFER | SST_ZAWRMASK) && !depth)
        mask |= D3DCOLORWRITEENABLE_ALPHA;
    RenderState(D3DRS_COLORWRITEENABLE, mask);
    g_r.batchMask = mask;
    g_r.batchZFunc = zfunc;
    g_r.batchZWrite = zwrite;
    // Blending.
    const bool shaderBlend = BlendInShader(r);
    const bool blend = (r.alphaMode & SST_ENALPHABLEND) != 0 && !shaderBlend;
    RenderState(D3DRS_ALPHABLENDENABLE, blend);
    if (blend) {
        const bool ab = (r.fbzMode & SST_ENALPHABUFFER) != 0;
        RenderState(D3DRS_SRCBLEND, SrcBlend((r.alphaMode & SST_RGBSRCFACT) >> SST_RGBSRCFACT_SHIFT, ab));
        RenderState(D3DRS_DESTBLEND, DstBlend((r.alphaMode & SST_RGBDSTFACT) >> SST_RGBDSTFACT_SHIFT, ab));
        RenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        RenderState(D3DRS_SRCBLENDALPHA, SrcBlend((r.alphaMode & SST_ASRCFACT) >> SST_ASRCFACT_SHIFT, ab));
        RenderState(D3DRS_DESTBLENDALPHA, DstBlend((r.alphaMode & SST_ADSTFACT) >> SST_ADSTFACT_SHIFT, ab));
        RenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    }
    // The alpha test is the shader's (MakeShaderKey), on the 8-bit alpha as integers.
    RenderState(D3DRS_ALPHATESTENABLE, FALSE);
    // The constants, in one upload.
    float k[kPsDepth + 1][4] = {};
    ColorConstant(k[kPsColor0], r.color0);
    ColorConstant(k[kPsColor1], r.color1);
    ColorConstant(k[kPsFogColor], r.fogColor);
    ColorConstant(k[kPsChromaKey], r.chromaKey);
    const float delta[4] = {st.ccR / 255.0f, st.ccG / 255.0f, st.ccB / 255.0f, st.ccA / 255.0f};
    memcpy(k[kPsDelta0], delta, sizeof(delta));
    const float stip[4] = {float(r.stipple & 0xFF), float((r.stipple >> 8) & 0xFF), float((r.stipple >> 16) & 0xFF),
                           float(r.stipple >> 24)};
    memcpy(k[kPsStipple], stip, sizeof(stip));
    const float tgt[4] = {1.0f / float(g_r.scale), float((r.alphaMode & SST_ALPHAREF) >> SST_ALPHAREF_SHIFT),
                          1.0f / float(g_r.w), 1.0f / float(g_r.h)};
    memcpy(k[kPsTarget], tgt, sizeof(tgt));
    // x: compare to bias, zaColor's depth as drawn; y: the depth bias (signed, Glide's units);
    // z: an overlay's pull forward (overlaybias steps).
    const float depthK[4] = {ClearDepth(FxU16(r.zaColor & SST_ZACOLOR_DEPTH)), float(FxI16(r.zaColor & 0xFFFF)),
                             float(g_config.overlayBias), 0};
    memcpy(k[kPsDepth], depthK, sizeof(depthK));
    for (int t = 0; t < 2 && t < g_board.numTmu; t++) {
        const TmuRegs& tr = r.tmu[t];
        // The coordinates' scale: texture coordinates are in 256ths of the texture's longer
        // side. (The LOD the detail and LOD fraction factors take is computed from them,
        // whether the texel is sampled or not.)
        const int asp = int((tr.tLOD & SST_LOD_ASPECT) >> SST_LOD_ASPECT_SHIFT);
        const bool sWide = (tr.tLOD & SST_LOD_S_IS_WIDER) != 0;
        const float sRange = sWide ? 256.0f : 256.0f / float(1 << asp);
        const float tRange = sWide ? 256.0f / float(1 << asp) : 256.0f;
        const float scale[4] = {1.0f / sRange, 1.0f / tRange, sRange, tRange};
        memcpy(k[t == 0 ? kPsTexScale0 : kPsTexScale1], scale, sizeof(scale));
        // Detail: the factor is ((bias - LOD) << scale) out of 255, at most detail max: bias in
        // whole LODs (signed 6 bits), the LOD the texel's (as MAME's Voodoo has it).
        int bias6 = int((tr.tDetail & SST_DETAIL_BIAS) >> SST_DETAIL_BIAS_SHIFT);
        if (bias6 & 0x20) bias6 -= 0x40;
        const float dk[4] = {float(bias6),
                             float(1 << ((tr.tDetail & SST_DETAIL_SCALE) >> SST_DETAIL_SCALE_SHIFT)) / 255.0f,
                             ((tr.tDetail & SST_DETAIL_MAX) >> SST_DETAIL_MAX_SHIFT) / 255.0f, 0};
        memcpy(k[t == 0 ? kPsDetail0 : kPsDetail1], dk, sizeof(dk));
    }
    Constants(0, k[0], kPsDepth + 1);
    if (shaderBlend && !g_r.dstCopy &&
        FAILED(d->CreateTexture(g_r.w, g_r.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_r.dstCopy,
                                nullptr)))
        g_r.dstCopy = nullptr;
    // Fog table.
    if ((r.fogMode & SST_ENFOGGING) && g_r.fogTex &&
        (!g_r.fogValid || memcmp(g_r.fogLoaded, r.fogTable, GR_FOG_TABLE_SIZE))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(g_r.fogTex->LockRect(0, &lr, nullptr, RewriteLock()))) {
            FxU32* px = static_cast<FxU32*>(lr.pBits);
            for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) px[i] = FxU32(r.fogTable[i]) << 24;
            g_r.fogTex->UnlockRect(0);
        }
        memcpy(g_r.fogLoaded, r.fogTable, GR_FOG_TABLE_SIZE);
        g_r.fogValid = true;
    }
    BindStage(2, g_r.fogTex);
    SamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    SamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    SamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    SamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
}

namespace {

// Triangles' own: their shader (and compare to bias's second), the vertex format, the copy
// blending in the shader reads (s3), and the TMUs' textures and samplers.
void ApplyTriangleState() {
    IDirect3DDevice9* d = g_r.dev;
    const GlideState& st = St();
    const Regs& r = st.r;
    const ShaderKey key = MakeShaderKey(st, g_board.numTmu);
    g_r.batchPs = GetShader(d, key);
    UseShader(g_r.batchPs);
    // Compare to bias tests zaColor's depth but writes the pixel's own (MAME's depth_test and
    // write_pixel): with depth writes on, a second pass writes it (RenderFlush).
    g_r.biasWritePs = key.depthConst && g_r.batchZWrite ? GetShader(d, DepthWriteKey(key, r)) : nullptr;
    UseFvf(kFvf);
    const bool shaderBlend = BlendInShader(r);
    BindStage(3, shaderBlend ? g_r.dstCopy : nullptr);
    if (shaderBlend) PointSampler(3);
    // Textures.
    for (int t = 0; t < 2; t++) {
        IDirect3DTexture9* tex = nullptr;
        if (t < key.tmus && ShaderUsesTexture(key, t)) tex = BindTexture(t);
        if (LogLevel() >= 2 && t < key.tmus) {
            TexDesc td;
            if (DescribeTexture(t, r.tmu[t], &td))
                GL_TRACE("  state: tmu%d %s mode %08lx fmt %d base 0x%lx lod %d-%d aspect %d %dx%d table %08x", t,
                         tex ? "sampled" : "not sampled", static_cast<unsigned long>(r.tmu[t].textureMode), td.format,
                         static_cast<unsigned long>(td.base), td.lodMin, td.lodMax, int(td.aspect), td.width, td.height,
                         td.tableHash);
        }
        BindStage(t, tex);
        if (!tex || t >= g_board.numTmu) continue;
        const TmuRegs& tr = r.tmu[t];
        // The game's filtering; texfilter=linear and anisotropy= (dd2d9's) change it, except on
        // cut-outs (a format with alpha, or chroma keying: trees, fences, sprites): made
        // linear, their transparent texels blend into the edges (dark fringes); made
        // anisotropic, the longer footprint reaches past their edges.
        DWORD minF = (tr.textureMode & SST_TMINFILTER) ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        DWORD magF = (tr.textureMode & SST_TMAGFILTER) ? D3DTEXF_LINEAR : D3DTEXF_POINT;
        DWORD aniso = 1;
        if (g_config.texLinear || g_config.anisotropy > 1) {
            static const bool kAlpha[16] = {false, false, true,  false, true, false, true, false,
                                            true,  true,  false, true,  true, true,  true, false};
            const bool cutout =
                kAlpha[(tr.textureMode & SST_TFORMAT) >> SST_TFORMAT_SHIFT] || (r.fbzMode & SST_ENCHROMAKEY);
            if (g_config.texLinear && !cutout) minF = magF = D3DTEXF_LINEAR;
            if (g_config.anisotropy > 1 && !g_r.anisoLimit) {
                D3DCAPS9 caps = {};
                g_r.anisoLimit = 1;
                if (SUCCEEDED(d->GetDeviceCaps(&caps)) && (caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC))
                    g_r.anisoLimit =
                        std::max<DWORD>(1, std::min<DWORD>(DWORD(g_config.anisotropy), caps.MaxAnisotropy));
                GL_LOG("anisotropy: %lu (the GPU: up to %lu)", static_cast<unsigned long>(g_r.anisoLimit),
                       static_cast<unsigned long>(caps.MaxAnisotropy));
            }
            // Only where the filtering is smooth: point (sharp on purpose) stays.
            if (g_r.anisoLimit > 1 && !cutout && minF == D3DTEXF_LINEAR) {
                minF = D3DTEXF_ANISOTROPIC;
                aniso = g_r.anisoLimit;
            }
        }
        SamplerState(t, D3DSAMP_MINFILTER, minF);
        SamplerState(t, D3DSAMP_MAGFILTER, magF);
        SamplerState(t, D3DSAMP_MAXANISOTROPY, aniso);
        const int lodMin = int(((tr.tLOD & SST_LODMIN) >> SST_LODMIN_SHIFT) >> 2),
                  lodMax = int(((tr.tLOD & SST_LODMAX) >> SST_LODMAX_SHIFT) >> 2);
        SamplerState(t, D3DSAMP_MIPFILTER,
                     lodMax <= lodMin                   ? D3DTEXF_NONE
                     : (tr.textureMode & SST_TRILINEAR) ? D3DTEXF_LINEAR
                                                        : D3DTEXF_POINT);
        int b6 = int((tr.tLOD & SST_LODBIAS) >> SST_LODBIAS_SHIFT);
        if (b6 & 0x20) b6 -= 0x40;
        float lodBias = b6 / 4.0f;
        SamplerState(t, D3DSAMP_MIPMAPLODBIAS, FloatBits(lodBias));
        SamplerState(t, D3DSAMP_ADDRESSU,
                     (tr.tLOD & SST_LOD_TMIRRORS)     ? D3DTADDRESS_MIRROR
                     : (tr.textureMode & SST_TCLAMPS) ? D3DTADDRESS_CLAMP
                                                      : D3DTADDRESS_WRAP);
        SamplerState(t, D3DSAMP_ADDRESSV,
                     (tr.tLOD & SST_LOD_TMIRRORT)     ? D3DTADDRESS_MIRROR
                     : (tr.textureMode & SST_TCLAMPT) ? D3DTADDRESS_CLAMP
                                                      : D3DTADDRESS_WRAP);
    }
}

}  // namespace

// Sets everything a batch draws with from the registers.
void ApplyState() {
    ApplyFixedState();
    ApplyTriangleState();
}

}  // namespace gl2d9
