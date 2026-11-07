// Logging and configuration.
#include "common.h"

#include <dwmapi.h>

#include <algorithm>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gl2d9 {

Config g_config;
CRITICAL_SECTION g_lock;

static FILE* g_logFile = nullptr;
static CRITICAL_SECTION g_logLock;
int g_logLevel = 0;  // effective level (0 when no file could be opened)

// Paths are handled as UTF-16 throughout: the ANSI APIs mangle folder names
// with characters outside the current code page (accents, CJK, ...).
static const size_t kPathMax = 32768;
static std::wstring g_dllPath;                 // where this DLL was loaded from
static std::wstring g_logPath;                 // log file actually opened
static std::vector<std::wstring> g_iniLoaded;  // config files that were read
static std::vector<std::wstring> g_iniErrors;  // unknown keys / bad values
static PVOID g_crashHandler;                   // CrashLogger registration
static LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* ep);

static std::wstring ModulePath(HMODULE m) {
    std::wstring s(kPathMax, L'\0');
    DWORD n = GetModuleFileNameW(m, &s[0], DWORD(s.size()));
    s.resize(n < s.size() ? n : 0);
    return s;
}

static std::wstring DirOf(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring() : path.substr(0, slash + 1);
}

static std::wstring BaseName(const std::wstring& path) {
    size_t slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? path : path.substr(slash + 1);
}

static std::wstring Env(const wchar_t* name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n <= 1) return std::wstring();
    std::wstring v(n, L'\0');
    n = GetEnvironmentVariableW(name, &v[0], n);
    v.resize(n);
    return v;
}

static std::wstring FromUtf8(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
    return w;
}

static std::string Utf8(const std::wstring& w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), &s[0], n, nullptr, nullptr);
    return s;
}

static void DebugMsg(const std::wstring& msg) {
    // Shown by Wine with WINEDEBUG=warn+seh (Wine 9) or +debugstr, and by DebugView on Windows,
    // even when no log file can be written.
    OutputDebugStringW((L"gl2d9: " + msg + L"\n").c_str());
}

// ---------------------------------------------------------------- config parsing

static std::string Lower(std::string s) {
    for (char& c : s) c = char(tolower(static_cast<unsigned char>(c)));
    return s;
}

static std::string Trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t\r\n"), e = s.find_last_not_of(" \t\r\n");
    return b == std::string::npos ? std::string() : s.substr(b, e - b + 1);
}

static bool ParseBool(const std::string& v, int* out) {
    if (v == "1" || v == "true" || v == "yes" || v == "on") {
        *out = 1;
        return true;
    }
    if (v == "0" || v == "false" || v == "no" || v == "off") {
        *out = 0;
        return true;
    }
    return false;
}

