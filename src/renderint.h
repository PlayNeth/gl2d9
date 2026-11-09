// The renderer's internals, shared by its files: render.cpp (the device), targets.cpp,
// rendertex.cpp (textures), renderstate.cpp (device state), draw.cpp, stipple.cpp,
// present.cpp, renderlfb.cpp (the linear frame buffer).
#pragma once
#include "render.h"

#include <d3d9.h>

#include <cstring>
#include <unordered_map>
#include <vector>

namespace gl2d9 {

struct DVtx {  // what Direct3D draws: pre-transformed, every attribute in a texture coordinate
    float x, y, z, rhw;
    float col[4];   // iterated RGBA, 0..255
    float st0[4];   // TMU0 s/w, t/w, 1/w
    float st1[4];   // TMU1
    float misc[4];  // fog, Z (0..1), the FBI's 1/w, Z (Glide's, 0..65535)
};
const DWORD kFvf = D3DFVF_XYZRHW | D3DFVF_TEX4 | D3DFVF_TEXCOORDSIZE4(0) | D3DFVF_TEXCOORDSIZE4(1) |
                   D3DFVF_TEXCOORDSIZE4(2) | D3DFVF_TEXCOORDSIZE4(3);

struct TexKey {
    int tmu, format, lodMin, lodMax, aspect;
    FxU32 base[4];  // the base address (multibase: the four)
    bool tsplit, odd;
    unsigned tableHash;
    bool operator==(const TexKey& o) const {
        return tmu == o.tmu && format == o.format && lodMin == o.lodMin && lodMax == o.lodMax && aspect == o.aspect &&
               memcmp(base, o.base, sizeof(base)) == 0 && tsplit == o.tsplit && odd == o.odd &&
               tableHash == o.tableHash;
    }
};
struct TexKeyHash {
    size_t operator()(const TexKey& k) const {
        size_t h = k.base[0] ^ k.base[1] * 3 ^ k.base[2] * 5 ^ k.base[3] * 7;
        h = h * 31 + k.tmu;
        h = h * 31 + k.format;
        h = h * 31 + (k.lodMin << 4 | k.lodMax);
        h = h * 31 + k.aspect;
        h = h * 31 + (k.tsplit << 1 | k.odd);
        h = h * 31 + k.tableHash;
        return h;
    }
};
struct TexEntry {
    IDirect3DTexture9* tex = nullptr;
    unsigned gen = 0;
    unsigned lastUse = 0;
    FxU32 spanStart = 0;
    std::vector<FxU8> source;  // the texture memory it was decoded from
    size_t bytes = 0;          // its Direct3D texture's size
    int w = 0, h = 0, levels = 0;
};
// A Direct3D texture no longer needed, kept for the next of its size.
struct SpareTex {
    IDirect3DTexture9* tex;
    int w, h, levels;
    size_t bytes;
};

struct Renderer {
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* dev = nullptr;
    HWND hwnd = nullptr;           // the game's window: what is presented into
    HWND deviceWindow = nullptr;   // what the device is made on: the game's window, or helper
    HWND helper = nullptr;         // a window of gl2d9's (WindowMakeHelper), where one was needed
    bool defaultInterval = false;  // (a fallback: D3DPRESENT_INTERVAL_DEFAULT)
    // Present at once, gl2d9 pacing (CompositorPaced, util.cpp): asked again where a device, a
    // Reset or a swap chain is made, and what Params gives them.
    bool compositor = false;
    // vsync=auto and grBufferSwap(0): presented at once, as the Voodoo swapped without waiting
    // (RenderSwap; Params). Changed after a few swaps of the other kind (a Reset).
    bool swapImmediate = false;
    unsigned swapOther = 0;  // consecutive swaps asking for the other kind
    bool ex = false;         // a Direct3D 9Ex device (where Windows refused a 9 one)
    DWORD anisoLimit = 0;    // anisotropy= within what the GPU can (0: not known yet; 1: none)
    // What is presented: a back buffer the size of the game window's client area, shown whole
    // in it, as capture and overlay tools (OBS, Steam, RTSS) expect of a windowed Direct3D 9
    // game: they hook Present and read or draw on the render target, the window swap chain
    // 0's. The device's own swap chain on the game's window; on the helper window, an
    // additional one on the game's (chain).
    UINT bbW = 0, bbH = 0;
    IDirect3DSwapChain9* chain = nullptr;
    UINT chainW = 0, chainH = 0;
    bool chainRefused = false;  // (then the device's own back buffer, into the game's window as 1.0.2 did)
    DWORD devThread = 0;        // the thread that made the device (Reset there only)
    DWORD resetFailedAt = 0;    // the last Reset refused (tried again a quarter second on)
    unsigned resetFailures = 0;
    DWORD targetsTriedAt = 0;  // the targets last made again (DeviceUsable: a quarter second on)
    // The device last made anew (RemakeDevice: a second on), and how often that failed since it
    // last worked. (Kept across the RenderOpen it does; RenderClose, a grSstWinClose, resets them.)
    DWORD remakeTriedAt = 0;
    unsigned remakeFailures = 0;
    bool remake = false;  // the device to be made anew (removed, hung, or Reset refused too long)
    int scale = 1;
    int w = 0, h = 0;                      // the targets' size (the game's resolution times the scale)
    IDirect3DTexture9* color[2] = {};      // front, back
    IDirect3DSurface9* colorSurf[2] = {};  // their surfaces
    bool inScene = false;                  // BeginDraw
    IDirect3DSurface9* depth = nullptr;
    IDirect3DTexture9* depthTex = nullptr;  // the depth buffer as an INTZ texture (readable), where there is one
    IDirect3DTexture9* fogTex = nullptr;
    FxU8 fogLoaded[GR_FOG_TABLE_SIZE] = {};
    bool fogValid = false;
    IDirect3DPixelShader9* presentPs = nullptr;
    IDirect3DPixelShader9* ditherPs = nullptr;  // voodoolook: the frame to 565, dithered
    IDirect3DTexture9* look = nullptr;          // and where it goes
    // Stipple rotate mode (StippleRows): the batch's rows, four floats each (start, stop, the
    // first pixel's bit, its step), the texture they go to (2048 rows a line), and the register's
    // position: pixels counted since it was last written.
    std::vector<float> stipRows;
    IDirect3DTexture9* stipTex = nullptr;
    int stipTexH = 0;
    FxU32 stipCount = 0;
    unsigned stipWrites = ~0u;
    FxU32 stipPattern = 0;
    IDirect3DPixelShader9* clearPs = nullptr;
    IDirect3DPixelShader9* auxReadPs = nullptr;
    IDirect3DPixelShader9* copyPs = nullptr;
    IDirect3DPixelShader9* sharpPs = nullptr;
    IDirect3DTexture9* dstCopy = nullptr;  // the target as blending in the shader reads it
    float gamma = 1.0f;
    std::unordered_map<TexKey, TexEntry, TexKeyHash> textures;
    size_t textureBytes = 0;   // of all of them, the spares' included
    unsigned trimFrame = ~0u;  // the frame the budget was last enforced in (once a frame)
    std::vector<SpareTex> spares;
    unsigned frame = 0;
    // The batch: triangles drawn with one state.
    std::vector<DVtx> batch;
    Regs batchRegs{};
    bool batchState = false;  // batchRegs and the device state are those of the batch
    bool batchDelta0 = false;
    float batchCc[4] = {};    // grConstantColorValue4's color (ITRGB_DELTA0)
    bool batchBlend = false;  // blended in the shader: drawn in groups that don't overlap, each over a copy (Draw)
    IDirect3DPixelShader9* batchPs = nullptr;  // its shader (null: none could be made, not drawn)
    // Compare to bias with depth writes (ApplyState): the second pass's shader, and the batch's
    // write mask and depth function, which the passes change (RenderFlush). Null: one pass.
    IDirect3DPixelShader9* biasWritePs = nullptr;
    DWORD batchMask = 0, batchZFunc = 0;
    bool batchZWrite = false;
    // LFB scratch.
    IDirect3DSurface9* readback = nullptr;
    IDirect3DTexture9* upload = nullptr;  // LFB writes: the color
    IDirect3DTexture9* uploadAux =
        nullptr;  // and the depth (r: Direct3D's, g: Glide's / 65535, b: Glide's), a: written
    int uploadW = 0, uploadH = 0;
    IDirect3DTexture9* auxRead = nullptr;  // the depth buffer read into floats
    IDirect3DSurface9* auxReadback = nullptr;
    std::unordered_map<uint64_t, IDirect3DPixelShader9*> lfbShaders;
    // Reads at the game's resolution (the frame shrunk on the GPU first when scaled), and the
    // last one kept: a game locking the frame buffer again with nothing drawn between (a
    // read per lock, several per frame) gets it without waiting for the GPU.
    IDirect3DSurface9* readSmall = nullptr;     // render target, the game's size (scale > 1)
    IDirect3DSurface9* readSmallSys = nullptr;  // its system-memory copy
    unsigned gen = 1;                           // counts changes to the targets (Dirty)
    unsigned readGen = 0, auxGen = 0;           // the gen of the reads kept (0: none)
    IDirect3DSurface9* readSurf = nullptr;      // the target the color read is of
    std::vector<FxU32> readCache;
    std::vector<FxU16> auxCache;
};
extern Renderer g_r;

// Something drew into the targets (or they were made again): the reads kept are stale.
inline void Dirty() { g_r.gen = g_r.gen + 1 ? g_r.gen + 1 : 1; }

inline DWORD FloatBits(float f) {  // a float render or sampler state
    DWORD d;
    memcpy(&d, &f, sizeof(d));
    return d;
}

// A depth value as drawn from Glide's 16-bit one, Z or W: as the shader writes it (its depth
// output, ShaderKey::depthOut), the value itself in the middle of its 24-bit step, so that
// it reads back exactly.
inline float ClearDepth(FxU16 v) { return (v + 0.5f) / 65536.0f; }

// The textures' memory is held to a budget (32-bit games: the address space, as managed
// textures keep a copy in system memory): the least recently used go, their Direct3D
// textures kept for reuse by the next ones of the same size.
constexpr size_t kTextureBudget = 96u << 20;

// The device (render.cpp).
D3DPRESENT_PARAMETERS Params();
SIZE PresentSize();
D3DPOOL TexturePool();
DWORD TextureUsage();
DWORD RewriteLock();
bool ResetDevice(const char* why);
bool DeviceUsable();

// The targets (targets.cpp).
void ReleaseTargets();
bool MakeTargets();
void ReleaseChain();
IDirect3DSurface9* Target(int buffer);
bool MakeTexture(IDirect3DTexture9** tex, int w, int h, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);

// The textures (rendertex.cpp).
void ReleaseTextures();
void DropSpares();
void TrimTextures(unsigned maxAge, size_t budget);
IDirect3DTexture9* BindTexture(int tmu);

// The device state (renderstate.cpp): every state the renderer sets goes through these, which
// skip what the device has already (ForgetDeviceState where that isn't known).
void ForgetDeviceState();
void RenderState(D3DRENDERSTATETYPE s, DWORD v);
void SamplerState(DWORD stage, D3DSAMPLERSTATETYPE t, DWORD v);
void BindStage(DWORD stage, IDirect3DBaseTexture9* tex);
void UseShader(IDirect3DPixelShader9* ps);
void Constants(UINT start, const float* v, UINT n);
void UseTarget(IDirect3DSurface9* rt);
void UseDepth(IDirect3DSurface9* ds);
void UseViewport(const D3DVIEWPORT9& vp);
void UseScissor(const RECT& rc);
void UseFvf(DWORD fvf);
void BeginDraw();
void EndDraw();
void PointSampler(int stage);
void SetPassState();
RECT ClipRect(const Regs& r);
void ApplyTargets(const Regs& r);
void ApplyFixedState();
void ApplyState();

// Drawing (draw.cpp, stipple.cpp, present.cpp).
extern const char* kClearPs;
extern const char* kPresentPs;
extern const char* kSharpPs;
extern const char* kDitherPs;
void DrawQuad(const RECT& rc, float z, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1);
void UploadStippleRows();
void StippleRows(const RVtx* t, const Regs& r, float* firstEntry, float* firstScan);
bool Present();

}  // namespace gl2d9
