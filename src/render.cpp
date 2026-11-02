// The renderer: the Voodoo's frame buffer as Direct3D 9 render targets (front and back color
// buffers, a depth buffer), its triangles drawn with the pixel shaders shader.cpp makes from
// the registers, its textures decoded from the emulated texture memory, and the front
// buffer presented into the game's window.
#include "render.h"

#include <d3d9.h>

#include <tlhelp32.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <unordered_map>

#include "shader.h"
#include "texmem.h"
#include "window.h"

namespace gl2d9 {

namespace {

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
};

struct Renderer {
    IDirect3D9* d3d = nullptr;
    IDirect3DDevice9* dev = nullptr;
    HWND hwnd = nullptr;        // the game's window: what is presented into
    HWND deviceWindow = nullptr;  // what the device is made on: the game's window, or helper
    HWND helper = nullptr;        // a window of gl2d9's (WindowMakeHelper), where one was needed
    bool defaultInterval = false;  // (a fallback: D3DPRESENT_INTERVAL_DEFAULT)
    bool ex = false;               // a Direct3D 9Ex device (where Windows refused a 9 one)
    DWORD anisoLimit = 0;          // anisotropy= within what the GPU can (0: not known yet; 1: none)
    UINT bbW = 0, bbH = 0;  // the device's back buffer (the largest monitor)
    int scale = 1;
    int w = 0, h = 0;       // the targets' size (the game's resolution times the scale)
    IDirect3DTexture9* color[2] = {};  // front, back
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
    size_t textureBytes = 0;  // of all of them
    std::vector<SpareTex> spares;
    unsigned frame = 0;
    // The batch: triangles drawn with one state.
    std::vector<DVtx> batch;
    Regs batchRegs{};
    bool batchState = false;  // batchRegs and the device state are those of the batch
    bool batchDelta0 = false;
    float batchCc[4] = {};    // grConstantColorValue4's color (ITRGB_DELTA0)
    bool batchBlend = false;  // blended in the shader: drawn a triangle at a time, each over a copy
    // LFB scratch.
    IDirect3DSurface9* readback = nullptr;
    IDirect3DTexture9* upload = nullptr;     // LFB writes: the color
    IDirect3DTexture9* uploadAux = nullptr;  // and the depth (r: Direct3D's, g: Glide's / 65535, b: w), a: written
    int uploadW = 0, uploadH = 0;
    IDirect3DTexture9* auxRead = nullptr;     // the depth buffer read into floats
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
Renderer g_r;

// Something drew into the targets (or they were made again): the reads kept are stale.
inline void Dirty() { g_r.gen = g_r.gen + 1 ? g_r.gen + 1 : 1; }

using CreateFn = IDirect3D9*(WINAPI*)(UINT);

inline DWORD FloatBits(float f) {  // a float render or sampler state
    DWORD d;
    memcpy(&d, &f, sizeof(d));
    return d;
}

// The W buffer's 16-bit float: 4-bit exponent, 12-bit mantissa (1.m * 2^e).
float GlideW(FxU16 v) { return (1.0f + (v & 0xFFF) / 4096.0f) * float(1u << (v >> 12)); }

// A depth value as drawn from Glide's 16-bit one, Z or W: as the shader writes it (its depth
// output, ShaderKey::depthOut), the value itself in the middle of its 24-bit step, so that
// it reads back exactly.
float ClearDepth(FxU16 v, bool) { return (v + 0.5f) / 65536.0f; }

// The d3d9.dll used: Windows' own (config d3d9=system), else whichever the search finds.
HMODULE D3D9Module() {
    static HMODULE m = [] {
        HMODULE h = nullptr;
        if (g_config.systemD3D9) {
            wchar_t path[MAX_PATH];
            const UINT n = GetSystemDirectoryW(path, MAX_PATH);
            if (n && n < MAX_PATH - 12) {
                lstrcatW(path, L"\\d3d9.dll");
                h = LoadLibraryW(path);
            }
        }
        return h ? h : LoadLibraryW(L"d3d9.dll");
    }();
    return m;
}

IDirect3D9* CreateD3D9() {
    HMODULE m = D3D9Module();
    auto fn = m ? reinterpret_cast<CreateFn>(reinterpret_cast<void*>(GetProcAddress(m, "Direct3DCreate9"))) : nullptr;
    return fn ? fn(D3D_SDK_VERSION) : nullptr;
}

// Direct3D 9Ex (Windows Vista on): where Windows refuses a Direct3D 9 device (NFS 4 on
// Windows 11, a DirectDraw object of its own alive), a 9Ex one is still made.
IDirect3D9Ex* CreateD3D9Ex() {
    using CreateExFn = HRESULT(WINAPI*)(UINT, IDirect3D9Ex**);
    HMODULE m = D3D9Module();
    auto fn =
        m ? reinterpret_cast<CreateExFn>(reinterpret_cast<void*>(GetProcAddress(m, "Direct3DCreate9Ex"))) : nullptr;
    IDirect3D9Ex* ex = nullptr;
    if (!fn || FAILED(fn(D3D_SDK_VERSION, &ex))) ex = nullptr;
    return ex;
}

// Textures the game's data is decoded into: managed, or (9Ex has no managed pool) dynamic in
// video memory (9Ex devices don't lose it).
D3DPOOL TexturePool() { return g_r.ex ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED; }
DWORD TextureUsage() { return g_r.ex ? D3DUSAGE_DYNAMIC : 0; }

// Every device made the usual ways refused (9 and 9Ex): what might explain it logged (Windows
// as it is and as the game is told, the adapter, the modules loaded: an overlay or another
// d3d9.dll), and other ways tried, each logged. The first that gives a usable device is kept.
D3DPRESENT_PARAMETERS Params();

void DiagnoseCreateDevice() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW real = {sizeof(real)}, told = {sizeof(told)};
    if (auto fn = reinterpret_cast<RtlGetVersionFn>(
            reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"))))
        fn(&real);
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    GetVersionExW(&told);
#pragma GCC diagnostic pop
    // (A program without a manifest is told 6.2 from Windows 8.1 on: a compatibility mode
    // tells it an older one.)
    GL_LOG("diagnosis: Windows %lu.%lu build %lu; the game is told %lu.%lu build %lu", real.dwMajorVersion,
           real.dwMinorVersion, real.dwBuildNumber, told.dwMajorVersion, told.dwMinorVersion, told.dwBuildNumber);
    D3DADAPTER_IDENTIFIER9 id = {};
    g_r.d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    D3DCAPS9 caps = {};
    const HRESULT capsHr = g_r.d3d->GetDeviceCaps(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, &caps);
    GL_LOG("diagnosis: adapter %s (%s %u.%u.%u.%u), %u adapters; caps 0x%08lx: pixel shaders %lu.%lu, textures %lux%lu",
           id.Description, id.Driver, unsigned(HIWORD(id.DriverVersion.HighPart)),
           unsigned(LOWORD(id.DriverVersion.HighPart)), unsigned(HIWORD(id.DriverVersion.LowPart)),
           unsigned(LOWORD(id.DriverVersion.LowPart)), g_r.d3d->GetAdapterCount(), static_cast<unsigned long>(capsHr),
           static_cast<unsigned long>((caps.PixelShaderVersion >> 8) & 0xFF),
           static_cast<unsigned long>(caps.PixelShaderVersion & 0xFF), static_cast<unsigned long>(caps.MaxTextureWidth),
           static_cast<unsigned long>(caps.MaxTextureHeight));
    // The modules loaded (DLLs that could hook Direct3D: overlays, wrappers, another d3d9).
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, 0);
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = {sizeof(me)};
        std::string list;
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
            char path[MAX_PATH * 2];
            WideCharToMultiByte(CP_UTF8, 0, me.szExePath, -1, path, sizeof(path), nullptr, nullptr);
            list += list.empty() ? "" : ", ";
            list += path;
        }
        CloseHandle(snap);
        GL_LOG("diagnosis: modules: %s", list.c_str());
    }
    // Other ways. (Each made on gl2d9's own window, the back buffer the largest monitor's.)
    struct Way {
        const char* what;
        DWORD flags;
        bool usable;  // a device made this way works as gl2d9's
    } ways[] = {
        {"no NOWINDOWCHANGES", D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_HARDWARE_VERTEXPROCESSING,
         true},
        {"no MULTITHREADED", D3DCREATE_FPU_PRESERVE | D3DCREATE_NOWINDOWCHANGES | D3DCREATE_HARDWARE_VERTEXPROCESSING,
         true},
        {"FPU_PRESERVE only", D3DCREATE_FPU_PRESERVE | D3DCREATE_HARDWARE_VERTEXPROCESSING, true},
        {"no flags", D3DCREATE_HARDWARE_VERTEXPROCESSING, true},
        {"no flags, software", D3DCREATE_SOFTWARE_VERTEXPROCESSING, true},
        {"mixed vertex processing", D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_MIXED_VERTEXPROCESSING,
         true},
    };
    if (!g_r.helper) g_r.helper = WindowMakeHelper();
    g_r.deviceWindow = g_r.helper ? g_r.helper : g_r.hwnd;
    for (const Way& w : ways) {
        D3DPRESENT_PARAMETERS pp = Params();
        IDirect3DDevice9* dev = nullptr;
        const HRESULT hr =
            g_r.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_r.deviceWindow, w.flags, &pp, &dev);
        GL_LOG("diagnosis: %s: 0x%08lx", w.what, static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr) && dev) {
            if (w.usable && !g_r.dev)
                g_r.dev = dev;
            else
                dev->Release();
        }
    }
    // The presentation parameters varied (with the usual flags).
    const DWORD flags = D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_NOWINDOWCHANGES |
                        D3DCREATE_HARDWARE_VERTEXPROCESSING;
    auto tryParams = [&](const char* what, D3DPRESENT_PARAMETERS pp, bool usable) {
        IDirect3DDevice9* dev = nullptr;
        const HRESULT hr =
            g_r.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_r.deviceWindow, flags, &pp, &dev);
        GL_LOG("diagnosis: %s: 0x%08lx", what, static_cast<unsigned long>(hr));
        if (SUCCEEDED(hr) && dev) {
            if (usable && !g_r.dev)
                g_r.dev = dev;
            else
                dev->Release();
        }
        return SUCCEEDED(hr);
    };
    D3DPRESENT_PARAMETERS pp = Params();
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    const bool hadDevice = g_r.dev != nullptr;
    if (tryParams("interval default", pp, true) && !hadDevice) g_r.defaultInterval = true;
    pp = Params();
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    tryParams("swap effect discard", pp, false);
    pp = Params();
    pp.BackBufferWidth = pp.BackBufferHeight = 0;
    tryParams("back buffer of the window's size", pp, false);
    pp = Params();
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    tryParams("back buffer X8R8G8B8", pp, false);
    GL_LOG("diagnosis: %s", g_r.dev ? "a device made another way: used" : "no way gave a device");
}

