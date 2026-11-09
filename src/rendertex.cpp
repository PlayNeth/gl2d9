// The renderer's textures: decoded from the emulated texture memory as the TMUs point at it,
// cached, and held to a budget.
#include "renderint.h"

#include <algorithm>

#include "texmem.h"

namespace gl2d9 {

void ReleaseTextures() {
    for (auto& e : g_r.textures)
        if (e.second.tex) e.second.tex->Release();
    g_r.textures.clear();
    for (auto& s : g_r.spares) s.tex->Release();
    g_r.spares.clear();
    g_r.textureBytes = 0;
}

namespace {

constexpr size_t kMaxSpares = 64;

void DropTexture(std::unordered_map<TexKey, TexEntry, TexKeyHash>::iterator it) {
    TexEntry& e = it->second;
    if (e.tex) {
        if (g_r.spares.size() < kMaxSpares) {
            g_r.spares.push_back({e.tex, e.w, e.h, e.levels, e.bytes});  // (still counted)
            g_r.textures.erase(it);
            return;
        }
        e.tex->Release();
    }
    g_r.textureBytes -= std::min(g_r.textureBytes, e.bytes);
    g_r.textures.erase(it);
}

}  // namespace

void DropSpares() {
    for (const SpareTex& sp : g_r.spares) {
        sp.tex->Release();
        g_r.textureBytes -= std::min(g_r.textureBytes, sp.bytes);
    }
    g_r.spares.clear();
}

void TrimTextures(unsigned maxAge, size_t budget) {
    for (auto it = g_r.textures.begin(); it != g_r.textures.end();) {
        auto next = std::next(it);
        if (g_r.frame - it->second.lastUse > maxAge) DropTexture(it);
        it = next;
    }
    if (g_r.textureBytes <= budget) return;
    // Over budget still: the spares first (nothing uses them), then the least recently used,
    // down to three quarters of it. Once a frame at most: textures all in use this frame
    // can't go, and trying again for each new one sorted them all each time (a stutter
    // growing with the cache: Unreal's light maps, new at new addresses).
    if (g_r.trimFrame == g_r.frame) return;
    g_r.trimFrame = g_r.frame;
    DropSpares();
    if (g_r.textureBytes <= budget / 4 * 3) return;
    GL_STUB("texture budget reached: least recently used textures dropped");
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
                g_r.textureBytes -= std::min(g_r.textureBytes, sp.bytes);  // (counted again below)
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
    // What it was decoded from, kept once it is (a decode cut short by a failed allocation,
    // CallFailed, is done again at the next use rather than taken as current).
    auto decoded = [&] {
        e.source.assign(src, src + srcLen);
        e.spanStart = d.spanStart;
        e.gen = gen;
        return e.tex;
    };
    if (!d.tsplit) {  // decoded straight into the texture
        for (int i = 0; i < levels; i++) {
            D3DLOCKED_RECT lr;
            if (FAILED(e.tex->LockRect(i, &lr, nullptr, levels == 1 ? RewriteLock() : 0))) continue;
            DecodeLevel(d, d.lodMin + i, static_cast<FxU32*>(lr.pBits), lr.Pitch / 4);
            e.tex->UnlockRect(i);
        }
        return decoded();
    }
    // Split (two-TMU trilinear): the TMU's memory holds the levels of one parity only, and the
    // others were never written there. For a level it lacks the Voodoo takes the next smaller
    // one (MAME's lodmask), here that one widened; for the smallest, which has none below it
    // in the range, the one above shrunk. (A lone level of the other parity: the next smaller
    // one decoded, as the Voodoo reads it.)
    std::vector<std::vector<FxU32>> px(levels);
    std::vector<int> lw(levels), lh(levels);
    auto present = [&](int i) { return (((d.lodMin + i) & 1) != 0) == d.odd; };
    for (int i = 0; i < levels; i++) {
        LevelSize(d.lodMin + i, d.aspect, &lw[i], &lh[i]);
        px[i].resize(size_t(lw[i]) * lh[i]);
        if (present(i)) DecodeLevel(d, d.lodMin + i, px[i].data(), lw[i]);
    }
    // Nearest-texel resampling of one level's texels into another's size.
    auto resample = [](const std::vector<FxU32>& src, int sw, int sh, std::vector<FxU32>& dst, int dw, int dh) {
        for (int y = 0; y < dh; y++)
            for (int x = 0; x < dw; x++) {
                const int sx = std::min(sw - 1, x * sw / dw), sy = std::min(sh - 1, y * sh / dh);
                dst[size_t(y) * dw + x] = src[size_t(sy) * sw + sx];
            }
    };
    for (int i = 0; i < levels; i++) {
        if (present(i)) continue;
        if (i + 1 < levels) {
            resample(px[i + 1], lw[i + 1], lh[i + 1], px[i], lw[i], lh[i]);
        } else if (i > 0) {
            resample(px[i - 1], lw[i - 1], lh[i - 1], px[i], lw[i], lh[i]);
        } else if (d.lodMin < GR_LOD_1) {
            int nw, nh;
            LevelSize(d.lodMin + 1, d.aspect, &nw, &nh);
            std::vector<FxU32> next(size_t(nw) * nh);
            DecodeLevel(d, d.lodMin + 1, next.data(), nw);
            resample(next, nw, nh, px[i], lw[i], lh[i]);
        } else {
            DecodeLevel(d, d.lodMin, px[i].data(), lw[i]);
        }
    }
    for (int i = 0; i < levels; i++) {
        D3DLOCKED_RECT lr;
        if (FAILED(e.tex->LockRect(i, &lr, nullptr, levels == 1 ? RewriteLock() : 0))) continue;
        for (int y = 0; y < lh[i]; y++)
            memcpy(static_cast<uint8_t*>(lr.pBits) + size_t(y) * lr.Pitch, &px[i][size_t(y) * lw[i]], lw[i] * 4);
        e.tex->UnlockRect(i);
    }
    return decoded();
}

void RenderTexturesChanged() {
    RenderFlush();
    g_r.batchState = false;
}

void RenderRecover() {
    g_r.batch.clear();
    g_r.stipRows.clear();
    g_r.batchState = false;
    if (!g_r.dev) return;
    DropSpares();
    g_r.trimFrame = ~0u;  // (enforced now, whatever the frame)
    TrimTextures(60, kTextureBudget / 2);
}

}  // namespace gl2d9
