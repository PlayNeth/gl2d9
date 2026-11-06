// gl2d9 - 3dfx Glide (2.1, 2.x, 3.x) on top of Direct3D 9.
// Shared declarations: configuration, logging, the global lock.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace gl2d9 {

#define GL2D9_VERSION "1.1.1"

enum { kDisplaySwitch, kDisplayWindow, kDisplayBorderless };
enum { kVSyncAuto, kVSyncOn, kVSyncOff };

// Which Glide this DLL is (the name it was built as): what it reports and exports.
enum GlideVersion { kGlide21, kGlide2x, kGlide3x };
extern GlideVersion g_version;

struct Config {
    int display = kDisplayBorderless;  // switch: real mode switch; window; borderless: the screen covered, scaled
    int stretch = 0;                   // window & borderless scaling: 0 aspect, 1 fill, 2 integer
    int filter = 1;                    // the frame scaled to the screen: 0 point, 1 linear, 2 sharp
    int vsync = kVSyncAuto;            // auto: grBufferSwap's interval; on / off: forced
    int scale = 1;                     // the game's resolution drawn N times larger on the GPU (1-8)
    bool systemD3D9 = true;            // Windows' own d3d9.dll, not one in the game folder (DXVK)
    int tmuMB = 0;                     // texture memory a TMU reports (and games can use), MB; 0 = auto
    int fbMB = 4;                      // frame buffer memory reported, MB
    int tmus = 2;                      // texture units reported (1 or 2, every Glide)
    bool shaderCache = true;           // compiled shaders kept in gl2d9.shaders (no compiling next time)
    bool filterResolutions = true;     // resolution lists games carry themselves: the monitor's only
    bool voodooLook = false;           // the picture shown dithered to 16 bits, as the Voodoo's was
    int overlayBias = 1;               // passes laid on drawn surfaces pulled forward N depth steps (shader.cpp)
    int fpsLimit = 0;                  // frames per second at most (0: off), paced at the buffer swap
    int cpus = 1;                      // processors the game runs on (0: all): one, as the games' PCs had
    bool texLinear = false;            // point-filtered textures drawn bilinear (cut-outs keep theirs)
    int anisotropy = 0;                // anisotropic filtering on smoothly filtered textures (0, 2-16)
    int log = 0;                       // 0 off, 1 info, 2 trace (every call)
    std::wstring logFile;              // empty: gl2d9.log next to the game's executable
};
extern Config g_config;
void LoadConfig(HMODULE self);
// Where the shader cache goes: beside this DLL, or (that folder not writable) in
// %LOCALAPPDATA%\gl2d9 under the game's name. Empty: nowhere.
std::wstring ShaderCachePath();

// ---------------------------------------------------------------- logging
void LogInit();
void ApplyCpus();                      // cpus=: the processors the process runs on (after LogInit: logged)
void LimitFrame();                     // fpslimit=: waits out the rest of the frame's time (at the buffer swap)
void LimitSwapInterval(int interval);  // vsync=auto: grBufferSwap(2 or more), frames that many refreshes apart
extern int g_logLevel;                 // (util.cpp) read at every log and trace site: inline
inline int LogLevel() { return g_logLevel; }
void Log(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void LogFlush(bool processExit);
void CrashLoggerRemove();  // before the DLL is unloaded (FreeLibrary)

#define GL_LOG(...)                                                 \
    do {                                                            \
        if (::gl2d9::LogLevel() >= 1) ::gl2d9::Log(1, __VA_ARGS__); \
    } while (0)
#define GL_TRACE(...)                                               \
    do {                                                            \
        if (::gl2d9::LogLevel() >= 2) ::gl2d9::Log(2, __VA_ARGS__); \
    } while (0)
// Logs the first call of an unimplemented or partially implemented path.
#define GL_STUB(what)                 \
    do {                              \
        static bool once_;            \
        if (!once_) {                 \
            once_ = true;             \
            GL_LOG("stub: %s", what); \
        }                             \
    } while (0)

// ---------------------------------------------------------------- global lock
// Glide is single-threaded by contract, but the window procedure (focus changes) and the
// presentation of a frame can meet a game's rendering thread: every entry point that
// touches the device or the state takes this.
extern CRITICAL_SECTION g_lock;
struct Guard {
    Guard() { EnterCriticalSection(&g_lock); }
    ~Guard() { LeaveCriticalSection(&g_lock); }
    Guard(const Guard&) = delete;
    Guard& operator=(const Guard&) = delete;
};

// An export's call that failed with a C++ exception (memory run out: a 32-bit game's address
// space, Unreal's): logged, what it left half done dropped, and the call returned as failed.
// The exception must not reach the game: it can't unwind through the game's frames (the game
// aborted, "requested the Runtime to terminate"). Called from the export's handler, after
// the lock was released by unwinding. With a value: that returned (the call's failure).
void CallFailed(const char* where);
template <class T>
T CallFailed(const char* where, T value) {
    CallFailed(where);
    return value;
}
// A Glide export: GLAPI FxBool grFoo(...) try { ENTRY("grFoo"); ... } catch (...) { return
// CallFailed(__func__, FXFALSE); }. ENTRY takes gl2d9's lock for the call (released before the
// handler runs, by unwinding) and traces it.
#define GLAPI extern "C" __attribute__((stdcall))
#define ENTRY(name)        \
    ::gl2d9::Guard guard_; \
    GL_TRACE("%s", name)

}  // namespace gl2d9
