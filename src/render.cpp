// The renderer: the Voodoo's frame buffer as Direct3D 9 render targets (front and back color
// buffers, a depth buffer), its triangles drawn with the pixel shaders shader.cpp makes from
// the registers, its textures decoded from the emulated texture memory, and the front
// buffer presented into the game's window. This file: the device, made, Reset, made anew and
// closed (renderint.h lists the others).
#include "renderint.h"

#include <tlhelp32.h>
#include <algorithm>
#include <cstdio>
#include <string>

#include "shader.h"
#include "window.h"

namespace gl2d9 {

Renderer g_r;

namespace {

using CreateFn = IDirect3D9*(WINAPI*)(UINT);

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

}  // namespace

// Textures the game's data is decoded into: managed, or (9Ex has no managed pool) dynamic in
// video memory (9Ex devices don't lose it).
D3DPOOL TexturePool() { return g_r.ex ? D3DPOOL_DEFAULT : D3DPOOL_MANAGED; }
DWORD TextureUsage() { return g_r.ex ? D3DUSAGE_DYNAMIC : 0; }
// Locking a one-level texture made so, every texel rewritten: a dynamic one (9Ex) discarded,
// not waited for while the GPU may still be drawing with it. (Not one with mipmaps: Wine's
// Direct3D discarded the levels already written with it, and the 3dfx tests' font came out
// in pieces.)
DWORD RewriteLock() { return g_r.ex ? D3DLOCK_DISCARD : 0; }

namespace {

// Every device made the usual ways refused (9 and 9Ex): what might explain it logged (Windows
// as it is and as the game is told, the adapter, the modules loaded: an overlay or another
// d3d9.dll), and other ways tried, each logged. The first that gives a usable device is kept.

void DiagnoseCreateDevice() {
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    OSVERSIONINFOW real = {}, told = {};
    real.dwOSVersionInfoSize = told.dwOSVersionInfoSize = sizeof(OSVERSIONINFOW);
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
        MODULEENTRY32W me = {};
        me.dwSize = sizeof(me);
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
    // Other ways. (Each made on gl2d9's own window, the back buffer the game window's size.)
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
            if (w.usable && !g_r.dev) {
                g_r.dev = dev;
                // (Better than no picture, but Direct3D then sets the x87 to single precision
                // at its calls, under the game's own doubles: timing and physics may suffer.)
                if (!(w.flags & D3DCREATE_FPU_PRESERVE))
                    GL_LOG(
                        "diagnosis: kept a device made without FPU_PRESERVE: the game's floating point may "
                        "lose precision");
            } else {
                dev->Release();
            }
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
            if (usable && !g_r.dev) g_r.dev = dev;
            else dev->Release();
        }
        return SUCCEEDED(hr);
    };
    D3DPRESENT_PARAMETERS pp = Params();
    pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    const bool hadDevice = g_r.dev != nullptr;
    if (tryParams("interval default", pp, true) && !hadDevice) g_r.defaultInterval = true;
    pp = Params();
    pp.SwapEffect = D3DSWAPEFFECT_COPY;
    tryParams("swap effect copy", pp, false);
    pp = Params();
    pp.BackBufferWidth = pp.BackBufferHeight = 0;
    tryParams("back buffer of the window's size", pp, false);
    pp = Params();
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    tryParams("back buffer X8R8G8B8", pp, false);
    GL_LOG("diagnosis: %s", g_r.dev ? "a device made another way: used" : "no way gave a device");
}

}  // namespace

D3DPRESENT_PARAMETERS Params() {
    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed = TRUE;
    // The whole back buffer drawn every Present. (The helper's own: copy, for a part of it
    // presented into the game's window where a swap chain there is refused.)
    pp.SwapEffect = g_r.deviceWindow && g_r.deviceWindow == g_r.helper ? D3DSWAPEFFECT_COPY : D3DSWAPEFFECT_DISCARD;
    // The desktop's format, whatever it is (a windowed back buffer must suit it: a game run
    // with "reduced color mode" has a 16-bit desktop). The buffers drawn into are 32-bit.
    pp.BackBufferFormat = D3DFMT_UNKNOWN;
    pp.BackBufferWidth = g_r.bbW;
    pp.BackBufferHeight = g_r.bbH;
    pp.BackBufferCount = 1;
    pp.hDeviceWindow = g_r.deviceWindow;
    // (Under the desktop compositor gl2d9 paces the frames itself: Renderer::compositor.)
    pp.PresentationInterval = g_config.vsync == kVSyncOff || g_r.compositor || g_r.swapImmediate
                                  ? D3DPRESENT_INTERVAL_IMMEDIATE
                                  : D3DPRESENT_INTERVAL_ONE;
    if (g_r.defaultInterval) pp.PresentationInterval = D3DPRESENT_INTERVAL_DEFAULT;
    return pp;
}