static void Apply(const std::string& key, const std::string& rawVal, const std::wstring& where) {
    const std::string v = Lower(rawVal);
    bool ok = true;
    auto number = [&](int lo, int hi, int* field) {
        char* end = nullptr;
        const long x = strtol(v.c_str(), &end, 10);
        if (end == v.c_str() || *end || x < lo || x > hi) ok = false;
        else *field = int(x);
    };
    if (key == "display") {
        if (v == "switch") g_config.display = kDisplaySwitch;
        else if (v == "window" || v == "windowed") g_config.display = kDisplayWindow;
        else if (v == "borderless") g_config.display = kDisplayBorderless;
        else ok = false;
    } else if (key == "stretch") {
        if (v == "aspect" || v == "0") g_config.stretch = 0;
        else if (v == "fill" || v == "1") g_config.stretch = 1;
        else if (v == "integer" || v == "2") g_config.stretch = 2;
        else ok = false;
    } else if (key == "filter") {
        if (v == "point" || v == "nearest" || v == "0") g_config.filter = 0;
        else if (v == "linear" || v == "bilinear" || v == "1") g_config.filter = 1;
        else if (v == "sharp" || v == "bilinear-sharp" || v == "sharp-bilinear" || v == "2") g_config.filter = 2;
        else ok = false;
    } else if (key == "d3d9") {
        if (v == "system") g_config.systemD3D9 = true;
        else if (v == "game" || v == "local") g_config.systemD3D9 = false;
        else ok = false;
    } else if (key == "vsync") {
        int b;
        if (v == "auto" || v == "game") g_config.vsync = kVSyncAuto;
        else if (ParseBool(v, &b)) g_config.vsync = b ? kVSyncOn : kVSyncOff;
        else ok = false;
    } else if (key == "scale" || key == "resolutionscale") {
        number(1, 8, &g_config.scale);
    } else if (key == "tmumemory" || key == "texmemory") {
        if (v == "auto") g_config.tmuMB = 0;
        else number(1, 32, &g_config.tmuMB);
    } else if (key == "fbmemory") {
        number(1, 16, &g_config.fbMB);
    } else if (key == "tmus") {
        number(1, 2, &g_config.tmus);
    } else if (key == "screenshotkey") {
        // Gone in 1.1.0 (recorders and overlays capture the picture now): accepted and
        // ignored, so old ini files load without an error.
    } else if (key == "fpslimit" || key == "fps") {
        char* end = nullptr;
        const long x = v == "off" ? 0 : strtol(v.c_str(), &end, 10);
        if (end && (end == v.c_str() || *end)) ok = false;
        else if (x < 0 || x > 1000) ok = false;
        else g_config.fpsLimit = int(x);
    } else if (key == "cpus") {
        int b;
        char* end = nullptr;
        // (on/true: one processor, the default; off/all/0: all of them)
        const long x = v == "off" || v == "all" ? 0 : ParseBool(v, &b) ? b : strtol(v.c_str(), &end, 10);
        if (end && (end == v.c_str() || *end)) ok = false;
        else if (x < 0 || x > 64) ok = false;
        else g_config.cpus = int(x);
    } else if (key == "texfilter") {
        if (v == "game" || v == "off" || v == "0") g_config.texLinear = false;
        else if (v == "linear" || v == "bilinear" || v == "on" || v == "1") g_config.texLinear = true;
        else ok = false;
    } else if (key == "anisotropy" || key == "af") {
        int b;
        char* end = nullptr;
        const long x = ParseBool(v, &b) && !b ? 0 : strtol(v.c_str(), &end, 10);
        if (end && (end == v.c_str() || *end)) ok = false;
        else if (x < 0 || x == 1 || x > 16) ok = false;  // (0 off, 2 to 16)
        else g_config.anisotropy = int(x);
    } else if (key == "overlaybias") {
        int b;
        char* end = nullptr;
        const long x = ParseBool(v, &b) ? b : strtol(v.c_str(), &end, 10);
        if (end && (end == v.c_str() || *end)) ok = false;
        else if (x < 0 || x > 4096) ok = false;
        else g_config.overlayBias = int(x);
    } else if (key == "voodoolook") {
        int b;
        if (ParseBool(v, &b)) g_config.voodooLook = b != 0;
        else ok = false;
    } else if (key == "filterresolutions") {
        int b;
        if (ParseBool(v, &b)) g_config.filterResolutions = b != 0;
        else ok = false;
    } else if (key == "shadercache") {
        int b;
        if (ParseBool(v, &b)) g_config.shaderCache = b != 0;
        else ok = false;
    } else if (key == "log") {
        int b;
        if (v == "2" || v == "trace") g_config.log = 2;
        else if (ParseBool(v, &b)) g_config.log = b;
        else ok = false;
    } else if (key == "logfile") {
        g_config.logFile = FromUtf8(Trim(rawVal));
    } else {
        ok = false;
    }
    if (!ok) g_iniErrors.push_back(where + L": ignored " + FromUtf8(key + "=" + rawVal));
}