BOOL CALLBACK AddMonitor(HMONITOR m, HDC, RECT*, LPARAM p) {
    SIZE* s = reinterpret_cast<SIZE*>(p);
    MONITORINFOEXW mi = {};
    mi.cbSize = sizeof(mi);
    if (GetMonitorInfoW(m, &mi)) {
        s->cx = std::max(s->cx, mi.rcMonitor.right - mi.rcMonitor.left);
        s->cy = std::max(s->cy, mi.rcMonitor.bottom - mi.rcMonitor.top);
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(mi.szDevice, ENUM_REGISTRY_SETTINGS, &dm)) {
            s->cx = std::max(s->cx, LONG(dm.dmPelsWidth));
            s->cy = std::max(s->cy, LONG(dm.dmPelsHeight));
        }
    }
    return TRUE;
}

D3DPRESENT_PARAMETERS Params() {
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_COPY;  // (Present takes a source rectangle)
    // The desktop's format, whatever it is (a windowed back buffer must suit it: a game run
    // with "reduced color mode" has a 16-bit desktop). The buffers drawn into are 32-bit.
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = g_r.bbW;
    pp.BackBufferHeight = g_r.bbH;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = g_r.deviceWindow;
    pp.PresentationInterval = g_config.vsync == kVSyncOff ? D3DPRESENT_INTERVAL_IMMEDIATE : D3DPRESENT_INTERVAL_ONE;
    if (g_r.defaultInterval) pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    return pp;
}

void ReleaseTextures() {
    for (auto& e : g_r.textures)
        if (e.second.tex) e.second.tex->Release();
    g_r.textures.clear();
    for (auto& s : g_r.spares) s.tex->Release();
    g_r.spares.clear();
    g_r.textureBytes = 0;
}

// The textures' memory is held to a budget (32-bit games: the address space, as managed
// textures keep a copy in system memory): the least recently used go, their Direct3D
// textures kept for reuse by the next ones of the same size.
constexpr size_t kTextureBudget = 96u << 20;
constexpr size_t kMaxSpares = 64;

void DropTexture(std::unordered_map<TexKey, TexEntry, TexKeyHash>::iterator it) {
    TexEntry& e = it->second;
    if (e.tex) {
        if (g_r.spares.size() < kMaxSpares) {
            g_r.spares.push_back({e.tex, e.w, e.h, e.levels});
        } else {
            e.tex->Release();
        }
    }
    g_r.textureBytes -= std::min(g_r.textureBytes, e.bytes);
    g_r.textures.erase(it);
}

void TrimTextures(unsigned maxAge, size_t budget) {
    for (auto it = g_r.textures.begin(); it != g_r.textures.end();) {
        auto next = std::next(it);
        if (g_r.frame - it->second.lastUse > maxAge) DropTexture(it);
        it = next;
    }
    if (g_r.textureBytes <= budget) return;
    GL_STUB("texture budget reached: least recently used textures dropped");
    // Over budget still: oldest first, down to three quarters of it.
    std::vector<std::pair<unsigned, TexKey>> byAge;
    byAge.reserve(g_r.textures.size());
    for (auto& e : g_r.textures) byAge.push_back({e.second.lastUse, e.first});
    std::sort(byAge.begin(), byAge.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& a : byAge) {
        if (g_r.textureBytes <= budget / 4 * 3 || a.first == g_r.frame) break;
        auto it = g_r.textures.find(a.second);
        if (it != g_r.textures.end()) DropTexture(it);
    }
}

void ReleaseTargets() {
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
}

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
// game's pixels (vpos / scale), and widened back the way the Voodoo's DAC shows it.
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
const char* kClearPs =
    "float4 c : register(c0);\n"
    "float4 main() : COLOR { return c; }\n";