// The size to present at: the game window's client area (kept while it is minimized or has
// none), within what a back buffer can be.
SIZE PresentSize() {
    RECT c = {};
    if (g_r.hwnd && !IsIconic(g_r.hwnd) && GetClientRect(g_r.hwnd, &c) && c.right > 0 && c.bottom > 0)
        return SIZE{std::min<LONG>(c.right, 16384), std::min<LONG>(c.bottom, 16384)};
    if (g_r.bbW && g_r.bbH) return SIZE{LONG(g_r.bbW), LONG(g_r.bbH)};
    return SIZE{std::max(g_board.screenW, 1), std::max(g_board.screenH, 1)};
}

// The device Reset at the size PresentSize gives (lost, or the window resized). Whatever is
// in video memory goes first (Reset refuses a device with any alive: gl2d9's targets, the
// swap chain on the game window; a hook's own, such as OBS's capture texture, are its to
// free in its Reset hook). A refusal is logged with its code, and tried again a quarter
// second on at the soonest: never a black picture with nothing said. The front buffer is
// kept across it where the device can still read it (a resize: the picture stays).
bool ResetDevice(const char* why) {
    IDirect3DDevice9* d = g_r.dev;
    const DWORD now = GetTickCount();
    if (g_r.resetFailures && now - g_r.resetFailedAt < 250) return false;
    EndDraw();
    // The front buffer, read back while it can be.
    IDirect3DSurface9* keep = nullptr;
    if (g_r.color[0] && d->TestCooperativeLevel() == D3D_OK) {
        IDirect3DSurface9* src = nullptr;
        if (SUCCEEDED(g_r.color[0]->GetSurfaceLevel(0, &src))) {
            if (FAILED(
                    d->CreateOffscreenPlainSurface(g_r.w, g_r.h, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &keep, nullptr)) ||
                FAILED(d->GetRenderTargetData(src, keep))) {
                if (keep) keep->Release();
                keep = nullptr;
            }
            src->Release();
        }
    }
    // What the device itself holds goes too (bound, a Direct3D 9 device's render targets and
    // textures in the default pool stayed alive, and Reset refused).
    for (DWORD i = 0; i < 8; i++) BindStage(i, nullptr);
    UseDepth(nullptr);
    IDirect3DSurface9* bb = nullptr;
    if (SUCCEEDED(d->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &bb))) {
        UseTarget(bb);
        bb->Release();
    }
    ReleaseChain();
    ReleaseTargets();
    const SIZE size = PresentSize();
    const UINT oldW = g_r.bbW, oldH = g_r.bbH;
    g_r.bbW = UINT(size.cx), g_r.bbH = UINT(size.cy);
    g_r.compositor = CompositorPaced();
    D3DPRESENT_PARAMETERS pp = Params();
    const HRESULT hr = d->Reset(&pp);
    ForgetDeviceState();  // (Reset puts every state back to its default)
    if (FAILED(hr)) {
        // The back buffer is still the old size: kept so, Present sees the size still to
        // change and asks again (a 9Ex device isn't lost by a refused Reset, and nothing else
        // would).
        g_r.bbW = oldW, g_r.bbH = oldH;
        g_r.resetFailedAt = now;
        // Logged the first time, then every 40th (about every ten seconds).
        if (g_r.resetFailures++ % 40 == 0)
            GL_LOG("device: Reset (%s, %ux%u) refused: 0x%08lx%s", why, pp.BackBufferWidth, pp.BackBufferHeight,
                   static_cast<unsigned long>(hr), g_r.resetFailures > 1 ? " (still)" : "");
        if (keep) keep->Release();
        // Refused for two seconds: something else holds video memory on the device (a
        // capture or overlay hook not freeing its own in its Reset hook). A new device holds
        // none of it, and the hook starts again on it.
        if (g_r.resetFailures >= 8) g_r.remake = true;
        return false;
    }
    if (g_r.resetFailures) GL_LOG("device: Reset (%s) done after %u refusal(s)", why, g_r.resetFailures);
    else GL_LOG("device: Reset (%s): back buffer %ux%u", why, g_r.bbW, g_r.bbH);
    g_r.resetFailures = 0;
    const bool made = MakeTargets();
    if (made && keep) {
        IDirect3DSurface9* dst = nullptr;
        if (SUCCEEDED(g_r.color[0]->GetSurfaceLevel(0, &dst))) {
            d->UpdateSurface(keep, nullptr, dst, nullptr);
            dst->Release();
        }
    }
    if (keep) keep->Release();
    return made;
}

// Device lost (another program's mode change, the lock screen): back when it can be Reset.
// Targets missing (a Reset or their making failed before): made again, at most every quarter
// second (else the screen stayed black until the game opened Glide again).
namespace {
bool RemakeDevice();
}  // namespace