// Reads one ini file. Keys outside any section, in [gl2d9] and in [default]
// apply to every game; a [<game>.exe] section applies to that executable only.
// gameSections: this pass reads the game's own sections only, else only the general ones
// (LoadConfig reads every file for the general ones first, so a game's section overrides
// them wherever they are).
static bool LoadIni(const std::wstring& path, const std::string& exeName, bool gameSections) {
    FILE* f = _wfopen(path.c_str(), L"rb");
    if (!f) return false;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    fclose(f);
    if (data.size() >= 3 && data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);  // UTF-8 BOM

    bool active = !gameSections;  // keys before any section: general
    size_t pos = 0, lineNo = 0;
    while (pos < data.size()) {
        size_t eol = data.find('\n', pos);
        if (eol == std::string::npos) eol = data.size();
        std::string line = Trim(data.substr(pos, eol - pos));
        pos = eol + 1;
        lineNo++;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            std::string name = Lower(Trim(line.substr(1, line.find(']') - 1)));
            active = gameSections ? name == exeName : name == "gl2d9" || name == "default";
            continue;
        }
        if (!active) continue;
        size_t eq = line.find('=');
        if (eq == std::string::npos) {
            g_iniErrors.push_back(BaseName(path) + L":" + std::to_wstring(lineNo) + L": ignored " + FromUtf8(line));
            continue;
        }
        std::string val = Trim(line.substr(eq + 1));
        // "key=value ; comment" (a space or tab before the ;), "key= ; comment" (no value).
        for (size_t i = 0; i < val.size(); i++)
            if (val[i] == ';' && (i == 0 || val[i - 1] == ' ' || val[i - 1] == '\t')) {
                val = Trim(val.substr(0, i));
                break;
            }
        Apply(Lower(Trim(line.substr(0, eq))), val, BaseName(path) + L":" + std::to_wstring(lineNo));
    }
    return true;
}

namespace {
bool CanWrite(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}
}  // namespace

std::wstring ShaderCachePath() {
    if (!g_config.shaderCache) return std::wstring();
    const std::wstring local = DirOf(g_dllPath) + L"gl2d9.shaders";
    if (CanWrite(local)) return local;
    std::wstring dir = Env(L"LOCALAPPDATA");
    if (dir.empty()) return std::wstring();
    dir += L"\\gl2d9";
    CreateDirectoryW(dir.c_str(), nullptr);
    std::wstring name = BaseName(ModulePath(nullptr));
    const size_t dot = name.rfind(L'.');
    if (dot != std::wstring::npos) name.resize(dot);
    const std::wstring other = dir + L"\\" + name + L".shaders";
    return CanWrite(other) ? other : std::wstring();
}

void LoadConfig(HMODULE self) {
    g_dllPath = ModulePath(self);
    const std::wstring exe = ModulePath(nullptr);
    const std::string exeName = Lower(Utf8(BaseName(exe)));

    // Lowest to highest priority.
    std::vector<std::wstring> files;
    std::wstring win(MAX_PATH + 1, L'\0');
    UINT n = GetWindowsDirectoryW(&win[0], UINT(win.size()));
    if (n && n < win.size()) files.push_back(win.substr(0, n) + L"\\gl2d9.ini");
    const std::wstring dllIni = DirOf(g_dllPath) + L"gl2d9.ini", exeIni = DirOf(exe) + L"gl2d9.ini";
    if (_wcsicmp(dllIni.c_str(), exeIni.c_str()) != 0) files.push_back(dllIni);
    files.push_back(exeIni);
    std::wstring envIni = Env(L"GL2D9_INI");
    if (!envIni.empty()) files.push_back(envIni);
    for (const auto& f : files)
        if (LoadIni(f, exeName, false)) g_iniLoaded.push_back(f);
    for (const auto& f : g_iniLoaded) LoadIni(f, exeName, true);

    // GL2D9_LOG=0/1/2 sets the level; any other value is a log file path (and enables logging).
    std::wstring envLog = Env(L"GL2D9_LOG");
    if (!envLog.empty()) {
        if (envLog == L"0" || envLog == L"1" || envLog == L"2") {
            g_config.log = envLog[0] - L'0';
        } else {
            g_config.log = g_config.log ? g_config.log : 1;
            g_config.logFile = envLog;
        }
    }
}

