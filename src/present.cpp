// The renderer's presenting: the front buffer into the game's window.
#include "renderint.h"

#include <algorithm>

#include "window.h"

namespace gl2d9 {

const char* kPresentPs =
    "sampler2D frame : register(s0);\n"
    "float4 k : register(c0);\n"  // x: 1/gamma
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float4 c = tex2D(frame, uv);\n"
    "  return float4(pow(saturate(c.rgb), k.x), 1);\n"
    "}\n";
// Sharp bilinear: each source pixel flat, blended into the next over one screen pixel only
// (bilinear blurs over the whole source pixel). zw is the real scale, not its integer part:
// below 2x (640x480 on a 768-line screen) that would be 1, plain bilinear.
const char* kSharpPs =
    "sampler2D frame : register(s0);\n"
    "float4 k : register(c0);\n"   // x: 1/gamma
    "float4 sz : register(c1);\n"  // xy: the source's size, zw: the scale (screen pixels per source pixel)
    "float4 main(float2 uv : TEXCOORD0) : COLOR {\n"
    "  float2 t = uv * sz.xy;\n"
    "  float2 f = frac(t) - 0.5;\n"
    "  float2 range = 0.5 - 0.5 / sz.zw;\n"
    "  float2 o = (f - clamp(f, -range, range)) * sz.zw + 0.5;\n"
    "  float4 c = tex2D(frame, (floor(t) + o) / sz.xy);\n"
    "  return float4(pow(saturate(c.rgb), k.x), 1);\n"
    "}\n";
// The Voodoo's 16-bit look: each channel to 5 or 6 bits with its 4x4 ordered dither, as
// MAME's Voodoo dithers a pixel written (dither_matrix_4x4, DITHER_RB / DITHER_G), on the
// game's pixels (vpos / scale), and widened back the way the Voodoo's DAC shows it. A look,
// not Glide's behaviour: the whole frame at presenting, whatever grDitherMode says (2x2, off),
// and blending works on the undithered 8-bit values the Voodoo would have dithered at each write.
const char* kDitherPs =
    "sampler2D frame : register(s0);\n"
    "float4 k : register(c0);\n"  // x: 1 / scale
    "float4 main(float2 uv : TEXCOORD0, float2 vpos : VPOS) : COLOR {\n"
    "  float3 v = round(saturate(tex2D(frame, uv).rgb) * 255);\n"
    "  float2 p = fmod(floor(vpos * k.x), 4);\n"
    "  float4 row = p.y < 1 ? float4(0, 8, 2, 10) : p.y < 2 ? float4(12, 4, 14, 6) : p.y < 3 ? float4(3, 11, 1, 9)"
    " : float4(15, 7, 13, 5);\n"
    "  float d = dot(row, float4(p.x == float4(0, 1, 2, 3)));\n"
    // R, B: ((v << 1) - (v >> 4) + (v >> 7) + d) >> 4; G: ((v << 2) - (v >> 4) + (v >> 6) + d) >> 4.
    "  float3 q = floor((v * float3(2, 4, 2) - floor(v / 16) + floor(v / float3(128, 64, 128)) + d) / 16);\n"
    "  float3 c = float3(q.r * 8 + floor(q.r / 4), q.g * 4 + floor(q.g / 16), q.b * 8 + floor(q.b / 4));\n"
    "  return float4(c / 255, 1);\n"
    "}\n";

namespace {

// The back buffer that goes into the game's window: the device's own, or the swap chain made
// on the game's window when the device is on the helper (null: none to be had).
IDirect3DSurface9* PresentTarget(const SIZE& size) {
    IDirect3DDevice9* d = g_r.dev;
    IDirect3DSurface9* bb = nullptr;
    if (g_r.deviceWindow == g_r.hwnd || g_r.chainRefused) {
        if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) bb = nullptr;
        return bb;
    }
    if (g_r.chain && (g_r.chainW != UINT(size.cx) || g_r.chainH != UINT(size.cy))) ReleaseChain();
    if (!g_r.chain) {
        g_r.compositor = CompositorPaced();
        D3DPRESENT_PARAMETERS pp = Params();
        pp.BackBufferWidth = UINT(size.cx);
        pp.BackBufferHeight = UINT(size.cy);
        pp.hDeviceWindow = g_r.hwnd;
        pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
        const HRESULT hr = d->CreateAdditionalSwapChain(&pp, &g_r.chain);
        if (FAILED(hr)) {
            // (Refused too, as the device was: shown from the device's own back buffer.)
            GL_LOG(
                "device: a swap chain on the game window (%ldx%ld) refused: 0x%08lx; presenting into it from "
                "the helper's",
                size.cx, size.cy, static_cast<unsigned long>(hr));
            g_r.chain = nullptr;
            g_r.chainRefused = true;
            if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) bb = nullptr;
            return bb;
        }
        g_r.chainW = UINT(size.cx), g_r.chainH = UINT(size.cy);
    }
    if (FAILED(g_r.chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &bb))) bb = nullptr;
    return bb;
}

}  // namespace