bool MakeTargets() {
    IDirect3DDevice9* d = g_r.dev;
    for (auto*& c : g_r.color) {
        if (FAILED(d->CreateTexture(g_r.w, g_r.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &c,
                                    nullptr))) {
            c = nullptr;
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
    if (!g_r.depth) return false;
    if (FAILED(d->CreateTexture(GR_FOG_TABLE_SIZE, 1, 1, TextureUsage(), D3DFMT_A8R8G8B8, TexturePool(), &g_r.fogTex,
                                 nullptr)))
        g_r.fogTex = nullptr;
    // Black, and the depth buffer at its farthest.
    for (auto* c : g_r.color) {
        IDirect3DSurface9* s = nullptr;
        if (SUCCEEDED(c->GetSurfaceLevel(0, &s))) {
            d->ColorFill(s, nullptr, D3DCOLOR_ARGB(0, 0, 0, 0));
            s->Release();
        }
    }
    IDirect3DSurface9* old = nullptr;
    d->GetDepthStencilSurface(&old);
    d->SetDepthStencilSurface(g_r.depth);
    d->Clear(0, nullptr, D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL, 0, 1.0f, 0);
    d->SetDepthStencilSurface(old);
    if (old) old->Release();
    g_r.batchState = false;
    return true;
}

// Device lost (another program's mode change, the lock screen): back when it can be Reset.
bool DeviceUsable() {
    if (!g_r.dev) return false;
    const HRESULT hr = g_r.dev->TestCooperativeLevel();
    if (hr == D3D_OK) return true;
    if (hr != D3DERR_DEVICENOTRESET) return false;
    ReleaseTargets();
    D3DPRESENT_PARAMETERS pp = Params();
    if (FAILED(g_r.dev->Reset(&pp))) return false;
    GL_LOG("device reset after a loss");
    return MakeTargets();
}

// ---------------------------------------------------------------- state from the registers

D3DBLEND SrcBlend(FxU32 f) {
    switch (f) {
    case 0: return D3DBLEND_ZERO;
    case 1: return D3DBLEND_SRCALPHA;
    case 2: return D3DBLEND_DESTCOLOR;  // as a source factor 2 is the destination's color
    case 3: return D3DBLEND_DESTALPHA;
    case 4: return D3DBLEND_ONE;
    case 5: return D3DBLEND_INVSRCALPHA;
    case 6: return D3DBLEND_INVDESTCOLOR;
    case 7: return D3DBLEND_INVDESTALPHA;
    case 15: return D3DBLEND_SRCALPHASAT;
    default: return D3DBLEND_ZERO;
    }
}
D3DBLEND DstBlend(FxU32 f) {
    switch (f) {
    case 0: return D3DBLEND_ZERO;
    case 1: return D3DBLEND_SRCALPHA;
    case 2: return D3DBLEND_SRCCOLOR;  // as a destination factor 2 is the source's color
    case 3: return D3DBLEND_DESTALPHA;
    case 4: return D3DBLEND_ONE;
    case 5: return D3DBLEND_INVSRCALPHA;
    case 6: return D3DBLEND_INVSRCCOLOR;
    case 7: return D3DBLEND_INVDESTALPHA;
    case 15: return D3DBLEND_SRCCOLOR;  // PREFOG_COLOR: the same without fog (with fog: BlendInShader)
    default: return D3DBLEND_ZERO;
    }
}

void SetColor(int reg, FxU32 argb) {
    const float v[4] = {((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
                        (argb >> 24) / 255.0f};
    g_r.dev->SetPixelShaderConstantF(reg, v, 1);
}

IDirect3DSurface9* Target(int buffer) {  // GR_BUFFER_FRONTBUFFER / BACKBUFFER
    IDirect3DSurface9* s = nullptr;
    g_r.color[buffer == GR_BUFFER_FRONTBUFFER ? 0 : 1]->GetSurfaceLevel(0, &s);
    return s;
}

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

// A texture as a TMU's registers point at it, decoded from the texture memory when new or
// written since.
IDirect3DTexture9* BindTexture(int tmu) {
    TexDesc d;
    const TmuRegs& tr = St().r.tmu[tmu];
    if (!DescribeTexture(tmu, tr, &d)) return nullptr;
    TexKey key = {tmu, d.format, d.lodMin, d.lodMax, d.aspect, {d.base, ~0u, ~0u, ~0u}, d.tsplit, d.odd, d.tableHash};
    if (d.multibase) memcpy(key.base, d.bases, sizeof(key.base));
    const unsigned gen = SpanGeneration(tmu, d.spanStart, d.spanEnd);
    auto found = g_r.textures.find(key);
    if (found == g_r.textures.end()) {
        if (g_r.textureBytes > kTextureBudget) TrimTextures(~0u, kTextureBudget);
        found = g_r.textures.emplace(key, TexEntry{}).first;
    }
    TexEntry& e = found->second;
    e.lastUse = g_r.frame;
    if (e.tex && e.gen == gen) return e.tex;
    // Written since (a 4 KB page of it, maybe only by a texture beside it): decoded again
    // only if its own bytes changed.
    const TexMemory& mem = g_texMem[tmu];
    const FxU32 end = std::min<FxU32>(d.spanEnd, FxU32(mem.bytes.size()));
    const FxU8* src = mem.bytes.data() + d.spanStart;
    const size_t srcLen = end > d.spanStart ? end - d.spanStart : 0;
    if (e.tex && e.spanStart == d.spanStart && e.source.size() == srcLen && memcmp(e.source.data(), src, srcLen) == 0) {
        e.gen = gen;
        return e.tex;
    }
    const int levels = d.lodMax - d.lodMin + 1;
    if (!e.tex) {
        for (size_t i = 0; i < g_r.spares.size(); i++) {
            const SpareTex& sp = g_r.spares[i];
            if (sp.w == d.width && sp.h == d.height && sp.levels == levels) {
                e.tex = sp.tex;
                g_r.spares[i] = g_r.spares.back();
                g_r.spares.pop_back();
                break;
            }
        }
        if (!e.tex && FAILED(g_r.dev->CreateTexture(d.width, d.height, levels, TextureUsage(), D3DFMT_A8R8G8B8,
                                                    TexturePool(), &e.tex, nullptr))) {
            e.tex = nullptr;
            return nullptr;
        }
        e.w = d.width, e.h = d.height, e.levels = levels;
        e.bytes = size_t(d.width) * d.height * 4 * 4 / 3 * 2;  // the mipmaps, twice (managed)
        g_r.textureBytes += e.bytes;
    }
    e.spanStart = d.spanStart;
    e.source.assign(src, src + srcLen);
    e.gen = gen;
    if (!d.tsplit) {  // decoded straight into the texture
        for (int i = 0; i < levels; i++) {
            D3DLOCKED_RECT lr;
            if (FAILED(e.tex->LockRect(i, &lr, nullptr, 0))) continue;
            DecodeLevel(d, d.lodMin + i, static_cast<FxU32*>(lr.pBits), lr.Pitch / 4);
            e.tex->UnlockRect(i);
        }
        return e.tex;
    }
    std::vector<FxU32> prev;  // the level above, for one of the other parity (made from it)
    int prevW = 0, prevH = 0;
    for (int i = 0; i < levels; i++) {
        const int lod = d.lodMin + i;
        int lw, lh;
        LevelSize(lod, d.aspect, &lw, &lh);
        std::vector<FxU32> px(size_t(lw) * lh);
        const bool present = ((lod & 1) != 0) == d.odd;
        if (present || prev.empty()) {
            DecodeLevel(d, lod, px.data(), lw);
        } else {
            // A level of the other TMU (two-TMU trilinear): made from the one above.
            for (int y = 0; y < lh; y++)
                for (int x = 0; x < lw; x++) {
                    const int sx = std::min(prevW - 1, x * prevW / lw), sy = std::min(prevH - 1, y * prevH / lh);
                    px[size_t(y) * lw + x] = prev[size_t(sy) * prevW + sx];
                }
        }
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(e.tex->LockRect(i, &lr, nullptr, 0))) {
            for (int y = 0; y < lh; y++)
                memcpy(static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch, &px[size_t(y) * lw], lw * 4);
            e.tex->UnlockRect(i);
        }
        prev.swap(px);
        prevW = lw;
        prevH = lh;
    }
    return e.tex;
}

// The draw buffer, the depth buffer, the viewport and the clip window (always on: Glide
// enables it with grSstWinOpen), and the fixed states.
void ApplyTargets(const Regs& r) {
    IDirect3DDevice9* d = g_r.dev;
    IDirect3DSurface9* rt =
        Target((r.fbzMode & SST_DRAWBUFFER) == SST_DRAWBUFFER_FRONT ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER);
    d->SetRenderTarget(0, rt);
    if (rt) rt->Release();
    d->SetDepthStencilSurface(g_r.depth);
    const D3DVIEWPORT9 vp = {0, 0, DWORD(g_r.w), DWORD(g_r.h), 0.0f, 1.0f};
    d->SetViewport(&vp);
    const RECT clip = ClipRect(r);
    d->SetScissorRect(&clip);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, TRUE);
    d->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE);
    d->SetRenderState(D3DRS_LIGHTING, FALSE);
    d->SetRenderState(D3DRS_CLIPPING, FALSE);
    d->SetRenderState(D3DRS_STENCILENABLE, FALSE);
    d->SetRenderState(D3DRS_FOGENABLE, FALSE);
    d->SetVertexShader(nullptr);
}

// Sets everything a batch draws with from the registers.
void ApplyState() {
    IDirect3DDevice9* d = g_r.dev;
    const GlideState& st = St();
    const Regs& r = st.r;
    ApplyTargets(r);
    // Depth.
    const bool depth = (r.fbzMode & SST_ENDEPTHBUFFER) != 0;
    d->SetRenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
    d->SetRenderState(D3DRS_ZFUNC, D3DCMP_NEVER + ((r.fbzMode & SST_ZFUNC) >> SST_ZFUNC_SHIFT));
    // (An overlay, IsOverlay, doesn't write depth: the next laid on the same spot would fight
    // the first's as the first fought the surface's.)
    d->SetRenderState(D3DRS_ZWRITEENABLE, depth && (r.fbzMode & SST_ZAWRMASK) && !IsOverlay(r) ? TRUE : FALSE);
    d->SetRenderState(D3DRS_DEPTHBIAS, 0);  // (the shader writes the depth, the bias in it)
    // Writes: RGB, and alpha where the aux buffer is an alpha buffer.
    DWORD mask = (r.fbzMode & SST_RGBWRMASK)
                     ? D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE
                     : 0;
    if ((r.fbzMode & (SST_ENALPHABUFFER | SST_ZAWRMASK)) == (SST_ENALPHABUFFER | SST_ZAWRMASK) && !depth)
        mask |= D3DCOLORWRITEENABLE_ALPHA;
    d->SetRenderState(D3DRS_COLORWRITEENABLE, mask);
    // Blending.
    const bool shaderBlend = BlendInShader(r);
    const bool blend = (r.alphaMode & SST_ENALPHABLEND) != 0 && !shaderBlend;
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, blend);
    if (blend) {
        d->SetRenderState(D3DRS_SRCBLEND, SrcBlend((r.alphaMode & SST_RGBSRCFACT) >> SST_RGBSRCFACT_SHIFT));
        d->SetRenderState(D3DRS_DESTBLEND, DstBlend((r.alphaMode & SST_RGBDSTFACT) >> SST_RGBDSTFACT_SHIFT));
        d->SetRenderState(D3DRS_SEPARATEALPHABLENDENABLE, TRUE);
        d->SetRenderState(D3DRS_SRCBLENDALPHA, SrcBlend((r.alphaMode & SST_ASRCFACT) >> SST_ASRCFACT_SHIFT));
        d->SetRenderState(D3DRS_DESTBLENDALPHA, DstBlend((r.alphaMode & SST_ADSTFACT) >> SST_ADSTFACT_SHIFT));
        d->SetRenderState(D3DRS_BLENDOP, D3DBLENDOP_ADD);
    }
    // Alpha test.
    const bool atest = (r.alphaMode & SST_ENALPHAFUNC) != 0 && !shaderBlend;
    d->SetRenderState(D3DRS_ALPHATESTENABLE, atest);
    if (atest) {
        d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_NEVER + ((r.alphaMode & SST_ALPHAFUNC) >> SST_ALPHAFUNC_SHIFT));
        d->SetRenderState(D3DRS_ALPHAREF, (r.alphaMode & SST_ALPHAREF) >> SST_ALPHAREF_SHIFT);
    }
    // The pixel shader and its constants.
    const ShaderKey key = MakeShaderKey(st, g_board.numTmu);
    d->SetPixelShader(GetShader(d, key));
    d->SetFVF(kFvf);
    SetColor(kPsColor0, r.color0);
    SetColor(kPsColor1, r.color1);
    SetColor(kPsFogColor, r.fogColor);
    SetColor(kPsChromaKey, r.chromaKey);
    const float delta[4] = {st.ccR / 255.0f, st.ccG / 255.0f, st.ccB / 255.0f, st.ccA / 255.0f};
    d->SetPixelShaderConstantF(kPsDelta0, delta, 1);
    const float stip[4] = {float(r.stipple & 0xFF), float((r.stipple >> 8) & 0xFF), float((r.stipple >> 16) & 0xFF),
                           float(r.stipple >> 24)};
    d->SetPixelShaderConstantF(kPsStipple, stip, 1);
    const float tgt[4] = {1.0f / float(g_r.scale), float((r.alphaMode & SST_ALPHAREF) >> SST_ALPHAREF_SHIFT),
                          1.0f / float(g_r.w), 1.0f / float(g_r.h)};
    d->SetPixelShaderConstantF(kPsTarget, tgt, 1);
    // x: compare to bias, zaColor's depth as drawn; y: the depth bias (signed, Glide's units);
    // z: an overlay's pull forward (overlaybias steps).
    const float depthK[4] = {ClearDepth(FxU16(r.zaColor & SST_ZACOLOR_DEPTH), (r.fbzMode & SST_WBUFFER) != 0),
                             float(FxI16(r.zaColor & 0xFFFF)), float(g_config.overlayBias), 0};
    d->SetPixelShaderConstantF(kPsDepth, depthK, 1);
    if (shaderBlend && !g_r.dstCopy &&
        FAILED(d->CreateTexture(g_r.w, g_r.h, 1, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_r.dstCopy,
                                nullptr)))
        g_r.dstCopy = nullptr;
    d->SetTexture(3, shaderBlend ? g_r.dstCopy : nullptr);
    if (shaderBlend) {
        d->SetSamplerState(3, D3DSAMP_MINFILTER, D3DTEXF_POINT);
        d->SetSamplerState(3, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
        d->SetSamplerState(3, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
        d->SetSamplerState(3, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
        d->SetSamplerState(3, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    }
    // Fog table.
    if ((r.fogMode & SST_ENFOGGING) && g_r.fogTex &&
        (!g_r.fogValid || memcmp(g_r.fogLoaded, r.fogTable, GR_FOG_TABLE_SIZE))) {
        D3DLOCKED_RECT lr;
        if (SUCCEEDED(g_r.fogTex->LockRect(0, &lr, nullptr, 0))) {
            FxU32* px = static_cast<FxU32*>(lr.pBits);
            for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) px[i] = FxU32(r.fogTable[i]) << 24;
            g_r.fogTex->UnlockRect(0);
        }
        memcpy(g_r.fogLoaded, r.fogTable, GR_FOG_TABLE_SIZE);
        g_r.fogValid = true;
    }
    d->SetTexture(2, g_r.fogTex);
    d->SetSamplerState(2, D3DSAMP_MINFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(2, D3DSAMP_MAGFILTER, D3DTEXF_LINEAR);
    d->SetSamplerState(2, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(2, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
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
        d->SetTexture(t, tex);
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
            static const bool kAlpha[16] = {false, false, true, false, true, false, true, false,
                                            true,  true,  false, true, true, true, true, false};
            const bool cutout =
                kAlpha[(tr.textureMode & SST_TFORMAT) >> SST_TFORMAT_SHIFT] || (r.fbzMode & SST_ENCHROMAKEY);
            if (g_config.texLinear && !cutout) minF = magF = D3DTEXF_LINEAR;
            if (g_config.anisotropy > 1 && !g_r.anisoLimit) {
                D3DCAPS9 caps = {};
                g_r.anisoLimit = 1;
                if (SUCCEEDED(d->GetDeviceCaps(&caps)) && (caps.TextureFilterCaps & D3DPTFILTERCAPS_MINFANISOTROPIC))
                    g_r.anisoLimit = std::max<DWORD>(1, std::min<DWORD>(DWORD(g_config.anisotropy), caps.MaxAnisotropy));
                GL_LOG("anisotropy: %lu (the GPU: up to %lu)", static_cast<unsigned long>(g_r.anisoLimit),
                       static_cast<unsigned long>(caps.MaxAnisotropy));
            }
            // Only where the filtering is smooth: point (sharp on purpose) stays.
            if (g_r.anisoLimit > 1 && !cutout && minF == D3DTEXF_LINEAR) {
                minF = D3DTEXF_ANISOTROPIC;
                aniso = g_r.anisoLimit;
            }
        }
        d->SetSamplerState(t, D3DSAMP_MINFILTER, minF);
        d->SetSamplerState(t, D3DSAMP_MAGFILTER, magF);
        d->SetSamplerState(t, D3DSAMP_MAXANISOTROPY, aniso);
        const int lodMin = int(((tr.tLOD & SST_LODMIN) >> SST_LODMIN_SHIFT) >> 2),
                  lodMax = int(((tr.tLOD & SST_LODMAX) >> SST_LODMAX_SHIFT) >> 2);
        d->SetSamplerState(t, D3DSAMP_MIPFILTER,
                           lodMax <= lodMin                   ? D3DTEXF_NONE
                           : (tr.textureMode & SST_TRILINEAR) ? D3DTEXF_LINEAR
                                                              : D3DTEXF_POINT);
        int b6 = int((tr.tLOD & SST_LODBIAS) >> SST_LODBIAS_SHIFT);
        if (b6 & 0x20) b6 -= 0x40;
        float lodBias = b6 / 4.0f;
        d->SetSamplerState(t, D3DSAMP_MIPMAPLODBIAS, FloatBits(lodBias));
        d->SetSamplerState(t, D3DSAMP_ADDRESSU, (tr.tLOD & SST_LOD_TMIRRORS)     ? D3DTADDRESS_MIRROR
                                                : (tr.textureMode & SST_TCLAMPS) ? D3DTADDRESS_CLAMP
                                                                                 : D3DTADDRESS_WRAP);
        d->SetSamplerState(t, D3DSAMP_ADDRESSV, (tr.tLOD & SST_LOD_TMIRRORT)     ? D3DTADDRESS_MIRROR
                                                : (tr.textureMode & SST_TCLAMPT) ? D3DTADDRESS_CLAMP
                                                                                 : D3DTADDRESS_WRAP);
        // Texture coordinates are in 256ths of the texture's longer side.
        const int asp = int((tr.tLOD & SST_LOD_ASPECT) >> SST_LOD_ASPECT_SHIFT);
        const bool sWide = (tr.tLOD & SST_LOD_S_IS_WIDER) != 0;
        const float sRange = sWide ? 256.0f : 256.0f / float(1 << asp);
        const float tRange = sWide ? 256.0f / float(1 << asp) : 256.0f;
        const float scale[4] = {1.0f / sRange, 1.0f / tRange, sRange, tRange};
        d->SetPixelShaderConstantF(t == 0 ? kPsTexScale0 : kPsTexScale1, scale, 1);
        // Detail: the factor is ((bias - LOD) << scale) out of 255, at most detail max: bias in
        // whole LODs (signed 6 bits), the LOD the texel's (as MAME's Voodoo has it).
        int bias6 = int((tr.tDetail & SST_DETAIL_BIAS) >> SST_DETAIL_BIAS_SHIFT);
        if (bias6 & 0x20) bias6 -= 0x40;
        const float dk[4] = {float(bias6),
                             float(1 << ((tr.tDetail & SST_DETAIL_SCALE) >> SST_DETAIL_SCALE_SHIFT)) / 255.0f,
                             ((tr.tDetail & SST_DETAIL_MAX) >> SST_DETAIL_MAX_SHIFT) / 255.0f, 0};
        d->SetPixelShaderConstantF(t == 0 ? kPsDetail0 : kPsDetail1, dk, 1);
    }
}

// The depth value a vertex is drawn with: increasing with Glide's depth value (Z: 1/z as
// given, W: w), so that the depth functions are Glide's.
inline float DepthOf(const RVtx& v, bool wbuffer) {
    if (wbuffer) {
        const float oow = std::max(v.oow, 0.0f);
        return 1.0f / (1.0f + oow);
    }
    return std::clamp(v.ooz / 65536.0f, 0.0f, 1.0f);
}

// Which of a vertex's s, t and 1/w each TMU iterates, as Glide 2 sends them (3dfx's
// _grUpdateParamIndex): a TMU whose texture combine reads its texel is active; the FBI's 1/w
// goes to every TMU, a TMU's own only with the hint that it differs; TMU1's s, t are TMU0's
// unless they differ (the hint) or TMU0 isn't active (TMU1's own are sent then).
struct TmuParams {
    bool ownW0, ownW1, ownSt1;
};

void Convert(const RVtx& v, DVtx* o, bool wbuffer, bool flipY, const TmuParams& p) {
    const float s = float(g_r.scale);
    const float y = flipY ? float(g_board.screenH) - v.y : v.y;
    o->x = v.x * s - 0.5f;
    o->y = y * s - 0.5f;
    o->z = DepthOf(v, wbuffer);
    o->rhw = 1.0f;  // every attribute interpolated linearly on the screen, as the Voodoo iterates them
    o->col[0] = v.r;
    o->col[1] = v.g;
    o->col[2] = v.b;
    o->col[3] = v.a;
    const float q0 = p.ownW0 ? v.oowt[0] : v.oow;
    o->st0[0] = v.sow[0];
    o->st0[1] = v.tow[0];
    o->st0[2] = q0;
    o->st0[3] = 0;
    o->st1[0] = p.ownSt1 ? v.sow[1] : v.sow[0];
    o->st1[1] = p.ownSt1 ? v.tow[1] : v.tow[0];
    o->st1[2] = p.ownW1 ? v.oowt[1] : v.oow;
    o->st1[3] = 0;
    o->misc[0] = v.fog;
    o->misc[1] = std::clamp(v.ooz / 65535.0f, 0.0f, 1.0f);
    o->misc[2] = v.oow;
    o->misc[3] = v.ooz;
}

}  // namespace

// ---------------------------------------------------------------- open / close

bool RenderOpen(HWND hwnd) {
    RenderClose();
    g_r.hwnd = hwnd;
    g_r.scale = std::clamp(g_config.scale, 1, 8);
    g_r.w = g_board.screenW * g_r.scale;
    g_r.h = g_board.screenH * g_r.scale;
    SIZE big = {g_board.screenW, g_board.screenH};
    EnumDisplayMonitors(nullptr, nullptr, AddMonitor, reinterpret_cast<LPARAM>(&big));
    g_r.bbW = UINT(big.cx);
    g_r.bbH = UINT(big.cy);
    g_r.d3d = CreateD3D9();
    if (!g_r.d3d) {
        GL_LOG("no Direct3D 9");
        return false;
    }
    D3DDISPLAYMODE mode = {};
    if (SUCCEEDED(g_r.d3d->GetAdapterDisplayMode(D3DADAPTER_DEFAULT, &mode)))
        GL_LOG("display mode: %ux%u, %u Hz, format %d%s", mode.Width, mode.Height, mode.RefreshRate, int(mode.Format),
               mode.Format == D3DFMT_R5G6B5 || mode.Format == D3DFMT_X1R5G5B5 ? " (16-bit)" : "");
    // The device on the game's window; where Direct3D refuses that (on Windows: a window of
    // another thread's), on a window of this thread's, presenting into the game's; then a back
    // buffer only the frame's size; then Direct3D 9Ex the same ways. The refusals are logged
    // in one line, once a way works (or before the diagnosis, when none does).
    const DWORD flags = D3DCREATE_FPU_PRESERVE | D3DCREATE_MULTITHREADED | D3DCREATE_NOWINDOWCHANGES;
    HRESULT hr = E_FAIL;
    std::string refused;
    auto refuse = [&](HRESULT h, const char* how) {
        char b[96];
        snprintf(b, sizeof(b), "%s%s 0x%08lx", refused.empty() ? "" : ", ", how, static_cast<unsigned long>(h));
        refused += b;
    };
    static const char* const kHow[] = {"game window hw", "game window sw", "own window hw", "own window sw",
                                       "frame-size back buffer"};
    for (int attempt = 0; attempt < 5 && !g_r.dev; attempt++) {
        const bool onHelper = attempt >= 2;
        if (onHelper && !g_r.helper) {
            g_r.helper = WindowMakeHelper();
            if (!g_r.helper) {
                GL_LOG("CreateDevice: no window of its own (%lu)", GetLastError());
                break;
            }
        }
        if (attempt == 4) g_r.bbW = UINT(g_r.w), g_r.bbH = UINT(g_r.h);
        g_r.deviceWindow = onHelper ? g_r.helper : hwnd;
        const DWORD vp =
            attempt & 1 && attempt != 4 ? D3DCREATE_SOFTWARE_VERTEXPROCESSING : D3DCREATE_HARDWARE_VERTEXPROCESSING;
        D3DPRESENT_PARAMETERS pp = Params();
        hr = g_r.d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_r.deviceWindow, flags | vp, &pp, &g_r.dev);
        if (FAILED(hr)) {
            g_r.dev = nullptr;
            refuse(hr, kHow[attempt]);
        } else if (attempt) {
            GL_LOG("CreateDevice: refused %s; made: %s", refused.c_str(), kHow[attempt]);
        }
    }
    if (!g_r.dev) {
        // Direct3D 9Ex, the same ways.
        g_r.bbW = UINT(big.cx), g_r.bbH = UINT(big.cy);
        if (IDirect3D9Ex* ex = CreateD3D9Ex()) {
            for (int attempt = 0; attempt < 4 && !g_r.dev; attempt++) {
                const bool onHelper = attempt >= 2;
                if (onHelper && !g_r.helper) g_r.helper = WindowMakeHelper();
                g_r.deviceWindow = onHelper && g_r.helper ? g_r.helper : hwnd;
                const DWORD vp =
                    attempt & 1 ? D3DCREATE_SOFTWARE_VERTEXPROCESSING : D3DCREATE_HARDWARE_VERTEXPROCESSING;
                D3DPRESENT_PARAMETERS pp = Params();
                IDirect3DDevice9Ex* dev = nullptr;
                hr = ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, g_r.deviceWindow, flags | vp, &pp, nullptr,
                                        &dev);
                if (SUCCEEDED(hr) && dev) {
                    g_r.dev = dev;
                    g_r.ex = true;
                    g_r.d3d->Release();
                    g_r.d3d = ex;  // (an IDirect3D9 too)
                    ex = nullptr;
                    GL_LOG("CreateDevice: Direct3D 9 refused (%s); made: Direct3D 9Ex, %s", refused.c_str(),
                           kHow[attempt]);
                } else {
                    refuse(hr, (std::string("9Ex ") + kHow[attempt]).c_str());
                }
            }
            if (ex) ex->Release();
        }
    }
    if (!g_r.dev) {
        GL_LOG("CreateDevice: every way refused (%s)", refused.c_str());
        DiagnoseCreateDevice();
    }
    if (!g_r.dev) {
        RenderClose();
        return false;
    }
    D3DADAPTER_IDENTIFIER9 id = {};
    g_r.d3d->GetAdapterIdentifier(D3DADAPTER_DEFAULT, 0, &id);
    GL_LOG("D3D9 device: %s; buffers %dx%d (x%d), back buffer %ux%u", id.Description, g_r.w, g_r.h, g_r.scale, g_r.bbW,
           g_r.bbH);
    if (!MakeTargets()) {
        GL_LOG("could not make the frame buffer (%dx%d)", g_r.w, g_r.h);
        RenderClose();
        return false;
    }
    g_r.presentPs = CompileFixed(g_r.dev, kPresentPs);
    g_r.clearPs = CompileFixed(g_r.dev, kClearPs);
    g_r.sharpPs = CompileFixed(g_r.dev, kSharpPs);
    if (g_config.voodooLook) g_r.ditherPs = CompileFixed(g_r.dev, kDitherPs);
    g_r.batch.reserve(3 * 1024);
    return true;
}