void LogInit() {
    InitializeCriticalSection(&g_logLock);
    std::wstring found = g_iniLoaded.empty() ? L"no gl2d9.ini found" : L"config: " + g_iniLoaded.back();
    if (!g_config.log) {
        DebugMsg(L"loaded from " + g_dllPath + L" (log off; " + found + L")");
        return;
    }
    const std::wstring exeDir = DirOf(ModulePath(nullptr));
    std::vector<std::wstring> candidates;
    if (!g_config.logFile.empty()) {
        std::wstring lf = g_config.logFile;
        bool absolute = lf.size() > 1 && (lf[1] == L':' || lf[0] == L'\\' || lf[0] == L'/');
        candidates.push_back(absolute ? lf : exeDir + lf);
    }
    candidates.push_back(exeDir + L"gl2d9.log");
    std::wstring cwd(kPathMax, L'\0');
    DWORD n = GetCurrentDirectoryW(DWORD(cwd.size()), &cwd[0]);
    if (n && n < cwd.size()) {
        cwd.resize(n);
        if (cwd.back() != L'\\') cwd += L'\\';
        candidates.push_back(cwd + L"gl2d9.log");
    }
    std::wstring tmp(MAX_PATH + 1, L'\0');
    n = GetTempPathW(DWORD(tmp.size()), &tmp[0]);
    if (n && n < tmp.size()) candidates.push_back(tmp.substr(0, n) + L"gl2d9.log");

    for (const auto& c : candidates) {
        g_logFile = _wfopen(c.c_str(), L"w");
        if (g_logFile) {
            g_logPath = c;
            break;
        }
        DebugMsg(L"cannot write log file " + c);
    }
    if (!g_logFile) {
        DebugMsg(L"loaded from " + g_dllPath + L", no writable log location");
        return;
    }
    // Buffered: trace logging writes thousands of lines a second (a syscall per
    // line is slow under Wine). Info lines are flushed immediately, trace lines
    // at most every 100 ms.
    setvbuf(g_logFile, nullptr, _IOFBF, 1 << 16);
    g_logLevel = g_config.log;
    DebugMsg(L"loaded from " + g_dllPath + L", logging to " + g_logPath);

    static const char* kDisplay[] = {"switch", "window", "borderless"};
    static const char* kStretch[] = {"aspect", "fill", "integer"};
    static const char* kVSync[] = {"auto", "on", "off"};
    static const char* kFilter[] = {"point", "linear", "sharp"};
    static const char* kVersion[] = {"glide.dll (Glide 2.1)", "glide2x.dll (Glide 2.x)", "glide3x.dll (Glide 3.x)"};
    Log(1, "gl2d9 " GL2D9_VERSION ", as %s", kVersion[g_version]);
    Log(1, "process: %s", Utf8(ModulePath(nullptr)).c_str());
    Log(1, "module:  %s", Utf8(g_dllPath).c_str());
    if (g_iniLoaded.empty()) Log(1, "config:  none found");
    for (const auto& f : g_iniLoaded) Log(1, "config:  %s", Utf8(f).c_str());
    for (const auto& e : g_iniErrors) Log(1, "config:  %s", Utf8(e).c_str());
    Log(1,
        "settings: display=%s stretch=%s filter=%s vsync=%s scale=%d d3d9=%s tmus=%d tmumemory=%d (0 = auto) "
        "fbmemory=%d "
        "shadercache=%d overlaybias=%d voodoolook=%d fpslimit=%d cpus=%d texfilter=%s anisotropy=%d log=%d",
        kDisplay[g_config.display], kStretch[g_config.stretch], kFilter[g_config.filter], kVSync[g_config.vsync],
        g_config.scale, g_config.systemD3D9 ? "system" : "game", g_config.tmus, g_config.tmuMB, g_config.fbMB,
        int(g_config.shaderCache), g_config.overlayBias, int(g_config.voodooLook), g_config.fpsLimit, g_config.cpus,
        g_config.texLinear ? "linear" : "game", g_config.anisotropy, g_config.log);
    g_crashHandler = AddVectoredExceptionHandler(0, CrashLogger);  // 0: after the handlers registered first
}

void CrashLoggerRemove() {
    if (g_crashHandler) RemoveVectoredExceptionHandler(g_crashHandler);
}

// The module whose image (past its headers) contains `addr`, or null.
static HMODULE ModuleAt(const void* addr) {
    if (reinterpret_cast<ULONG_PTR>(addr) < 0x10000) return nullptr;  // null and small values: no module
    HMODULE m = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCWSTR>(addr), &m) ||
        !m)
        return nullptr;
    const auto* base = reinterpret_cast<const uint8_t*>(m);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const ULONG_PTR off = static_cast<const uint8_t*>(addr) - base;
    if (static_cast<const uint8_t*>(addr) < base || off < nt->OptionalHeader.SizeOfHeaders ||
        off >= nt->OptionalHeader.SizeOfImage)
        return nullptr;
    return m;
}