// The front buffer into the window, the back buffer its client area's size and presented
// whole (see Renderer::bbW). The window resized: the device Reset to its new size, on the
// thread that made it (from another, such as the window's own repainting, the picture is
// stretched to it until the game's next frame).
bool Present() {
    IDirect3DDevice9* d = g_r.dev;
    HWND hwnd = g_r.hwnd;
    RECT client = {};
    if (!hwnd || !GetClientRect(hwnd, &client) || client.right <= 0 || client.bottom <= 0 || IsIconic(hwnd))
        return false;
    // The targets and states change from here on, whichever way it returns (it returned with
    // voodoolook's target still set, and the next batch drew into it).
    g_r.batchState = false;
    const SIZE want = PresentSize();
    if (g_r.deviceWindow == hwnd && (UINT(want.cx) != g_r.bbW || UINT(want.cy) != g_r.bbH) &&
        GetCurrentThreadId() == g_r.devThread && !ResetDevice("window resized"))
        return false;
    // (The helper's own back buffer into the game's window: the part of it the client takes.)
    const bool own = g_r.deviceWindow == hwnd || g_r.chainRefused;
    const UINT cw = own ? std::min<UINT>(g_r.bbW, g_r.deviceWindow == hwnd ? g_r.bbW : UINT(want.cx)) : UINT(want.cx);
    const UINT ch = own ? std::min<UINT>(g_r.bbH, g_r.deviceWindow == hwnd ? g_r.bbH : UINT(want.cy)) : UINT(want.cy);
    // voodoolook: the frame dithered to 16 bits first (into look), shown from there.
    IDirect3DTexture9* frame = g_r.color[0];
    if (g_r.ditherPs && MakeTexture(&g_r.look, g_r.w, g_r.h, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT)) {
        IDirect3DSurface9* ls = nullptr;
        if (SUCCEEDED(g_r.look->GetSurfaceLevel(0, &ls))) {
            UseTarget(ls);
            UseDepth(nullptr);
            UseViewport(D3DVIEWPORT9{0, 0, DWORD(g_r.w), DWORD(g_r.h), 0.0f, 1.0f});
            SetPassState();
            UseShader(g_r.ditherPs);
            const float k[4] = {1.0f / float(g_r.scale), 0, 0, 0};
            Constants(0, k, 1);
            BindStage(0, g_r.color[0]);
            PointSampler(0);
            DrawQuad(RECT{0, 0, g_r.w, g_r.h}, 0.0f);
            ls->Release();
            frame = g_r.look;
        }
    }
    IDirect3DSurface9* bb = PresentTarget(want);
    if (!bb) return false;
    UseTarget(bb);
    UseDepth(nullptr);
    const D3DVIEWPORT9 vp = {0, 0, cw, ch, 0.0f, 1.0f};
    UseViewport(vp);
    const RECT whole = {0, 0, LONG(cw), LONG(ch)};
    EndDraw();
    d->ColorFill(bb, &whole, D3DCOLOR_XRGB(0, 0, 0));
    const RECT dst = ImageRect(int(cw), int(ch));
    SetPassState();
    const int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    const bool linear = g_config.filter != 0 && (dw != g_r.w || dh != g_r.h);
    const bool sharp = linear && g_config.filter == 2 && g_r.sharpPs;
    UseShader(sharp ? g_r.sharpPs : g_r.presentPs);
    const float k[4] = {1.0f / std::max(0.1f, g_r.gamma), 0, 0, 0};
    Constants(0, k, 1);
    if (sharp) {
        const float sz[4] = {float(g_r.w), float(g_r.h), std::max(1.0f, float(dw) / float(g_r.w)),
                             std::max(1.0f, float(dh) / float(g_r.h))};
        Constants(1, sz, 1);
    }
    BindStage(0, frame);
    SamplerState(0, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    SamplerState(0, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    SamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    SamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    SamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    DrawQuad(dst, 0.0f);
    BindStage(0, nullptr);
    bb->Release();
    const RECT part = {0, 0, LONG(cw), LONG(ch)};
    EndDraw();
    const HRESULT hr = g_r.chain                  ? g_r.chain->Present(nullptr, nullptr, nullptr, nullptr, 0)
                       : g_r.deviceWindow == hwnd ? d->Present(nullptr, nullptr, nullptr, nullptr)
                                                  : d->Present(&part, nullptr, hwnd, nullptr);
    g_r.batchState = false;  // (the targets changed)
    if (hr == D3DERR_DEVICEREMOVED || hr == D3DERR_DEVICEHUNG || hr == D3DERR_DRIVERINTERNALERROR) {
        GL_LOG("device: Present: 0x%08lx: made anew", static_cast<unsigned long>(hr));
        g_r.remake = true;
    }
    return SUCCEEDED(hr);
}

bool RenderPacedByCompositor() { return g_r.dev && g_r.compositor && !g_r.defaultInterval; }

bool RenderSwap(int interval) {
    if (!RenderReady()) return false;
    RenderFlush();
    if (!DeviceUsable()) return false;
    std::swap(g_r.color[0], g_r.color[1]);
    std::swap(g_r.colorSurf[0], g_r.colorSurf[1]);
    g_r.batchState = false;
    const bool shown = Present();
    g_r.frame++;
    // vsync=auto: a swap interval of 0 presents at once, as the Voodoo swapped without waiting
    // for the display (Unreal with UseVSync=False, benchmarks); 1 and more wait. Present's own
    // wait is a device setting, so the device is Reset to the other kind, after 8 swaps in a row
    // asking for it (a game switching for one frame, a loading screen's, doesn't Reset back and
    // forth). Reset after presenting: it keeps the front buffer, the frame just shown, and the
    // next frame is drawn anew (before it, the frame the game had just drawn was lost: black).
    // A refused Reset is tried again at the next swap. Under the compositor Present never
    // waits: CoreSwap's pacing honours 0 itself.
    const bool immediate = g_config.vsync == kVSyncAuto && interval == 0;
    if (immediate == g_r.swapImmediate || g_r.compositor || g_r.defaultInterval) {
        g_r.swapOther = 0;
    } else if (++g_r.swapOther >= 8 && GetCurrentThreadId() == g_r.devThread) {
        g_r.swapImmediate = immediate;
        if (ResetDevice("swap interval")) {
            g_r.swapOther = 0;
            GL_LOG("vsync: the game swaps %s (grBufferSwap(%d))", immediate ? "at once" : "on the display's refresh",
                   interval);
        } else {
            g_r.swapImmediate = !immediate;  // (still the device's kind)
        }
    }
    // Textures not used for a while go (the texture memory still holds them).
    if ((g_r.frame & 15) == 0) TrimTextures(300, kTextureBudget);
    // Once a minute or so: what the texture cache holds (for logs from games).
    if (g_r.frame % 3600 == 0)
        GL_LOG("frame %u: %u textures, %u MB, %u spare", g_r.frame, unsigned(g_r.textures.size()),
               unsigned(g_r.textureBytes >> 20), unsigned(g_r.spares.size()));
    return shown;
}

void RenderRefresh() {
    if (!RenderReady()) return;
    RenderFlush();
    if (DeviceUsable()) Present();
}

void RenderSetGamma(float gamma) { g_r.gamma = gamma > 0 ? gamma : 1.0f; }

}  // namespace gl2d9
