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

#define GL2D9_VERSION "0.1.0"

enum { kDisplaySwitch, kDisplayWindow, kDisplayBorderless };
enum { kVSyncAuto, kVSyncOn, kVSyncOff };

// Which Glide this DLL is (the name it was built as): what it reports and exports.
enum GlideVersion { kGlide21, kGlide2x, kGlide3x };
extern GlideVersion g_version;

struct Config {
    int display = kDisplayBorderless;  // switch: real mode switch; window; borderless: the screen covered, scaled
    int stretch = 0;                   // window & borderless scaling: 0 aspect, 1 fill, 2 integer
    int filter = 0;                    // the frame scaled to the screen: 0 point, 1 linear, 2 sharp
    int vsync = kVSyncAuto;            // auto: grBufferSwap's interval; on / off: forced
    int scale = 1;                     // the game's resolution drawn N times larger on the GPU (1-8)
    bool systemD3D9 = true;            // Windows' own d3d9.dll, not one in the game folder (DXVK)
    int tmuMB = 4;                     // texture memory a TMU reports (and games can use), MB
    int fbMB = 4;                      // frame buffer memory reported, MB
    int tmus = 2;                      // texture units reported (Glide 2.1: 1)
    bool shaderCache = true;           // compiled shaders kept in gl2d9.shaders (no compiling next time)
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
extern int g_logLevel;  // (util.cpp) read at every log and trace site: inline
inline int LogLevel() { return g_logLevel; }
void Log(int level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
void LogFlush(bool processExit);
void CrashLoggerRemove();  // before the DLL is unloaded (FreeLibrary)

#define GL_LOG(...) do { if (::gl2d9::LogLevel() >= 1) ::gl2d9::Log(1, __VA_ARGS__); } while (0)
#define GL_TRACE(...) do { if (::gl2d9::LogLevel() >= 2) ::gl2d9::Log(2, __VA_ARGS__); } while (0)
// Logs the first call of an unimplemented or partially implemented path.
#define GL_STUB(what) do { static bool once_; if (!once_) { once_ = true; GL_LOG("stub: %s", what); } } while (0)

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

}  // namespace gl2d9