// "module.dll+0x1234" for a code address, or the bare address outside any module. (No
// allocation: called by CrashLogger, maybe with the memory run out.)
static void DescribeAddress(const void* addr, char* out, size_t size) {
    if (HMODULE m = ModuleAt(addr)) {
        wchar_t path[MAX_PATH] = L"";
        GetModuleFileNameW(m, path, MAX_PATH);
        const wchar_t* name = wcsrchr(path, L'\\');
        name = name ? name + 1 : path;
        char narrow[MAX_PATH * 3] = "";
        WideCharToMultiByte(CP_UTF8, 0, name, -1, narrow, sizeof(narrow), nullptr, nullptr);
        snprintf(out, size, "%s+0x%lx", narrow,
                 static_cast<unsigned long>(static_cast<const char*>(addr) - reinterpret_cast<const char*>(m)));
    } else {
        snprintf(out, size, "%p", addr);
    }
}

// Logs crashes: which module faulted, and the modules found on the stack (a
// rough call chain). Exceptions are seen before any handler runs, so one that
// the program then handles is logged too; the number logged is limited.
static LONG CALLBACK CrashLogger(EXCEPTION_POINTERS* ep) {
    static LONG logged;
    const DWORD code = ep->ExceptionRecord->ExceptionCode;
    const char* what = nullptr;
    switch (code) {
    case EXCEPTION_ACCESS_VIOLATION: what = "access violation"; break;
    case EXCEPTION_IN_PAGE_ERROR: what = "in-page error"; break;
    case EXCEPTION_ILLEGAL_INSTRUCTION: what = "illegal instruction"; break;
    case EXCEPTION_PRIV_INSTRUCTION: what = "privileged instruction"; break;
    case EXCEPTION_INT_DIVIDE_BY_ZERO: what = "integer division by zero"; break;
    case EXCEPTION_ARRAY_BOUNDS_EXCEEDED: what = "array bounds exceeded"; break;
    // Visual C++'s throw: a game's own error handling (Unreal engine games throw one for a
    // "critical error" and then show their error box), usually caught. Counted apart, so
    // a game that throws them routinely can't use up the crashes' log lines.
    case 0xE06D7363: what = "C++ exception"; break;
    case EXCEPTION_STACK_OVERFLOW: {
        // Little stack left: one fixed line, no formatting (a crash that logged nothing at all
        // was NFS High Stakes' endless window-procedure loop). Once.
        static LONG once;
        if (g_logFile && InterlockedExchange(&once, 1) == 0) {
            fputs("stack overflow - if the game crashed now, a call went round in a loop (a window procedure?)\n",
                  g_logFile);
            fflush(g_logFile);
        }
        return EXCEPTION_CONTINUE_SEARCH;
    }
    default: return EXCEPTION_CONTINUE_SEARCH;  // not a crash
    }
    static LONG loggedThrows;
    if (InterlockedIncrement(code == 0xE06D7363 ? &loggedThrows : &logged) > 5) return EXCEPTION_CONTINUE_SEARCH;
    char at[300];
    DescribeAddress(ep->ExceptionRecord->ExceptionAddress, at, sizeof(at));
    char detail[64] = "";
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
        snprintf(detail, sizeof(detail), " %s 0x%08lx",
                 kind == 0   ? "reading"
                 : kind == 8 ? "executing"
                             : "writing",
                 static_cast<unsigned long>(ep->ExceptionRecord->ExceptionInformation[1]));
    }
    Log(1, "exception 0x%08lx (%s%s) at %s - if the game crashed now, this is where", code, what, detail, at);
#if defined(_M_IX86) || defined(__i386__)
    // Return addresses: stack values that point into a loaded module. Only the
    // committed memory region holding the stack pointer is read.
    const auto* sp = reinterpret_cast<const ULONG_PTR*>(ep->ContextRecord->Esp);
    MEMORY_BASIC_INFORMATION mbi;
    if (!VirtualQuery(sp, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT) return EXCEPTION_CONTINUE_SEARCH;
    const auto* top = reinterpret_cast<const ULONG_PTR*>(static_cast<const char*>(mbi.BaseAddress) + mbi.RegionSize);
    // (A fixed buffer: a crash may come with the memory run out, and nothing here may throw.)
    char chain[12 * 304] = "";
    size_t used = 0;
    int found = 0;
    for (int i = 0; i < 512 && sp + i < top && found < 12; i++) {
        if (!ModuleAt(reinterpret_cast<const void*>(sp[i]))) continue;
        DescribeAddress(reinterpret_cast<const void*>(sp[i]), at, sizeof(at));
        const int n = snprintf(chain + used, sizeof(chain) - used, "%s%s", found++ ? ", " : "", at);
        if (n > 0) used = std::min(sizeof(chain) - 1, used + size_t(n));
    }
    Log(1, "  stack (modules found, innermost first): %s", found ? chain : "none");
#endif
    return EXCEPTION_CONTINUE_SEARCH;
}