void RenderClose() {
    g_r.batch.clear();
    g_r.batchState = false;
    ReleaseTextures();
    ReleaseTargets();
    ReleaseShaders();
    for (auto** ps : {&g_r.presentPs, &g_r.clearPs, &g_r.sharpPs, &g_r.auxReadPs, &g_r.copyPs, &g_r.ditherPs})
        if (*ps) (*ps)->Release(), *ps = nullptr;
    for (auto& e : g_r.lfbShaders)
        if (e.second) e.second->Release();
    g_r.lfbShaders.clear();
    if (g_r.dev) g_r.dev->Release(), g_r.dev = nullptr;
    if (g_r.d3d) g_r.d3d->Release(), g_r.d3d = nullptr;
    WindowDestroyHelper(g_r.helper);
    g_r.helper = g_r.deviceWindow = nullptr;
    g_r.defaultInterval = false;
    g_r.ex = false;
    g_r.anisoLimit = 0;
    g_r.hwnd = nullptr;
}

bool RenderReady() { return g_r.dev && g_r.color[0]; }

// ---------------------------------------------------------------- drawing

namespace {
void PointSampler(int stage);

// The batch's stipple rows into their texture (s4), 2048 a line, and where it is (c12).
void UploadStippleRows() {
    IDirect3DDevice9* d = g_r.dev;
    const size_t rows = g_r.stipRows.size() / 4;
    const int h = int((rows + 2047) / 2048);
    if (g_r.stipTex && g_r.stipTexH < h) g_r.stipTex->Release(), g_r.stipTex = nullptr;
    if (!g_r.stipTex) {
        const int th = std::max(h, 16);
        if (FAILED(d->CreateTexture(2048, th, 1, D3DUSAGE_DYNAMIC, D3DFMT_A32B32G32R32F, D3DPOOL_DEFAULT, &g_r.stipTex,
                                    nullptr))) {
            g_r.stipTex = nullptr;
            return;
        }
        g_r.stipTexH = th;
    }
    D3DLOCKED_RECT lr;
    if (FAILED(g_r.stipTex->LockRect(0, &lr, nullptr, D3DLOCK_DISCARD))) return;
    for (int y = 0; y < h; y++) {
        const size_t first = size_t(y) * 2048, n = std::min<size_t>(2048, rows - first);
        memcpy(static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch, &g_r.stipRows[first * 4], n * 16);
    }
    g_r.stipTex->UnlockRect(0);
    d->SetTexture(4, g_r.stipTex);
    PointSampler(4);
    const float k[4] = {float(g_board.screenH - 1), (g_r.batchRegs.fbzMode & SST_YORIGIN) ? 1.0f : 0.0f, 1.0f / 2048.0f,
                        1.0f / float(g_r.stipTexH)};
    d->SetPixelShaderConstantF(kPsStippleRows, k, 1);
}
}  // namespace

