// Logging and configuration.
#include "common.h"

#include <cstdarg>
#include <cstdio>
#include <string>

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
    if (v == "1" || v == "true" || v == "yes" || v == "on") { *out = 1; return true; }
    if (v == "0" || v == "false" || v == "no" || v == "off") { *out = 0; return true; }
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
        number(1, 32, &g_config.tmuMB);
    } else if (key == "fbmemory") {
        number(1, 16, &g_config.fbMB);
    } else if (key == "tmus") {
        number(1, 2, &g_config.tmus);
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
    HANDLE h = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    Log(1, "settings: display=%s stretch=%s filter=%s vsync=%s scale=%d d3d9=%s tmus=%d tmumemory=%d fbmemory=%d shadercache=%d log=%d",
        kDisplay[g_config.display], kStretch[g_config.stretch], kFilter[g_config.filter], kVSync[g_config.vsync],
        g_config.scale, g_config.systemD3D9 ? "system" : "game", g_config.tmus,
        g_config.tmuMB, g_config.fbMB, int(g_config.shaderCache), g_config.log);
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
                            static_cast<LPCWSTR>(addr), &m) || !m)
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

// "module.dll+0x1234" for a code address, or the bare address outside any module.
static void DescribeAddress(const void* addr, char* out, size_t size) {
    if (HMODULE m = ModuleAt(addr)) {
        snprintf(out, size, "%s+0x%lx", Utf8(BaseName(ModulePath(m))).c_str(),
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
    default: return EXCEPTION_CONTINUE_SEARCH;  // not a crash (or, for stack overflows, no stack left to log with)
    }
    static LONG loggedThrows;
    if (InterlockedIncrement(code == 0xE06D7363 ? &loggedThrows : &logged) > 5) return EXCEPTION_CONTINUE_SEARCH;
    char at[300];
    DescribeAddress(ep->ExceptionRecord->ExceptionAddress, at, sizeof(at));
    char detail[64] = "";
    if (code == EXCEPTION_ACCESS_VIOLATION && ep->ExceptionRecord->NumberParameters >= 2) {
        const ULONG_PTR kind = ep->ExceptionRecord->ExceptionInformation[0];
        snprintf(detail, sizeof(detail), " %s 0x%08lx", kind == 0 ? "reading" : kind == 8 ? "executing" : "writing",
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
    std::string chain;
    int found = 0;
    for (int i = 0; i < 512 && sp + i < top && found < 12; i++) {
        if (!ModuleAt(reinterpret_cast<const void*>(sp[i]))) continue;
        DescribeAddress(reinterpret_cast<const void*>(sp[i]), at, sizeof(at));
        chain += found++ ? ", " : "";
        chain += at;
    }
    Log(1, "  stack (modules found, innermost first): %s", found ? chain.c_str() : "none");
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

}  // namespace gl2d9
