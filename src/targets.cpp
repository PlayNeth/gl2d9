// The renderer's targets: the color buffers, the depth buffer and their scratch textures.
#include "renderint.h"

namespace gl2d9 {

void ReleaseTargets() {
    for (auto*& c : g_r.colorSurf)
        if (c) c->Release(), c = nullptr;
    for (auto*& c : g_r.color)
        if (c) c->Release(), c = nullptr;
    if (g_r.depth) g_r.depth->Release(), g_r.depth = nullptr;
    if (g_r.depthTex) g_r.depthTex->Release(), g_r.depthTex = nullptr;
    if (g_r.uploadAux) g_r.uploadAux->Release(), g_r.uploadAux = nullptr;
    if (g_r.auxRead) g_r.auxRead->Release(), g_r.auxRead = nullptr;
    if (g_r.auxReadback) g_r.auxReadback->Release(), g_r.auxReadback = nullptr;
    if (g_r.fogTex) g_r.fogTex->Release(), g_r.fogTex = nullptr;
    if (g_r.readback) g_r.readback->Release(), g_r.readback = nullptr;
    if (g_r.upload) g_r.upload->Release(), g_r.upload = nullptr;
    if (g_r.dstCopy) g_r.dstCopy->Release(), g_r.dstCopy = nullptr;
    if (g_r.readSmall) g_r.readSmall->Release(), g_r.readSmall = nullptr;
    if (g_r.look) g_r.look->Release(), g_r.look = nullptr;
    if (g_r.stipTex) g_r.stipTex->Release(), g_r.stipTex = nullptr;
    g_r.stipTexH = 0;
    if (g_r.readSmallSys) g_r.readSmallSys->Release(), g_r.readSmallSys = nullptr;
    g_r.readSurf = nullptr;
    Dirty();
    g_r.uploadW = g_r.uploadH = 0;
    g_r.fogValid = false;
    ForgetDeviceState();
}

// (Failing part way, none are kept: RenderReady goes by color[0], and Target used color[1]
// whatever it was.)
bool MakeTargets() {
    IDirect3DDevice9* d = g_r.dev;
    EndDraw();
    for (int i = 0; i < 2; i++) {
        IDirect3DTexture9*& c = g_r.color[i];
        if (FAILED(d->CreateTexture(g_r.w, g_r.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &c,
                                    nullptr)) ||
            FAILED(c->GetSurfaceLevel(0, &g_r.colorSurf[i]))) {
            if (c) c->Release(), c = nullptr;
            g_r.colorSurf[i] = nullptr;
            ReleaseTargets();
            return false;
        }
    }
    // The depth buffer: an INTZ texture where the driver has them (the LFB reads it), a plain
    // depth buffer otherwise.
    const D3DFORMAT kIntz = static_cast<D3DFORMAT>(MAKEFOURCC('I', 'N', 'T', 'Z'));
    D3DDISPLAYMODE mode = {};
    if (FAILED(g_r.d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode))) mode.Format = D3DFMT_X8R8G8B8;
    if (SUCCEEDED(g_r.d3d->CheckDeviceFormat(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mode.Format, D3DUSAGE_DEPTHSTENCIL,
                                             D3DRTYPE_TEXTURE, kIntz)) &&
        SUCCEEDED(
            d->CreateTexture(g_r.w, g_r.h, 1, D3DUSAGE_DEPTHSTENCIL, kIntz, D3DPOOL_DEFAULT, &g_r.depthTex, nullptr))) {
        g_r.depthTex->GetSurfaceLevel(0, &g_r.depth);
    } else {
        g_r.depthTex = nullptr;
        if (FAILED(d->CreateDepthStencilSurface(g_r.w, g_r.h, D3DFMT_D24S8, D3DMULTISAMPLE_NONE, 0, FALSE, &g_r.depth,
                                                nullptr)))
            g_r.depth = nullptr;
    }
    if (!g_r.depth) {
        ReleaseTargets();
        return false;
    }
    if (FAILED(d->CreateTexture(GR_FOG_TABLE_SIZE, 1, 1, TextureUsage(), D3DFMT_A8R8G8B8, TexturePool(), &g_r.fogTex,
                                nullptr)))
        g_r.fogTex = nullptr;
    // Black, and the depth buffer at its farthest.
    for (auto* s : g_r.colorSurf) d->ColorFill(s, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
    IDirect3DSurface9* old = nullptr;
    d->GetDepthStencilSurface(&old);
    UseDepth(g_r.depth);
    d->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    UseDepth(old);
    if (old) old->Release();
    g_r.batchState = false;
    return true;
}

void ReleaseChain() {
    if (g_r.chain) g_r.chain->Release(), g_r.chain = nullptr;
    g_r.chainW = g_r.chainH = 0;
}

// GR_BUFFER_FRONTBUFFER / BACKBUFFER's surface (the targets hold it).
IDirect3DSurface9* Target(int buffer) { return g_r.colorSurf[buffer == GR_BUFFER_FRONTBUFFER ? 0 : 1]; }

// A texture of gl2d9's own, made if not made yet.
bool MakeTexture(IDirect3DTexture9** tex, int w, int h, DWORD usage, D3DFORMAT fmt, D3DPOOL pool) {
    if (*tex) return true;
    if (FAILED(g_r.dev->CreateTexture(w, h, 1, usage, fmt, pool, tex, nullptr))) *tex = nullptr;
    return *tex != nullptr;
}

}  // namespace gl2d9