void RenderFlush() {
    if (g_r.batch.empty() || !RenderReady()) {
        g_r.batch.clear();
        g_r.stipRows.clear();
        return;
    }
    IDirect3DDevice9* d = g_r.dev;
    Dirty();
    if (!g_r.stipRows.empty()) UploadStippleRows();
    if (g_r.batchBlend && g_r.dstCopy) {
        // Each triangle over a copy of what it covers (it may cover the one before).
        IDirect3DSurface9* rt = nullptr;
        IDirect3DSurface9* copy = nullptr;
        d->GetRenderTarget(0, &rt);
        g_r.dstCopy->GetSurfaceLevel(0, &copy);
        for (size_t i = 0; i + 3 <= g_r.batch.size(); i += 3) {
            const DVtx* v = &g_r.batch[i];
            float x0 = std::min({v[0].x, v[1].x, v[2].x}), x1 = std::max({v[0].x, v[1].x, v[2].x});
            float y0 = std::min({v[0].y, v[1].y, v[2].y}), y1 = std::max({v[0].y, v[1].y, v[2].y});
            RECT rc = {LONG(std::floor(x0)), LONG(std::floor(y0)), LONG(std::ceil(x1)) + 2, LONG(std::ceil(y1)) + 2};
            rc.left = std::clamp<LONG>(rc.left, 0, g_r.w), rc.right = std::clamp<LONG>(rc.right, rc.left, g_r.w);
            rc.top = std::clamp<LONG>(rc.top, 0, g_r.h), rc.bottom = std::clamp<LONG>(rc.bottom, rc.top, g_r.h);
            if (rc.right <= rc.left || rc.bottom <= rc.top) continue;
            if (rt && copy) d->StretchRect(rt, &rc, copy, &rc, D3DTEXF_NONE);
            if (SUCCEEDED(d->BeginScene())) {
                d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, v, sizeof(DVtx));
                d->EndScene();
            }
        }
        if (copy) copy->Release();
        if (rt) rt->Release();
    } else if (SUCCEEDED(d->BeginScene())) {
        d->DrawPrimitiveUP(D3DPT_TRIANGLELIST, UINT(g_r.batch.size() / 3), g_r.batch.data(), sizeof(DVtx));
        d->EndScene();
    }
    g_r.batch.clear();
    if (!g_r.stipRows.empty()) {
        g_r.stipRows.clear();
        d->SetTexture(4, nullptr);
    }
}

// ---------------------------------------------------------------- stipple rotate mode

// The Voodoo's stipple rotate mode: the stipple register turns one bit for each pixel the
// rasterizer reaches (after clipping, before the other tests), row by row, left to right, from
// triangle to triangle, and bit 31 says whether the pixel is drawn; it starts over when the
// register is written. As MAME's Voodoo (0.220's sequential register: rotated left, then bit 31,
// so the k-th pixel since the write tests bit 30 - k; current MAME rotates right on a per-row
// copy, for its threads). A GPU draws in parallel, so the CPU walks each triangle's rows as the
// Voodoo does (MAME's poly_manager: vertices snapped to 1/16 by truncation, rows round(y1) to
// round(y3), each from round(x) on the long edge to round(x) on the other at the row's middle,
// round taking .5 down) and gives the shader every row's span and the bit of its first pixel.
constexpr bool kStippleRotateLeft = true;  // (false: current MAME's direction, pixel k tests bit k)