bool DeviceUsable() {
    // Made anew, from the thread that made it (it is that thread's), where it must be.
    if (g_r.remake) return GetCurrentThreadId() == g_r.devThread && RemakeDevice();
    if (!g_r.dev) return false;
    HRESULT hr = g_r.dev->TestCooperativeLevel();
    // 9Ex devices aren't lost (TestCooperativeLevel always succeeds): removed or hung is
    // what can happen to them.
    if (g_r.ex && SUCCEEDED(hr)) {
        hr = static_cast<IDirect3DDevice9Ex*>(g_r.dev)->CheckDeviceState(g_r.deviceWindow);
        if (hr == D3DERR_DEVICEREMOVED || hr == D3DERR_DEVICEHUNG) {
            GL_LOG("device: %s: made anew", hr == D3DERR_DEVICEREMOVED ? "removed" : "hung");
            g_r.remake = true;
            return false;
        }
        // Any other failure (out of video memory, say) would leave nothing drawn for good.
        if (FAILED(hr) && hr != D3DERR_DEVICELOST) {
            GL_LOG("device: state 0x%08lx: made anew", static_cast<unsigned long>(hr));
            g_r.remake = true;
            return false;
        }
        if (SUCCEEDED(hr)) hr = D3D_OK;  // (occluded, a mode change: drawn anyway)
    }
    if (hr == D3D_OK) {
        if (g_r.color[0]) return true;
        const DWORD now = GetTickCount();
        if (g_r.targetsTriedAt && now - g_r.targetsTriedAt < 250) return false;
        g_r.targetsTriedAt = now | 1;
        if (!MakeTargets()) return false;
        GL_LOG("render targets made again");
        return true;
    }
    if (hr == D3DERR_DRIVERINTERNALERROR) {
        GL_LOG("device: driver internal error: made anew");
        g_r.remake = true;
        return false;
    }
    // Reset by the thread that made the device (from another, such as the window's while it
    // repaints, the next frame of the game's does it).
    if (hr != D3DERR_DEVICENOTRESET || GetCurrentThreadId() != g_r.devThread) return false;
    return ResetDevice("device lost");
}

// ---------------------------------------------------------------- open / close

bool RenderOpen(HWND hwnd) {
    RenderClose();
    g_r.hwnd = hwnd;
    g_r.scale = std::clamp(g_config.scale, 1, 8);
    g_r.w = g_board.screenW * g_r.scale;
    g_r.h = g_board.screenH * g_r.scale;
    const SIZE first = WindowClientSize();  // (the window may still be on its way there)
    g_r.bbW = UINT(first.cx);
    g_r.bbH = UINT(first.cy);
    g_r.devThread = GetCurrentThreadId();
    g_r.compositor = CompositorPaced();
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
        g_r.bbW = UINT(first.cx), g_r.bbH = UINT(first.cy);
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
    PrewarmShaders(g_r.dev);
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
    ReleaseChain();
    g_r.chainRefused = false;
    if (g_r.dev) EndDraw(), g_r.dev->Release(), g_r.dev = nullptr;
    ForgetDeviceState();
    if (g_r.d3d) g_r.d3d->Release(), g_r.d3d = nullptr;
    g_r.bbW = g_r.bbH = 0;
    g_r.resetFailures = 0;
    g_r.targetsTriedAt = g_r.remakeTriedAt = 0;
    g_r.remakeFailures = 0;
    g_r.remake = false;  // (RemakeDevice asks again itself where its RenderOpen fails)
    g_r.batchPs = g_r.biasWritePs = nullptr;
    WindowDestroyHelper(g_r.helper);
    g_r.helper = g_r.deviceWindow = nullptr;
    g_r.defaultInterval = false;
    g_r.ex = false;
    g_r.anisoLimit = 0;
    g_r.hwnd = nullptr;
}

namespace {

// The device made anew on the game's window (Renderer::remake): everything on it goes and is
// made again (textures are decoded again from the texture memory as they are used; the frame
// buffer starts black, as after a loss). Tried at most once a second.
bool RemakeDevice() {
    const DWORD now = GetTickCount();
    if (g_r.remakeTriedAt && now - g_r.remakeTriedAt < 1000) return false;
    const DWORD triedAt = now | 1;
    unsigned failures = g_r.remakeFailures;
    HWND hwnd = g_r.hwnd;
    if (!failures) GL_LOG("device: made anew");
    g_r.remake = false;
    const bool made = RenderOpen(hwnd);  // (its RenderClose resets the two below)
    g_r.remakeTriedAt = triedAt;
    if (!made) {
        // Tried again a second on (RenderOpen left no device: kept asking for one).
        if (!failures++) GL_LOG("device: could not be made anew: tried again each second");
        g_r.remakeFailures = failures;
        g_r.hwnd = hwnd;
        g_r.remake = true;
        return false;
    }
    if (failures) GL_LOG("device: made anew after %u tries", failures + 1);
    return g_r.color[0] != nullptr;
}

}  // namespace

// (The targets lost to a failed Reset: made again here, DeviceUsable.)
bool RenderReady() {
    if (!g_r.dev && !g_r.remake) return false;
    return (g_r.dev && g_r.color[0]) || (DeviceUsable() && g_r.color[0]);
}

}  // namespace gl2d9