void Log(int level, const char* fmt, ...) {
    if (!g_logFile) return;
    EnterCriticalSection(&g_logLock);
    fprintf(g_logFile, "[%8lu %04lx] ", GetTickCount(), GetCurrentThreadId());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_logFile, fmt, ap);
    va_end(ap);
    fputc('\n', g_logFile);
    static DWORD last;
    DWORD now = GetTickCount();
    if (level <= 1 || now - last >= 100) {
        fflush(g_logFile);
        last = now;
    }
    LeaveCriticalSection(&g_logLock);
}

// At process exit the other threads are gone, and one may have died holding the
// log lock: only flush if it is free, so exit can't hang.
void LogFlush(bool processExit) {
    if (!g_logFile) return;
    if (processExit) {
        if (!TryEnterCriticalSection(&g_logLock)) return;
    } else {
        EnterCriticalSection(&g_logLock);
    }
    fflush(g_logFile);
    if (!processExit) {  // unloaded while the process goes on: a later load opens the file again
        fclose(g_logFile);
        g_logFile = nullptr;
    }
    LeaveCriticalSection(&g_logLock);
}

// ---------------------------------------------------------------- cpus=, fpslimit= (dd2d9's)

// cpus= processors the process may run on: one logical processor per physical core (two
// hyperthreads of one core would be about one core's worth), the first core last (it takes
// most of the system's interrupts) when there are cores to spare. For games of the
// single-core era whose timing breaks across cores.
void ApplyCpus() {
    if (g_config.cpus <= 0) return;
    DWORD_PTR process = 0, system = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &process, &system)) return;
    std::vector<DWORD_PTR> cores;  // each core's first logical processor the process may use
    DWORD len = 0;
    GetLogicalProcessorInformation(nullptr, &len);
    std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> info(len / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION) + 1);
    len = DWORD(info.size() * sizeof(info[0]));
    if (GetLogicalProcessorInformation(info.data(), &len)) {
        for (DWORD i = 0; i < len / sizeof(info[0]); i++) {
            if (info[i].Relationship != RelationProcessorCore) continue;
            const DWORD_PTR usable = info[i].ProcessorMask & process;
            if (usable) cores.push_back(usable & (~usable + 1));  // its lowest bit
        }
    }
    if (cores.empty())  // no topology: the logical processors themselves
        for (unsigned bit = 0; bit < sizeof(DWORD_PTR) * 8; bit++)
            if (process & (DWORD_PTR(1) << bit)) cores.push_back(DWORD_PTR(1) << bit);
    if (int(cores.size()) > g_config.cpus) std::rotate(cores.begin(), cores.begin() + 1, cores.end());  // core 0 last
    DWORD_PTR mask = 0;
    int count = 0;
    for (DWORD_PTR c : cores) {
        if (count >= g_config.cpus) break;
        mask |= c;
        count++;
    }
    if (!mask || !SetProcessAffinityMask(GetCurrentProcess(), mask)) return;
    Log(1, "cpus: the game runs on %d processor(s) (mask 0x%lx)", count, static_cast<unsigned long>(mask));
}