namespace {
int RoundCoordinate(float v) {
    const float ip = std::floor(v);
    return int(ip) + ((v - ip) > 0.5f ? 1 : 0);
}
float Snap16(float v) {  // the FIFO's float to 12.4 fixed point (MAME's float_to_int32: toward zero)
    return float(int(v * 16.0f)) * (1.0f / 16.0f);
}
}  // namespace

// The rows of a triangle (three vertices) into stipRows; first: its first entry and scanline.
// One empty row above and below the Voodoo's: Direct3D's rows of the triangle may reach one
// further, and those pixels aren't drawn.
void StippleRows(const RVtx* t, const Regs& r, float* firstEntry, float* firstScan) {
    struct P {
        float x, y;
    } a = {Snap16(t[0].x), Snap16(t[0].y)}, b = {Snap16(t[1].x), Snap16(t[1].y)}, c = {Snap16(t[2].x), Snap16(t[2].y)};
    const P* v1 = &a;
    const P* v2 = &b;
    const P* v3 = &c;
    if (v2->y < v1->y) std::swap(v1, v2);
    if (v3->y < v2->y) {
        std::swap(v2, v3);
        if (v2->y < v1->y) std::swap(v1, v2);
    }
    const int W = g_board.screenW, H = g_board.screenH;
    const int v1y = RoundCoordinate(v1->y), v3y = RoundCoordinate(v3->y);
    const int y0 = std::max(v1y, 0), y1 = std::min(v3y, H);
    const float d12 = v2->y == v1->y ? 0.0f : (v2->x - v1->x) / (v2->y - v1->y);
    const float d13 = v3->y == v1->y ? 0.0f : (v3->x - v1->x) / (v3->y - v1->y);
    const float d23 = v3->y == v2->y ? 0.0f : (v3->x - v2->x) / (v3->y - v2->y);
    const bool clip = (r.fbzMode & SST_ENRECTCLIP) != 0;
    const int cl = int((r.clipLeftRight >> SST_CLIPLEFT_SHIFT) & 0xFFF), cr = int(r.clipLeftRight & 0xFFF);
    const int ct = int((r.clipBottomTop >> SST_CLIPBOTTOM_SHIFT) & 0xFFF), cb = int(r.clipBottomTop & 0xFFF);
    const int top = std::min(y0, y1) - 1, bottom = std::max(y0, y1) + 1;  // (the empty rows around)
    *firstEntry = float(g_r.stipRows.size() / 4);
    *firstScan = float(top);
    const float step = kStippleRotateLeft ? -1.0f : 1.0f;
    for (int y = top; y < bottom; y++) {
        int sx = 0, ex = 0;
        if (y >= y0 && y < y1) {
            const float fy = float(y) + 0.5f;
            const float startx = v1->x + (fy - v1->y) * d13;
            const float stopx = fy < v2->y ? v1->x + (fy - v1->y) * d12 : v2->x + (fy - v2->y) * d23;
            sx = RoundCoordinate(startx), ex = RoundCoordinate(stopx);
            if (sx > ex) std::swap(sx, ex);
            sx = std::max(sx, 0), ex = std::min(ex, W);
            if (clip) {  // (the clip window relative to the origin, as the scissor: ClipRect)
                if (y < ct || y >= cb || sx >= cr) sx = ex = 0;
                ex = std::min(ex, cr);
                sx = std::max(sx, cl);
            }
            if (sx >= ex) sx = ex = 0;
        }
        const FxU32 k = g_r.stipCount;
        const float bit = float(kStippleRotateLeft ? (30u - k) & 31u : k & 31u);
        g_r.stipRows.insert(g_r.stipRows.end(), {float(sx), float(ex), bit, step});
        g_r.stipCount = (k + FxU32(ex - sx)) & 31u;
    }
}

// Pixels the pipeline wrote through the frame buffer: the register turns for them too (the
// pattern isn't applied to them: no game is known to stipple frame buffer writes, and current
// MAME warns of it as unexpected).
void RenderStippleAdvance(unsigned pixels) {
    const Regs& r = St().r;
    if ((r.fbzMode & (SST_ENSTIPPLE | SST_ENSTIPPLEPATTERN)) != SST_ENSTIPPLE) return;
    g_r.stipCount = (g_r.stipCount + pixels) & 31u;
}

namespace {
void DrawTriangles(const RVtx* v, int n);
}

// In pieces of 256 triangles: a batch, and rotate mode's row table, stay bounded however
// many a vertex array has.
void RenderTriangles(const RVtx* v, int n) {
    for (int i = 0; i + 3 <= n; i += 3 * 256) DrawTriangles(v + i, std::min(n - i, 3 * 256) / 3 * 3);
}

namespace {
void DrawTriangles(const RVtx* v, int n) {
    if (!RenderReady() || n < 3) return;
    const GlideState& st = St();
    // A state change ends the batch (the registers compared: cheap next to a draw call).
    const float cc[4] = {st.ccR, st.ccG, st.ccB, st.ccA};
    if (!g_r.batchState || memcmp(&g_r.batchRegs, &st.r, sizeof(Regs)) != 0 || g_r.batchDelta0 != (st.ccDelta0 != 0) ||
        (st.ccDelta0 && memcmp(g_r.batchCc, cc, sizeof(cc)) != 0)) {
        RenderFlush();
        if (!DeviceUsable()) return;
        ApplyState();
        g_r.batchRegs = st.r;
        g_r.batchDelta0 = st.ccDelta0 != 0;
        memcpy(g_r.batchCc, cc, sizeof(cc));
        g_r.batchBlend = BlendInShader(st.r);
        g_r.batchState = true;
    }
    const bool wbuffer = (st.r.fbzMode & SST_WBUFFER) != 0;
    const bool flipY = (st.r.fbzMode & SST_YORIGIN) != 0;
    const size_t at = g_r.batch.size();
    g_r.batch.resize(at + size_t(n));
    const FxU32 hints = st.paramHints;
    const bool active0 = TmuActive(st.r.tmu[0].textureMode);
    const bool active1 = g_board.numTmu > 1 && TmuActive(st.r.tmu[1].textureMode);
    const TmuParams p = {active0 && (hints & 0x2), active1 && (hints & 0x8), active1 && ((hints & 0x10) || !active0)};
    for (int i = 0; i < n; i++) Convert(v[i], &g_r.batch[at + i], wbuffer, flipY, p);
    if ((st.r.fbzMode & (SST_ENSTIPPLE | SST_ENSTIPPLEPATTERN)) == SST_ENSTIPPLE) {
        // Rotate mode: where the register is, then each triangle's rows (StippleRows).
        if (g_r.stipWrites != g_board.stippleWrites || g_r.stipPattern != st.r.stipple) {
            g_r.stipWrites = g_board.stippleWrites;
            g_r.stipPattern = st.r.stipple;
            g_r.stipCount = 0;
        }
        for (int i = 0; i + 3 <= n; i += 3) {
            float first, scan;
            StippleRows(v + i, st.r, &first, &scan);
            for (int j = 0; j < 3; j++) g_r.batch[at + i + j].st0[3] = first, g_r.batch[at + i + j].st1[3] = scan;
        }
    }
    if (g_r.batch.size() >= 3 * 4096 || g_r.stipRows.size() >= size_t(4) * 2048 * 256) RenderFlush();
}
}  // namespace

namespace {
// A quad over the target rectangle (pixels), at depth z.
void DrawQuad(const RECT& rc, float z, float u0 = 0, float v0 = 0, float u1 = 1, float v1 = 1) {
    struct V {
        float x, y, z, rhw, u, v;
    } q[4] = {{float(rc.left) - 0.5f, float(rc.top) - 0.5f, z, 1, u0, v0},
              {float(rc.right) - 0.5f, float(rc.top) - 0.5f, z, 1, u1, v0},
              {float(rc.left) - 0.5f, float(rc.bottom) - 0.5f, z, 1, u0, v1},
              {float(rc.right) - 0.5f, float(rc.bottom) - 0.5f, z, 1, u1, v1}};
    g_r.dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_TEX1);
    if (SUCCEEDED(g_r.dev->BeginScene())) {
        g_r.dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, q, sizeof(V));
        g_r.dev->EndScene();
    }
}

}  // namespace

void RenderClear(FxU32 argb, FxU8 alpha, FxU16 depthValue) {
    if (!RenderReady()) return;
    RenderFlush();
    if (!DeviceUsable()) return;
    Dirty();
    const Regs& r = St().r;
    IDirect3DDevice9* d = g_r.dev;
    ApplyTargets(r);
    g_r.batchState = false;  // (the state below is the clear's)
    const bool rgb = (r.fbzMode & SST_RGBWRMASK) != 0;
    const bool depth = (r.fbzMode & (SST_ENDEPTHBUFFER | SST_ZAWRMASK)) == (SST_ENDEPTHBUFFER | SST_ZAWRMASK);
    const bool alphaBuf = (r.fbzMode & (SST_ENALPHABUFFER | SST_ZAWRMASK)) == (SST_ENALPHABUFFER | SST_ZAWRMASK);
    DWORD mask = rgb ? D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE : 0;
    if (alphaBuf) mask |= D3DCOLORWRITEENABLE_ALPHA;
    d->SetRenderState(D3DRS_COLORWRITEENABLE, mask);
    d->SetRenderState(D3DRS_ZENABLE, depth ? D3DZB_TRUE : D3DZB_FALSE);
    d->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
    d->SetRenderState(D3DRS_ZWRITEENABLE, depth);
    const float zero = 0;
    d->SetRenderState(D3DRS_DEPTHBIAS, FloatBits(zero));
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetPixelShader(g_r.clearPs);
    const float c[4] = {((argb >> 16) & 0xFF) / 255.0f, ((argb >> 8) & 0xFF) / 255.0f, (argb & 0xFF) / 255.0f,
                        alpha / 255.0f};
    d->SetPixelShaderConstantF(0, c, 1);
    DrawQuad(ClipRect(r), ClearDepth(depthValue, (r.fbzMode & SST_WBUFFER) != 0));
}

