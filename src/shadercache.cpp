// The pixel shaders compiled and kept: made from shader.cpp's HLSL by d3dcompiler, by key in
// memory and by source hash on disk (gl2d9.shaders), and all made at once when the device is.
#include "shaderint.h"

#include <d3dcompiler.h>

#include <string>
#include <unordered_map>
#include <vector>

namespace gl2d9 {

namespace {

using CompileFn = HRESULT(WINAPI*)(const void*, SIZE_T, const char*, const D3D_SHADER_MACRO*, ID3DInclude*, const char*,
                                   const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);

CompileFn Compiler() {
    static const CompileFn fn = [] {
        static const wchar_t* kNames[] = {L"d3dcompiler_47.dll", L"d3dcompiler_46.dll", L"d3dcompiler_43.dll",
                                          L"d3dcompiler_42.dll"};
        for (const wchar_t* n : kNames) {
            if (HMODULE m = LoadLibraryW(n)) {
                if (auto f = reinterpret_cast<CompileFn>(reinterpret_cast<void*>(GetProcAddress(m, "D3DCompile")))) {
                    GL_LOG("shaders: compiled with %ls", n);
                    return f;
                }
            }
        }
        GL_LOG("shaders: no d3dcompiler found: nothing can be drawn");
        return CompileFn(nullptr);
    }();
    return fn;
}

struct KeyHash {
    size_t operator()(const ShaderKey& k) const {
        size_t h = k.colorPath;
        h = h * 1000003u ^ k.fogMode;
        h = h * 1000003u ^ (k.tmuMode[0] + 7 * k.tmuMode[1]);
        h = h * 1000003u ^ (k.tmus | k.chromaKey << 4 | k.delta0 << 5 | k.stipple << 6 | k.depthConst << 8 |
                            k.depthOut << 9 | k.dstAlpha << 13);
        h = h * 1000003u ^ k.blend;
        return h;
    }
};

std::unordered_map<ShaderKey, IDirect3DPixelShader9*, KeyHash> g_cache;

// The disk cache: compiled shaders by a hash of their source (so a gl2d9 that generates other
// source never takes the old ones), in records of a header (the source's hash, the bytecode's
// size, the key's size, a hash of the key and the bytecode), the key and the bytecode. The key (kKeyBytes: the
// ShaderKey's fields; 0 for the fixed shaders) lets PrewarmShaders make them all when the device
// is made. "SC1" files had no keys, "SC2" ones no bytecode hash: started over.
constexpr char kDiskMagic[8] = {'G', 'L', '2', 'D', '9', 'S', 'C', '3'};
constexpr uint32_t kKeyBytes = 27;
constexpr size_t kRecordHeader = 24;
struct DiskCache {
    bool loaded = false;
    std::wstring path;
    std::unordered_map<uint64_t, std::vector<DWORD>> code;
    std::vector<std::pair<uint64_t, ShaderKey>> keys;  // the keyed records' source hashes and keys, in file order
};
DiskCache g_disk;

// A ShaderKey as kKeyBytes bytes, field by field (no padding), and back.
void PutKey(const ShaderKey& k, char* p) {
    const FxU32 words[5] = {k.colorPath, k.fogMode, k.tmuMode[0], k.tmuMode[1], k.blend};
    memcpy(p, words, sizeof(words));
    const FxU8 bytes[7] = {k.tmus, k.chromaKey, k.delta0, k.stipple, k.depthConst, k.depthOut, k.dstAlpha};
    memcpy(p + sizeof(words), bytes, sizeof(bytes));
}
ShaderKey GetKey(const char* p) {
    FxU32 words[5];
    FxU8 bytes[7];
    memcpy(words, p, sizeof(words));
    memcpy(bytes, p + sizeof(words), sizeof(bytes));
    ShaderKey k{};
    k.colorPath = words[0];
    k.fogMode = words[1];
    k.tmuMode[0] = words[2];
    k.tmuMode[1] = words[3];
    k.blend = words[4];
    k.tmus = bytes[0];
    k.chromaKey = bytes[1];
    k.delta0 = bytes[2];
    k.stipple = bytes[3];
    k.depthConst = bytes[4];
    k.depthOut = bytes[5];
    k.dstAlpha = bytes[6];
    return k;
}
static_assert(sizeof(FxU32) * 5 + 7 == kKeyBytes, "kKeyBytes: the key's fields");

// Whether a key read from the file is one MakeShaderKey can make. Generate indexes tmuMode[]
// with tmus, so a damaged one must never reach it.
bool ValidKey(const ShaderKey& k) {
    const bool texturing = (k.colorPath & SST_ENTEXTUREMAP) != 0;
    if ((k.colorPath & ~kColorPathKeyBits) || (k.fogMode & ~kFogModeKeyBits) || (k.blend & ~kBlendKeyBits))
        return false;
    if (k.tmus > 2 || (k.tmus != 0) != texturing) return false;
    for (int t = 0; t < 2; t++)
        if ((k.tmuMode[t] & ~kTmuModeKeyBits) || (t >= k.tmus && k.tmuMode[t])) return false;
    if (k.chromaKey > 1 || k.delta0 > 1 || k.stipple > 2 || k.depthConst > 1) return false;
    if (k.dstAlpha > 1 || (k.dstAlpha && !(k.blend & SST_RGBDSTFACT))) return false;  // (blending here only)
    if (k.depthOut & ~(DEPTH_Z | DEPTH_W | DEPTH_BIAS | DEPTH_OVERLAY)) return false;
    if (k.depthOut && (k.depthConst || !(k.depthOut & DEPTH_Z) == !(k.depthOut & DEPTH_W))) return false;
    return true;
}

uint64_t Fnv64(const void* data, size_t n) {
    uint64_t h = 14695981039346656037ull;
    for (size_t i = 0; i < n; i++) h = (h ^ static_cast<const unsigned char*>(data)[i]) * 1099511628211ull;
    return h;
}
uint64_t SourceHash(const std::string& s) { return Fnv64(s.data(), s.size()); }

// Bytecode as the compiler makes it: a ps_3_0 version token first, the end token last.
bool PlausibleBytecode(const DWORD* code, size_t words) {
    return words >= 2 && code[0] == 0xFFFF0300u && code[words - 1] == 0x0000FFFFu;
}

void LoadDiskCache() {
    if (g_disk.loaded) return;
    g_disk.loaded = true;
    g_disk.path = ShaderCachePath();
    if (g_disk.path.empty()) return;
    HANDLE h = CreateFileW(g_disk.path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size = {};
    std::vector<char> data;
    // (Shaders of older gl2d9s pile up in it: past 16 MB it starts over.)
    if (GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (16 << 20)) {
        data.resize(size_t(size.QuadPart));
        DWORD got = 0;
        if (!ReadFile(h, data.data(), DWORD(data.size()), &got, nullptr)) got = 0;
        data.resize(got);
    }
    CloseHandle(h);
    if (data.size() < sizeof(kDiskMagic) || memcmp(data.data(), kDiskMagic, sizeof(kDiskMagic)) != 0) {
        // Another format (or empty): started over.
        DeleteFileW(g_disk.path.c_str());
        return;
    }
    // Records are read until one is cut short (a crash while writing it) or damaged: its
    // sizes, its key or its bytecode not what SaveToDisk wrote. The sizes are checked against
    // what is left before anything is read, so a damaged size can't read past the data.
    size_t at = sizeof(kDiskMagic);
    while (data.size() - at >= kRecordHeader) {
        uint64_t hash, bodyHash;
        uint32_t bytes, keyBytes;
        memcpy(&hash, &data[at], 8);
        memcpy(&bytes, &data[at + 8], 4);
        memcpy(&keyBytes, &data[at + 12], 4);
        memcpy(&bodyHash, &data[at + 16], 8);
        const size_t left = data.size() - at - kRecordHeader;
        if (keyBytes != 0 && keyBytes != kKeyBytes) break;
        if (!bytes || bytes % 4 || keyBytes > left || bytes > left - keyBytes) break;
        const char* body = &data[at + kRecordHeader];
        if (Fnv64(body, keyBytes + bytes) != bodyHash) break;
        ShaderKey key{};
        if (keyBytes) {
            key = GetKey(body);
            if (!ValidKey(key)) break;
        }
        std::vector<DWORD> code(bytes / 4);
        memcpy(code.data(), body + keyBytes, bytes);
        if (!PlausibleBytecode(code.data(), code.size())) break;
        if (keyBytes) g_disk.keys.emplace_back(hash, key);
        g_disk.code[hash] = std::move(code);  // (a later record of the same source wins)
        at += kRecordHeader + keyBytes + bytes;
    }
    // The rest cut off from the first bad record on: the records SaveToDisk adds from now on
    // can then be read (behind it they were lost, compiled again each run).
    if (at < data.size()) {
        GL_LOG("shaders: the cache is damaged at byte %u of %u: cut off there", unsigned(at), unsigned(data.size()));
        HANDLE w = CreateFileW(g_disk.path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                               FILE_ATTRIBUTE_NORMAL, nullptr);
        if (w != INVALID_HANDLE_VALUE) {
            LARGE_INTEGER to;
            to.QuadPart = LONGLONG(at);
            if (SetFilePointerEx(w, to, nullptr, FILE_BEGIN)) SetEndOfFile(w);
            CloseHandle(w);
        }
    }
    GL_LOG("shaders: %u in the cache (%ls)", unsigned(g_disk.code.size()), g_disk.path.c_str());
}

void SaveToDisk(uint64_t hash, const void* code, uint32_t bytes, const ShaderKey* key) {
    if (g_disk.path.empty()) return;
    HANDLE h = CreateFileW(g_disk.path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    std::vector<char> rec;
    LARGE_INTEGER size = {};
    if (GetFileSizeEx(h, &size) && size.QuadPart == 0)
        rec.insert(rec.end(), kDiskMagic, kDiskMagic + sizeof(kDiskMagic));
    const size_t at = rec.size();
    const uint32_t keyBytes = key ? kKeyBytes : 0;
    rec.resize(at + kRecordHeader + keyBytes + bytes);
    memcpy(&rec[at], &hash, 8);
    memcpy(&rec[at + 8], &bytes, 4);
    memcpy(&rec[at + 12], &keyBytes, 4);
    if (key) PutKey(*key, &rec[at + kRecordHeader]);
    memcpy(&rec[at + kRecordHeader + keyBytes], code, bytes);
    const uint64_t bodyHash = Fnv64(&rec[at + kRecordHeader], keyBytes + bytes);
    memcpy(&rec[at + 16], &bodyHash, 8);
    DWORD put = 0;
    WriteFile(h, rec.data(), DWORD(rec.size()), &put, nullptr);
    CloseHandle(h);
}

}  // namespace

namespace {
// Bytecode for HLSL source: from the disk cache, or compiled (and added to it, with the key it
// was generated for, if any). Errors logged.
bool Bytecode(const std::string& src, UINT flags, std::vector<DWORD>* out, const ShaderKey* key,
              bool* fromDisk = nullptr) {
    LoadDiskCache();
    const uint64_t hash = SourceHash(src);
    auto cached = g_disk.code.find(hash);
    if (fromDisk) *fromDisk = cached != g_disk.code.end();
    if (cached != g_disk.code.end()) {
        *out = cached->second;
        return true;
    }
    CompileFn compile = Compiler();
    if (!compile) return false;
    ID3DBlob* code = nullptr;
    ID3DBlob* errors = nullptr;
    const HRESULT hr =
        compile(src.data(), src.size(), "gl2d9", nullptr, nullptr, "main", "ps_3_0", flags, 0, &code, &errors);
    const bool ok = SUCCEEDED(hr) && code;
    if (ok) {
        const DWORD* words = static_cast<const DWORD*>(code->GetBufferPointer());
        out->assign(words, words + code->GetBufferSize() / 4);
        g_disk.code[hash] = *out;
        if (key) g_disk.keys.emplace_back(hash, *key);  // (made at once too if Glide is opened again)
        SaveToDisk(hash, words, uint32_t(code->GetBufferSize()), key);
    } else {
        GL_LOG("shader compile failed (0x%08lx): %s\n%s", static_cast<unsigned long>(hr),
               errors ? static_cast<const char*>(errors->GetBufferPointer()) : "", src.c_str());
    }
    if (code) code->Release();
    if (errors) errors->Release();
    return ok;
}

IDirect3DPixelShader9* MakeShader(IDirect3DDevice9* dev, const std::string& src, UINT flags,
                                  const ShaderKey* key = nullptr) {
    std::vector<DWORD> code;
    IDirect3DPixelShader9* ps = nullptr;
    bool fromDisk = false;
    if (Bytecode(src, flags, &code, key, &fromDisk) && FAILED(dev->CreatePixelShader(code.data(), &ps))) {
        ps = nullptr;
        // Refused from the disk cache (a record damaged on disk): compiled again, once, and
        // the new one added (else that shader was missing every run from then on).
        if (fromDisk) {
            GL_LOG("shaders: a cached shader refused: compiled again");
            g_disk.code.erase(SourceHash(src));
            if (Bytecode(src, flags, &code, key) && FAILED(dev->CreatePixelShader(code.data(), &ps))) ps = nullptr;
        }
    }
    return ps;
}
}  // namespace

IDirect3DPixelShader9* GetShader(IDirect3DDevice9* dev, const ShaderKey& k) {
    auto it = g_cache.find(k);
    if (it != g_cache.end()) return it->second;
    IDirect3DPixelShader9* ps = MakeShader(dev, Generate(k), D3DCOMPILE_OPTIMIZATION_LEVEL3, &k);
    GL_TRACE("shader %08lx %08lx %08lx %08lx tmus %d: %s", static_cast<unsigned long>(k.colorPath),
             static_cast<unsigned long>(k.fogMode), static_cast<unsigned long>(k.tmuMode[0]),
             static_cast<unsigned long>(k.tmuMode[1]), k.tmus, ps ? "made" : "failed");
    g_cache[k] = ps;  // (a failed one stays null: not compiled again)
    return ps;
}

// Every shader of the disk cache made at once, when the device is made (the game loading): the
// driver (or DXVK) turns each into the GPU's code as it is made, which otherwise happened at
// its first draw, a hitch in the game the first time each was seen in a session. Only those
// this gl2d9 and these settings generate (their source's hash the record's): a cache keeps
// older gl2d9s' and other settings' shaders too.
void PrewarmShaders(IDirect3DDevice9* dev) {
    LoadDiskCache();
    if (g_disk.keys.empty()) return;
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceCounter(&t0);
    unsigned made = 0, stale = 0, failed = 0;
    for (const auto& e : g_disk.keys) {
        if (g_cache.count(e.second)) continue;  // (made already, or a record of the same key again)
        if (SourceHash(Generate(e.second)) != e.first) {
            stale++;
            continue;
        }
        const auto code = g_disk.code.find(e.first);
        IDirect3DPixelShader9* ps = nullptr;
        if (code == g_disk.code.end() || FAILED(dev->CreatePixelShader(code->second.data(), &ps))) {
            failed++;  // (left to GetShader, which compiles it again)
            continue;
        }
        g_cache[e.second] = ps;
        made++;
    }
    QueryPerformanceCounter(&t1);
    QueryPerformanceFrequency(&freq);
    GL_LOG("shaders: %u made from the cache at once, in %.0f ms (%u of other versions or settings left%s)", made,
           double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(freq.QuadPart), stale,
           failed ? ", some refused: made again when drawn" : "");
}

void ReleaseShaders() {
    for (auto& e : g_cache)
        if (e.second) e.second->Release();
    g_cache.clear();
}

// A fixed shader (the presentation passes, the LFB's), through the same cache.
IDirect3DPixelShader9* CompileFixed(IDirect3DDevice9* dev, const char* src) { return MakeShader(dev, src, 0); }

}  // namespace gl2d9