namespace {
// Sleeps until QueryPerformanceCounter reaches `target`: a high-resolution waitable timer
// (Windows 10 1803 and later) for all but the last half millisecond, which is spun; without
// one, Sleep for all but the last ~1.5 ms. (Spinning the last 2 ms on Sleep(0) cost the game
// its only core with cpus=1.)
void SleepUntilQpc(int64_t target) {
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    // One high-resolution timer per thread, closed with the thread. Without one (Windows
    // before 10 1803): Sleep, with the system's timer at 1 ms (asked once: at the default
    // 15.6 ms a Sleep(15) took 31 ms, and fpslimit=60 gave an erratic 32-64 fps).
    struct ThreadTimer {
        HANDLE h = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /* HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        ~ThreadTimer() {
            if (h) CloseHandle(h);
        }
    };
    thread_local ThreadTimer threadTimer;
    HANDLE timer = threadTimer.h;
    // (winmm.dll loaded only then: linked, it was loaded into every game.)
    static LONG periodSet;
    auto finePeriod = [] {
        if (InterlockedExchange(&periodSet, 1)) return;
        if (HMODULE mm = LoadLibraryW(L"winmm.dll"))
            if (auto begin = reinterpret_cast<UINT(WINAPI*)(UINT)>(
                    reinterpret_cast<void*>(GetProcAddress(mm, "timeBeginPeriod"))))
                begin(1);
    };
    if (!timer) finePeriod();
    for (;;) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        const int64_t left = target - now.QuadPart;
        if (left <= 0) return;
        const double ms = double(left) * 1000.0 / double(freq.QuadPart);
        if (timer && ms > 0.6) {
            LARGE_INTEGER due;
            due.QuadPart = -int64_t((ms - 0.5) * 10000.0);  // relative, in 100 ns
            if (SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE)) {
                WaitForSingleObject(timer, INFINITE);
                continue;
            }
            // (Refused: Sleep from here on, not a busy wait for the rest.)
            timer = nullptr;
            finePeriod();
        }
        if (!timer && ms > 2.0) Sleep(DWORD(ms - 1.5));
        // The last half millisecond: the processor offered to the process's other threads
        // meanwhile (with cpus=1 they share the game's one: its sound, a renderer's thread).
        else if (!SwitchToThread()) YieldProcessor();
    }
}
}  // namespace

// fpslimit=N: frames spaced 1/N s apart. A frame later than a whole period starts the
// count again (no burst of frames to catch up).
void LimitFrame() {
    if (g_config.fpsLimit <= 0) return;
    static int64_t next;
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    const int64_t period = freq.QuadPart / g_config.fpsLimit;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    if (!next || now.QuadPart > next + period) next = now.QuadPart;
    SleepUntilQpc(next);
    next += period;
}

namespace {
// The display's refresh rate, asked again every few seconds (the game may switch modes).
// Called by the thread that swaps.
DWORD DisplayHz() {
    static DWORD hz, hzAt;
    const DWORD tick = GetTickCount();
    if (!hzAt || tick - hzAt > 5000) {
        DEVMODEW dm = {};
        dm.dmSize = sizeof(dm);
        hz = EnumDisplaySettingsW(nullptr, ENUM_CURRENT_SETTINGS, &dm) && dm.dmDisplayFrequency > 1
                 ? dm.dmDisplayFrequency
                 : 60;
        hzAt = tick | 1;
    }
    return hz;
}
}  // namespace

// vsync=auto and grBufferSwap(N), N 2 or more: frames N refreshes apart. The swap waits for
// one refresh (D3DPRESENT_INTERVAL_ONE); the rest is waited here, from the display's rate
// (asked again every few seconds: the game may switch modes). A frame later than that starts
// the count again.
void LimitSwapInterval(int interval) {
    static int64_t next;
    static LARGE_INTEGER freq;
    if (interval < 2) {
        next = 0;
        return;
    }
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    const DWORD hz = DisplayHz();
    const int64_t period = freq.QuadPart * interval / hz;
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    // (Less a tenth of a refresh: the swap's own wait for the refresh lines it up.)
    if (!next || now.QuadPart > next + period) next = now.QuadPart;
    SleepUntilQpc(next - freq.QuadPart / (10 * int64_t(hz)));
    next += period;
}

// ---------------------------------------------------------------- vsync under the compositor
// (dd2d9's, 1.5.0.) A windowed frame is shown at the desktop compositor's next pass whenever
// it is presented, so the pacing that matters is one frame a refresh, which gl2d9 keeps
// itself (PaceSwap): Direct3D 9's own windowed vsync makes Present wait for a vblank, and
// Intel's driver spins the CPU for it (with dd2d9, N.I.C.E. 2 on a UHD 620: Present 7-8 ms a
// frame, the game's own work slowed from 3-5 to 7-10 ms on the shared power budget, a frame
// now and then a refresh late; presented at once and paced, all 600 frames of 10 s on time).
// Here it is the game's own thread that Present held. Not under Wine, whose compositor isn't
// Windows' (Present's vsync as before), nor without one.
namespace {
using DwmTimingFn = HRESULT(WINAPI*)(HWND, DWM_TIMING_INFO*);
using DwmEnabledFn = HRESULT(WINAPI*)(BOOL*);
struct Dwm {
    DwmTimingFn timing = nullptr;
    DwmEnabledFn enabled = nullptr;
};
const Dwm& DwmFunctions() {
    static const Dwm dwm = [] {
        Dwm r;
        if (g_config.vsync == kVSyncOff) return r;
        if (GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "wine_get_version")) return r;
        wchar_t path[MAX_PATH];
        const UINT n = GetSystemDirectoryW(path, MAX_PATH);
        if (!n || n >= MAX_PATH - 12) return r;
        lstrcatW(path, L"\\dwmapi.dll");
        const HMODULE m = LoadLibraryW(path);
        if (!m) return r;
        r.enabled =
            reinterpret_cast<DwmEnabledFn>(reinterpret_cast<void*>(GetProcAddress(m, "DwmIsCompositionEnabled")));
        r.timing =
            reinterpret_cast<DwmTimingFn>(reinterpret_cast<void*>(GetProcAddress(m, "DwmGetCompositionTimingInfo")));
        if (!r.enabled || !r.timing) r = Dwm();
        return r;
    }();
    return dwm;
}

// The compositor's clock: QueryPerformanceCounter's value at the last vblank, and the refresh
// period in its units. False with vsync=off, under Wine, while composition is off, or when
// the compositor won't tell.
bool CompositorClock(int64_t* vblank, int64_t* period) {
    const Dwm& dwm = DwmFunctions();
    if (!dwm.timing) return false;
    DWM_TIMING_INFO t = {};
    t.cbSize = sizeof(t);
    // (nullptr: the whole desktop's timing, as Windows 8.1 and later require; fails while
    // composition is off)
    if (FAILED(dwm.timing(nullptr, &t)) || !t.qpcRefreshPeriod || !t.qpcVBlank) return false;
    *vblank = int64_t(t.qpcVBlank);
    *period = int64_t(t.qpcRefreshPeriod);
    return true;
}
}  // namespace

bool CompositorPaced() {
    if (g_config.vsync == kVSyncOff) return false;
    const Dwm& dwm = DwmFunctions();
    BOOL on = FALSE;
    const bool paced = dwm.enabled && SUCCEEDED(dwm.enabled(&on)) && on;
    static volatile LONG logged = -1;  // what the log said last (-1: nothing yet)
    if (InterlockedExchange(&logged, paced) != LONG(paced))
        GL_LOG(paced ? "vsync: frames paced by gl2d9 to the desktop compositor's refresh"
                     : "vsync: Direct3D 9's (Wine, or no desktop compositor)");
    return paced;
}

// Frames `slots` refresh periods apart, on a schedule as fpslimit's: a frame late by less
// than a period doesn't move the next one (snapping to the vblank after it instead, as tried
// in dd2d9 with DwmFlush, had N.I.C.E. 2 two or three refreshes a frame for good, the game's
// timing catching up), and one later than that starts the schedule again. The schedule leans
// toward the compositor's vblanks, an eighth of the way a slot: the game starts its frame at
// one and its present is shown at the next, whatever the clocks' drift. Without the
// compositor's clock (Windows 7's turned off since the device was made), the display's rate
// gives the period, and nothing to lean to.
void PaceSwap(int slots) {
    static int64_t next;
    static LARGE_INTEGER freq;
    if (!freq.QuadPart) QueryPerformanceFrequency(&freq);
    for (int i = 0; i < slots; i++) {
        LARGE_INTEGER now;
        QueryPerformanceCounter(&now);
        int64_t vblank = 0, period = 0;
        const bool clock = CompositorClock(&vblank, &period);
        if (!clock) period = freq.QuadPart / DisplayHz();
        int64_t due = next;
        if (!due || now.QuadPart > due + period) due = now.QuadPart;  // the first, or a whole period late
        if (clock) {
            int64_t off = ((due - vblank) % period + period) % period;  // due is off past a vblank
            if (off > period / 2) off -= period;                        // or -off before the next
            due -= off / 8;
        }
        SleepUntilQpc(due);
        next = due + period;
    }
}

}  // namespace gl2d9