namespace {
bool MakeTexture(IDirect3DTexture9** tex, int w, int h, DWORD usage, D3DFORMAT fmt, D3DPOOL pool);
void PointSampler(int stage);

// The front buffer into the window.
void Present() {
    IDirect3DDevice9* d = g_r.dev;
    HWND hwnd = g_r.hwnd;
    RECT client = {};
    if (!hwnd || !GetClientRect(hwnd, &client) || client.right <= 0 || client.bottom <= 0 || IsIconic(hwnd)) return;
    const UINT cw = std::min<UINT>(UINT(client.right), g_r.bbW), ch = std::min<UINT>(UINT(client.bottom), g_r.bbH);
    // voodoolook: the frame dithered to 16 bits first (into look), shown from there.
    IDirect3DTexture9* frame = g_r.color[0];
    if (g_r.ditherPs &&
        MakeTexture(&g_r.look, g_r.w, g_r.h, D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT)) {
        IDirect3DSurface9* ls = nullptr;
        if (SUCCEEDED(g_r.look->GetSurfaceLevel(0, &ls))) {
            d->SetRenderTarget(0, ls);
            d->SetDepthStencilSurface(nullptr);
            d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
            d->SetRenderState(D3DRS_ZENABLE, FALSE);
            d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
            d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
            d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
            d->SetPixelShader(g_r.ditherPs);
            const float k[4] = {1.0f / float(g_r.scale), 0, 0, 0};
            d->SetPixelShaderConstantF(0, k, 1);
            d->SetTexture(0, g_r.color[0]);
            PointSampler(0);
            DrawQuad(RECT{0, 0, g_r.w, g_r.h}, 0.0f);
            ls->Release();
            frame = g_r.look;
        }
    }
    IDirect3DSurface9* bb = nullptr;
    if (FAILED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) return;
    d->SetRenderTarget(0, bb);
    d->SetDepthStencilSurface(nullptr);
    const D3DVIEWPORT9 vp = {0, 0, cw, ch, 0.0f, 1.0f};
    d->SetViewport(&vp);
    const RECT whole = {0, 0, LONG(cw), LONG(ch)};
    d->ColorFill(bb, &whole, D3DCOLOR_XRGB(0, 0, 0));
    const RECT dst = ImageRect(int(cw), int(ch));
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    const int dw = dst.right - dst.left, dh = dst.bottom - dst.top;
    const bool linear = g_config.filter != 0 && (dw != g_r.w || dh != g_r.h);
    const bool sharp = linear && g_config.filter == 2 && g_r.sharpPs;
    d->SetPixelShader(sharp ? g_r.sharpPs : g_r.presentPs);
    const float k[4] = {1.0f / std::max(0.1f, g_r.gamma), 0, 0, 0};
    d->SetPixelShaderConstantF(0, k, 1);
    if (sharp) {
        const float sz[4] = {float(g_r.w), float(g_r.h), std::max(1.0f, float(dw) / float(g_r.w)),
                             std::max(1.0f, float(dh) / float(g_r.h))};
        d->SetPixelShaderConstantF(1, sz, 1);
    }
    d->SetTexture(0, frame);
    d->SetSamplerState(0, D3DSAMP_MINFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MAGFILTER, linear ? D3DTEXF_LINEAR : D3DTEXF_POINT);
    d->SetSamplerState(0, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
    DrawQuad(dst, 0.0f);
    d->SetTexture(0, nullptr);
    bb->Release();
    const RECT src = {0, 0, LONG(cw), LONG(ch)};
    d->Present(&src, nullptr, hwnd, nullptr);
    g_r.batchState = false;  // (the targets changed)
}
}  // namespace

namespace {
// The frame just shown (the front buffer, at its own resolution), gamma as shown, into a
// 24-bit BMP.
void SaveScreenshot() {
    IDirect3DDevice9* d = g_r.dev;
    if (!g_r.readback &&
        FAILED(d->CreateOffscreenPlainSurface(g_r.w, g_r.h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_r.readback, nullptr))) {
        g_r.readback = nullptr;
        return;
    }
    IDirect3DSurface9* rt = Target(GR_BUFFER_FRONTBUFFER);
    const HRESULT hr = rt ? d->GetRenderTargetData(rt, g_r.readback) : E_FAIL;
    if (rt) rt->Release();
    if (FAILED(hr)) return;
    const std::wstring path = ScreenshotPath();
    if (path.empty()) return;
    D3DLOCKED_RECT lr;
    if (FAILED(g_r.readback->LockRect(&lr, nullptr, D3DLOCK_READONLY))) return;
    FxU8 lut[256];
    const float k = 1.0f / std::max(0.1f, g_r.gamma);
    for (int i = 0; i < 256; i++) lut[i] = FxU8(std::lround(std::pow(i / 255.0f, k) * 255.0f));
    const int w = g_r.w, h = g_r.h, rowBytes = (w * 3 + 3) & ~3;
    std::vector<FxU8> file(54 + size_t(rowBytes) * h, 0);
    auto put32 = [&](size_t at, FxU32 v) { memcpy(&file[at], &v, 4); };
    file[0] = 'B', file[1] = 'M';
    put32(2, FxU32(file.size()));
    put32(10, 54);
    put32(14, 40);
    put32(18, FxU32(w));
    put32(22, FxU32(h));
    file[26] = 1, file[28] = 24;
    put32(34, FxU32(size_t(rowBytes) * h));
    for (int y = 0; y < h; y++) {  // (bottom-up)
        const FxU32* src = reinterpret_cast<const FxU32*>(static_cast<const FxU8*>(lr.pBits) + size_t(y) * lr.Pitch);
        FxU8* dst = &file[54 + size_t(h - 1 - y) * rowBytes];
        for (int x = 0; x < w; x++) {
            dst[x * 3 + 0] = lut[src[x] & 0xFF];
            dst[x * 3 + 1] = lut[(src[x] >> 8) & 0xFF];
            dst[x * 3 + 2] = lut[(src[x] >> 16) & 0xFF];
        }
    }
    g_r.readback->UnlockRect();
    char narrow[MAX_PATH * 3];
    WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, narrow, sizeof(narrow), nullptr, nullptr);
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD put = 0;
    const bool ok = f != INVALID_HANDLE_VALUE && WriteFile(f, file.data(), DWORD(file.size()), &put, nullptr) &&
                    put == file.size();
    const DWORD err = ok ? 0 : GetLastError();
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    if (ok) GL_LOG("screenshot: %s", narrow);
    else GL_LOG("screenshot: could not write %s (%lu)", narrow, err);
}

// The screenshot key, pressed (once per press) while the game has the focus.
bool ScreenshotKeyPressed() {
    static bool down;
    if (!g_config.screenshotKey) return false;
    const bool now = (GetAsyncKeyState(g_config.screenshotKey) & 0x8000) != 0;
    const bool pressed = now && !down;
    down = now;
    if (!pressed) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    return pid == GetCurrentProcessId();
}
}  // namespace

void RenderSwap(int interval) {
    (void)interval;
    if (!RenderReady()) return;
    RenderFlush();
    if (!DeviceUsable()) return;
    std::swap(g_r.color[0], g_r.color[1]);
    g_r.batchState = false;
    Present();
    if (ScreenshotKeyPressed()) SaveScreenshot();
    g_r.frame++;
    // Textures not used for a while go (the texture memory still holds them).
    if ((g_r.frame & 15) == 0) TrimTextures(300, kTextureBudget);
    // Once a minute or so: what the texture cache holds (for logs from games).
    if (g_r.frame % 3600 == 0)
        GL_LOG("frame %u: %u textures, %u MB, %u spare", g_r.frame, unsigned(g_r.textures.size()),
               unsigned(g_r.textureBytes >> 20), unsigned(g_r.spares.size()));
}

void RenderRefresh() {
    if (!RenderReady()) return;
    RenderFlush();
    if (DeviceUsable()) Present();
}

void RenderSetGamma(float gamma) { g_r.gamma = gamma > 0 ? gamma : 1.0f; }

void RenderTexturesChanged() {
    RenderFlush();
    g_r.batchState = false;
}

// ---------------------------------------------------------------- the linear frame buffer

namespace {
// Glide's 16-bit depth value from a depth as drawn (DepthOf / ClearDepth): Z or W, the same.
FxU16 GlideDepth(float z) { return FxU16(std::clamp(z * 65536.0f, 0.0f, 65535.0f)); }
bool MakeTexture(IDirect3DTexture9** tex, int w, int h, DWORD usage, D3DFORMAT fmt, D3DPOOL pool) {
    if (*tex) return true;
    if (FAILED(g_r.dev->CreateTexture(w, h, 1, usage, fmt, pool, tex, nullptr))) *tex = nullptr;
    return *tex != nullptr;
}

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
enum : unsigned { kLfbDepthOut = 1u << 16, kLfbChroma = 1u << 17, kLfbPipeline = 1u << 18 };
IDirect3DPixelShader9* LfbShader(uint64_t key) {
    auto it = g_r.lfbShaders.find(key);
    if (it != g_r.lfbShaders.end()) return it->second;
    const FxU32 f = key & 0xFFFF;              // the fog bits (fogMode's low ones)
    const FxU32 blend = FxU32(key >> 32);      // alphaMode's test and factors: blended here
    std::string s =
        "sampler2D col : register(s0);\nsampler2D aux : register(s1);\nsampler2D fogTab : register(s2);\n"
        "sampler2D dstTex : register(s3);\n"
        "float4 fogColor : register(c2);\nfloat4 chromaKey : register(c3);\nfloat4 target : register(c10);\n"
        "struct Out { float4 c : COLOR; float z : DEPTH; };\n";
    s += blend ? "Out main(float2 uv : TEXCOORD0, float2 vpos : VPOS) {\n" : "Out main(float2 uv : TEXCOORD0) {\n";
    s += "  Out o;\n"
         "  float4 d = tex2D(aux, uv);\n"
         "  clip(d.a - 0.5);\n"
         "  float4 t = tex2D(col, uv);\n"
         "  float3 c = t.rgb, prefog = c;\n"
         "  float a = t.a;\n";
    if (key & kLfbChroma) s += "  if (all(abs(c - chromaKey.rgb) < 0.5 / 255)) discard;\n";
    if (key & kLfbPipeline && f & SST_ENFOGGING) {
        if (f & SST_FOG_CONSTANT) {
            s += "  c = saturate(c + fogColor.rgb);\n";
        } else {
            if (f & SST_FOG_ALPHA) s += "  float fa = a;\n";
            else if (f & SST_FOG_Z) s += "  float fa = d.g;\n";
            else {
                s += "  float w = max(d.b, 1);\n  float k = floor(log2(w));\n  float r = w / exp2(k);\n";
                s += "  float fa = tex2D(fogTab, float2((clamp(4 * k + 8 - 8 / r, 0, 63) + 0.5) / 64, 0.5)).a;\n";
            }
            if ((f & SST_FOGMULT) && !(f & SST_FOGADD)) s += "  c = fa * fogColor.rgb;\n";
            else if ((f & SST_FOGADD) && !(f & SST_FOGMULT)) s += "  c = (1 - fa) * c;\n";
            else if (f & SST_FOGMULT) s += "  c = 0;\n";
            else s += "  c = lerp(c, fogColor.rgb, fa);\n";
        }
    }
    if (blend) s += BlendCode(blend, "tex2D(dstTex, (vpos + 0.5) * target.zw)");
    s += "  o.c = float4(c, a);\n  o.z = d.r;\n  return o;\n}\n";
    IDirect3DPixelShader9* ps = CompileFixed(g_r.dev, s.c_str());
    g_r.lfbShaders[key] = ps;
    return ps;
}

void PointSampler(int stage) {
    IDirect3DDevice9* d = g_r.dev;
    d->SetSamplerState(stage, D3DSAMP_MINFILTER, D3DTEXF_POINT);
    d->SetSamplerState(stage, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
    d->SetSamplerState(stage, D3DSAMP_MIPFILTER, D3DTEXF_NONE);
    d->SetSamplerState(stage, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
    d->SetSamplerState(stage, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
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
    rt->Release();  // (the targets hold it)
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
        if (g_r.readSmall && g_r.readSmallSys && SUCCEEDED(d->StretchRect(rt, nullptr, g_r.readSmall, nullptr,
                                                                          D3DTEXF_POINT))) {
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
    if (!g_r.auxReadback && FAILED(d->CreateOffscreenPlainSurface(W, H, D3DFMT_R32F, D3DPOOL_SYSTEMMEM,
                                                                  &g_r.auxReadback, nullptr))) {
        g_r.auxReadback = nullptr;
        return false;
    }
    // The depth texture drawn into floats (it can't be the depth buffer meanwhile).
    IDirect3DSurface9* rt = nullptr;
    g_r.auxRead->GetSurfaceLevel(0, &rt);
    d->SetRenderTarget(0, rt);
    d->SetDepthStencilSurface(nullptr);
    const D3DVIEWPORT9 vp = {0, 0, DWORD(W), DWORD(H), 0.0f, 1.0f};
    d->SetViewport(&vp);
    d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
    d->SetRenderState(D3DRS_ZENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
    d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
    d->SetRenderState(D3DRS_COLORWRITEENABLE, 0xF);
    d->SetPixelShader(g_r.auxReadPs);
    d->SetTexture(0, g_r.depthTex);
    PointSampler(0);
    DrawQuad(RECT{0, 0, W, H}, 0.0f);
    d->SetTexture(0, nullptr);
    g_r.batchState = false;
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
    const bool wbuffer = (r.fbzMode & SST_WBUFFER) != 0;
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
    const float constAux[3] = {ClearDepth(constDepth, wbuffer), constDepth / 65535.0f, GlideW(constDepth)};
    for (int j = 0; j < h; j++) {
        FxU32* crow = reinterpret_cast<FxU32*>(static_cast<uint8_t*>(lc.pBits) + size_t(j) * lc.Pitch);
        const size_t row = size_t(j) * w;
        if (plain) {
            for (int i = 0; i < w; i++) crow[i] = mask && !mask[row + i] ? 0 : argb[row + i] | 0xFF000000u;
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
                a[0] = ClearDepth(z, wbuffer), a[1] = z / 65535.0f, a[2] = GlideW(z);
            } else {
                a[0] = constAux[0], a[1] = constAux[1], a[2] = constAux[2];
            }
            a[3] = mask && !mask[k] ? 0.0f : 1.0f;
        }
    }
    if (!plain) g_r.uploadAux->UnlockRect(0);
    g_r.upload->UnlockRect(0);
    if (pipeline) ApplyState();  // the registers' state (the pipeline's), the rest set below
    else ApplyTargets(r);
    g_r.batchState = false;
    IDirect3DSurface9* rt = Target(buffer == GR_BUFFER_FRONTBUFFER ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER);
    d->SetRenderTarget(0, rt);
    if (rt) rt->Release();
    DWORD colorMask = 0;
    if (argb) colorMask |= D3DCOLORWRITEENABLE_RED | D3DCOLORWRITEENABLE_GREEN | D3DCOLORWRITEENABLE_BLUE;
    if (aux && alphaBuf) colorMask |= D3DCOLORWRITEENABLE_ALPHA;
    uint64_t key = 0;
    bool shaderBlend = false;
    if (pipeline) {
        // The registers decide: blending, the alpha and depth tests, the write masks, clipping.
        DWORD m = 0;
        d->GetRenderState(D3DRS_COLORWRITEENABLE, &m);
        d->SetRenderState(D3DRS_COLORWRITEENABLE, m & (colorMask | D3DCOLORWRITEENABLE_ALPHA));
        // PREFOG_COLOR with fog: blended in the shader against a copy of the target under the
        // rectangle (ApplyState made the copy texture, and turned Direct3D's blending and alpha
        // test off), as triangles are.
        shaderBlend = BlendInShader(r) && g_r.dstCopy;
        if (shaderBlend) {
            key |= uint64_t(r.alphaMode & (SST_ENALPHAFUNC | SST_ALPHAFUNC | SST_RGBSRCFACT | SST_RGBDSTFACT |
                                           SST_ASRCFACT | SST_ADSTFACT))
                   << 32;
        } else if (r.alphaMode & SST_ENALPHAFUNC) {
            d->SetRenderState(D3DRS_ALPHATESTENABLE, TRUE);
            d->SetRenderState(D3DRS_ALPHAFUNC, D3DCMP_NEVER + ((r.alphaMode & SST_ALPHAFUNC) >> SST_ALPHAFUNC_SHIFT));
            d->SetRenderState(D3DRS_ALPHAREF, (r.alphaMode & SST_ALPHAREF) >> SST_ALPHAREF_SHIFT);
        }
        d->SetRenderState(D3DRS_DEPTHBIAS, 0);
        key |= kLfbPipeline | kLfbDepthOut | (r.fogMode & 0xFFFF) |
               ((r.fbzMode & SST_ENCHROMAKEY) ? unsigned(kLfbChroma) : 0u);
    } else {
        d->SetRenderState(D3DRS_ALPHABLENDENABLE, FALSE);
        d->SetRenderState(D3DRS_ALPHATESTENABLE, FALSE);
        d->SetRenderState(D3DRS_COLORWRITEENABLE, colorMask);
        d->SetRenderState(D3DRS_SCISSORTESTENABLE, FALSE);
        d->SetRenderState(D3DRS_ZENABLE, depthData ? D3DZB_TRUE : D3DZB_FALSE);
        d->SetRenderState(D3DRS_ZFUNC, D3DCMP_ALWAYS);
        d->SetRenderState(D3DRS_ZWRITEENABLE, depthData);
        d->SetRenderState(D3DRS_DEPTHBIAS, 0);
        key = kLfbDepthOut;
    }
    if (plain && !g_r.copyPs) g_r.copyPs = CompileFixed(d, kCopyPs);
    d->SetPixelShader(plain ? g_r.copyPs : LfbShader(key));
    d->SetTexture(0, g_r.upload);
    d->SetTexture(1, plain ? nullptr : g_r.uploadAux);
    PointSampler(0);
    PointSampler(1);
    const int s = g_r.scale;
    const RECT rc = {x * s, y * s, (x + w) * s, (y + h) * s};
    if (shaderBlend) {
        IDirect3DSurface9* t = Target(buffer == GR_BUFFER_FRONTBUFFER ? GR_BUFFER_FRONTBUFFER : GR_BUFFER_BACKBUFFER);
        IDirect3DSurface9* copy = nullptr;
        if (t && SUCCEEDED(g_r.dstCopy->GetSurfaceLevel(0, &copy))) {
            d->StretchRect(t, &rc, copy, &rc, D3DTEXF_NONE);
            copy->Release();
        }
        if (t) t->Release();
        d->SetTexture(3, g_r.dstCopy);
        PointSampler(3);
    } else {
        d->SetTexture(3, nullptr);
    }
    DrawQuad(rc, 0.0f, 0, 0, float(w) / g_r.uploadW, float(h) / g_r.uploadH);
    d->SetTexture(0, nullptr);
    d->SetTexture(1, nullptr);
    d->SetTexture(3, nullptr);
    // Shown at once when it went to the front buffer.
    if (buffer == GR_BUFFER_FRONTBUFFER) Present();
}

}  // namespace gl2d9
