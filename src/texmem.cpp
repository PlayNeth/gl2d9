// Texture memory: Glide's size and address arithmetic (ditex.c), the downloads into the
// emulated TMU memory (gtexdl.c), and decoding a texture the registers point at into
// 32-bit ARGB for the renderer.
#include "texmem.h"

#include "render.h"

#include <optional>

namespace gl2d9 {

TexMemory g_texMem[kMaxTmu];

namespace {

// Texels a level takes in memory, [aspect class][lod]: 8:1, 4:1, 2:1, 1:1 (Glide's
// _grMipMapSize: every level takes 4 texels at least, and a level one texel high takes two
// rows).
const FxU32 kMipMapSize[4][9] = {
    {0x02000, 0x00800, 0x00200, 0x00080, 0x00020, 0x00010, 0x00008, 0x00004, 0x00004},
    {0x04000, 0x01000, 0x00400, 0x00100, 0x00040, 0x00010, 0x00008, 0x00004, 0x00004},
    {0x08000, 0x02000, 0x00800, 0x00200, 0x00080, 0x00020, 0x00008, 0x00004, 0x00004},
    {0x10000, 0x04000, 0x01000, 0x00400, 0x00100, 0x00040, 0x00010, 0x00004, 0x00004},
};

struct Offsets {
    FxU32 all[4][10];
    FxU32 tsplit[4][10];
    Offsets() {  // Glide's _grMipMapInit
        for (int ar = 0; ar < 4; ar++) {
            all[ar][0] = 0;
            for (int lod = 1; lod <= 9; lod++) all[ar][lod] = all[ar][lod - 1] + kMipMapSize[ar][lod - 1];
            tsplit[ar][0] = tsplit[ar][1] = 0;
            for (int lod = 2; lod <= 9; lod++) tsplit[ar][lod] = tsplit[ar][lod - 2] + kMipMapSize[ar][lod - 2];
        }
    }
};
const Offsets& Off() {
    static const Offsets o;
    return o;
}

inline int ClampLod(int lod) { return lod < 0 ? 0 : lod > 8 ? 8 : lod; }

}  // namespace

FxU32 MipMapSize(int aspectClass, int lod) { return kMipMapSize[aspectClass & 3][ClampLod(lod)]; }
FxU32 MipMapOffset(int aspectClass, int lod) { return Off().all[aspectClass & 3][lod < 0 ? 0 : lod > 9 ? 9 : lod]; }
FxU32 MipMapOffsetTsplit(int aspectClass, int lod) {
    return Off().tsplit[aspectClass & 3][lod < 0 ? 0 : lod > 9 ? 9 : lod];
}

// Glide 2 aspects 0 (8x1) to 6 (1x8): a tall one mirrored onto its wide one (Glide's
// "if (aspect > GR_ASPECT_1x1) aspect = GR_ASPECT_1x8 - aspect").
int AspectClass(GrAspectRatio_t aspect) {
    if (aspect < 0) aspect = 0;
    if (aspect > GR_ASPECT_1x8) aspect = GR_ASPECT_1x8;
    return aspect > GR_ASPECT_1x1 ? GR_ASPECT_1x8 - aspect : aspect;
}

FxU32 TexMemRequired(GrLOD_t smallLod, GrLOD_t largeLod, GrAspectRatio_t aspect, GrTextureFormat_t format,
                     FxU32 evenOdd) {
    const int ar = AspectClass(aspect);
    smallLod = ClampLod(smallLod);
    largeLod = ClampLod(largeLod);
    FxU32 mem = 0;
    if (evenOdd == GR_MIPMAPLEVELMASK_BOTH || evenOdd == 0 || evenOdd > 3) {
        if (smallLod >= largeLod) mem = MipMapOffset(ar, smallLod + 1) - MipMapOffset(ar, largeLod);
    } else {
        const int x = evenOdd == GR_MIPMAPLEVELMASK_EVEN;
        for (int lod = largeLod; lod <= smallLod; lod++)
            if ((lod ^ x) & 1) mem += MipMapSize(ar, lod);
    }
    if (format >= GR_TEXFMT_16BIT) mem <<= 1;
    return (mem + 7) & ~7u;
}

FxU32 TexCalcBaseAddress(FxU32 start, GrLOD_t largeLod, GrAspectRatio_t aspect, GrTextureFormat_t format,
                         FxU32 evenOdd) {
    const int ar = AspectClass(aspect);
    largeLod = ClampLod(largeLod);
    FxU32 sum;
    if (evenOdd == GR_MIPMAPLEVELMASK_BOTH) {
        sum = MipMapOffset(ar, largeLod);
    } else {
        if ((evenOdd == GR_MIPMAPLEVELMASK_EVEN && (largeLod & 1)) || (evenOdd == GR_MIPMAPLEVELMASK_ODD && !(largeLod & 1)))
            largeLod += 1;
        sum = MipMapOffsetTsplit(ar, largeLod);
    }
    if (format >= GR_TEXFMT_16BIT) sum <<= 1;
    return start - sum;
}

void LevelSize(GrLOD_t lod, GrAspectRatio_t aspect, int* w, int* h) {
    lod = ClampLod(lod);
    if (aspect < 0) aspect = 0;
    if (aspect > GR_ASPECT_1x8) aspect = GR_ASPECT_1x8;
    const int big = 256 >> lod;
    // Wide aspects divide the height, tall ones the width (Glide's _grMipMapHostWH).
    const int shift = aspect <= GR_ASPECT_1x1 ? GR_ASPECT_1x1 - aspect : aspect - GR_ASPECT_1x1;
    const int small = std::max(1, big >> shift);
    *w = aspect <= GR_ASPECT_1x1 ? big : small;
    *h = aspect <= GR_ASPECT_1x1 ? small : big;
}

// ---------------------------------------------------------------- downloads

// Where a level lives in a TMU's memory: base (bytes, Glide's base address) plus the levels
// before it (of its parity, for a split texture). Texels are kept in rows of the level's
// width, one after the other (the memory a level takes, Glide's _grMipMapSize, is at least
// that).
FxU32 LevelAddress(FxU32 baseBytes, GrLOD_t lod, GrAspectRatio_t aspect, GrTextureFormat_t format, bool tsplit) {
    const int ar = AspectClass(aspect);
    FxU32 off = tsplit ? MipMapOffsetTsplit(ar, lod) : MipMapOffset(ar, lod);
    if (format >= GR_TEXFMT_16BIT) off <<= 1;
    return baseBytes + off;
}

void DownloadLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                   GrTextureFormat_t format, FxU32 evenOdd, const void* data, int t, int maxT) {
    if (tmu < 0 || tmu >= g_board.numTmu || !data) return;
    // A level outside the mask isn't downloaded (Glide: "skip this level entirely").
    if (!(evenOdd & ((thisLod & 1) ? GR_MIPMAPLEVELMASK_ODD : GR_MIPMAPLEVELMASK_EVEN))) return;
    int w, h;
    LevelSize(thisLod, aspect, &w, &h);
    if (t < 0) t = 0;
    if (maxT >= h) maxT = h - 1;
    if (t > maxT) return;
    const int bpp = format >= GR_TEXFMT_16BIT ? 2 : 1;
    const FxU32 base = TexCalcBaseAddress(startAddress, largeLod, aspect, format, evenOdd) & ~7u;
    const FxU32 addr = LevelAddress(base, thisLod, aspect, format, evenOdd != GR_MIPMAPLEVELMASK_BOTH);
    TexMemory& m = g_texMem[tmu];
    const size_t rowBytes = size_t(w) * bpp;
    const size_t start = size_t(addr) + size_t(t) * rowBytes;
    const size_t len = size_t(maxT - t + 1) * rowBytes;
    if (start >= m.bytes.size()) return;
    const size_t n = std::min(len, m.bytes.size() - start);
    // The game's data: the rows t to maxT, one after the other. (The same again, as games
    // often download: nothing changes, nothing is redrawn.)
    if (memcmp(&m.bytes[start], data, n) == 0) return;
    RenderTexturesChanged();
    memcpy(&m.bytes[start], data, n);
    m.Touch(start, n);
}

namespace {
unsigned Hash(const void* data, size_t n) {  // FNV-1a, never 0
    unsigned h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ static_cast<const FxU8*>(data)[i]) * 16777619u;
    return h | 1;
}
}  // namespace

// 3dfx's Glide broadcasts the tables to every TMU (_grTexDownloadNccTable and
// _grTexDownloadPalette: "only one global texture palette no matter how many TMUs"), whichever
// it is given: a game may download a palette for TMU0 and draw with it on TMU1 (Unreal
// Tournament does).
void DownloadNcc(GrChipID_t tmu, int which, const GuNccTable* table) {
    if (tmu < 0 || tmu >= g_board.numTmu || !table || which < 0 || which > 1) return;
    const unsigned hash = Hash(table->packed_data, sizeof(table->packed_data));
    if (hash == g_board.nccHash[0][which] && memcmp(&g_board.ncc[0][which], table, sizeof(*table)) == 0) return;
    RenderTexturesChanged();
    for (int t = 0; t < g_board.numTmu; t++) {
        g_board.ncc[t][which] = *table;
        g_board.nccHash[t][which] = hash;
    }
}

void DownloadPalette(GrChipID_t tmu, const FxU32* entries, int start, int end) {
    if (tmu < 0 || tmu >= g_board.numTmu || !entries) return;
    if (start < 0) start = 0;
    if (end > 255) end = 255;
    bool same = true;
    for (int i = start; i <= end && same; i++) same = g_board.palette[0][i] == (entries[i] & 0xFFFFFF);
    if (same) return;
    RenderTexturesChanged();
    for (int t = 0; t < g_board.numTmu; t++) {
        for (int i = start; i <= end; i++) g_board.palette[t][i] = entries[i] & 0xFFFFFF;
        g_board.paletteHash[t] = Hash(g_board.palette[t], sizeof(g_board.palette[t]));
    }
}

// ---------------------------------------------------------------- decoding

namespace {

inline FxU32 Expand(FxU32 v, int bits) {  // n-bit channel to 8 bits, the top bits repeated
    switch (bits) {
    case 1: return v ? 0xFF : 0;
    case 2: return v * 0x55;
    case 3: return (v << 5) | (v << 2) | (v >> 1);
    case 4: return v * 0x11;
    case 5: return (v << 3) | (v >> 2);
    case 6: return (v << 2) | (v >> 4);
    default: return v;
    }
}

// The NCC table as the hardware has it (packed_data, what Glide downloads): Y (16 bytes), I and
// Q (4 entries each, three signed 9-bit components).
struct Ncc {
    int y[16];
    int i[4][3], q[4][3];
    explicit Ncc(const GuNccTable& t) {
        for (int k = 0; k < 16; k++) y[k] = int((t.packed_data[k >> 2] >> ((k & 3) * 8)) & 0xFF);
        for (int k = 0; k < 4; k++) {
            const FxU32 vi = t.packed_data[4 + k], vq = t.packed_data[8 + k];
            auto s9 = [](FxU32 v) { return int(v & 0x100 ? (v | ~0x1FFu) : v & 0x1FF); };
            i[k][0] = s9(vi >> 18);
            i[k][1] = s9(vi >> 9);
            i[k][2] = s9(vi);
            q[k][0] = s9(vq >> 18);
            q[k][1] = s9(vq >> 9);
            q[k][2] = s9(vq);
        }
    }
    FxU32 Rgb(FxU32 texel) const {
        const int yy = y[(texel >> 4) & 15], ii = (texel >> 2) & 3, qq = texel & 3;
        int c[3];
        for (int k = 0; k < 3; k++) c[k] = std::clamp(yy + i[ii][k] + q[qq][k], 0, 255);
        return FxU32(c[0]) << 16 | FxU32(c[1]) << 8 | FxU32(c[2]);
    }
};

}  // namespace

// One texel of `format` as 0xAARRGGBB.
static inline FxU32 Texel(int format, FxU32 v, const FxU32* pal, const Ncc* ncc) {
    switch (format) {
    case GR_TEXFMT_RGB_332:
        return 0xFF000000u | Expand((v >> 5) & 7, 3) << 16 | Expand((v >> 2) & 7, 3) << 8 | Expand(v & 3, 2);
    case GR_TEXFMT_YIQ_422: return 0xFF000000u | (ncc ? ncc->Rgb(v) : 0);
    case GR_TEXFMT_ALPHA_8: return v * 0x01010101u;  // the alpha in every channel (as MAME's Voodoo)
    case GR_TEXFMT_INTENSITY_8: return 0xFF000000u | v << 16 | v << 8 | v;
    case GR_TEXFMT_ALPHA_INTENSITY_44: {
        const FxU32 a = Expand(v >> 4, 4), i = Expand(v & 15, 4);
        return a << 24 | i << 16 | i << 8 | i;
    }
    case GR_TEXFMT_P_8: return 0xFF000000u | pal[v & 0xFF];
    case GR_TEXFMT_ARGB_8332:
        return (v >> 8) << 24 | Expand((v >> 5) & 7, 3) << 16 | Expand((v >> 2) & 7, 3) << 8 | Expand(v & 3, 2);
    case GR_TEXFMT_AYIQ_8422: return (v >> 8) << 24 | (ncc ? ncc->Rgb(v & 0xFF) : 0);
    case GR_TEXFMT_RGB_565:
        return 0xFF000000u | Expand(v >> 11, 5) << 16 | Expand((v >> 5) & 63, 6) << 8 | Expand(v & 31, 5);
    case GR_TEXFMT_ARGB_1555:
        return (v & 0x8000 ? 0xFF000000u : 0) | Expand((v >> 10) & 31, 5) << 16 | Expand((v >> 5) & 31, 5) << 8 |
               Expand(v & 31, 5);
    case GR_TEXFMT_ARGB_4444:
        return Expand(v >> 12, 4) << 24 | Expand((v >> 8) & 15, 4) << 16 | Expand((v >> 4) & 15, 4) << 8 |
               Expand(v & 15, 4);
    case GR_TEXFMT_ALPHA_INTENSITY_88: {
        const FxU32 i = v & 0xFF;
        return (v >> 8) << 24 | i << 16 | i << 8 | i;
    }
    case GR_TEXFMT_AP_88: return (v >> 8) << 24 | pal[v & 0xFF];
    default: return 0xFF000000u;
    }
}

bool DescribeTexture(GrChipID_t tmu, const TmuRegs& r, TexDesc* d) {
    const FxU32 tl = r.tLOD;
    d->tmu = tmu;
    d->format = int((r.textureMode & SST_TFORMAT) >> SST_TFORMAT_SHIFT);
    d->base = r.texBaseAddr << 3;  // (may have wrapped below 0: the level offsets bring it back)
    // LOD range of the 4.2 fields (whole LODs: Glide writes whole ones), largest first.
    d->lodMin = std::min(8, int(((tl & SST_LODMIN) >> SST_LODMIN_SHIFT) >> 2));
    d->lodMax = std::min(8, int(((tl & SST_LODMAX) >> SST_LODMAX_SHIFT) >> 2));
    if (d->lodMax < d->lodMin) d->lodMax = d->lodMin;
    const int asp = int((tl & SST_LOD_ASPECT) >> SST_LOD_ASPECT_SHIFT);
    // Back to a Glide 2 aspect: S wider = 8x1 (3) .. 2x1 (1); else 1x1 .. 1x8.
    d->aspect = (tl & SST_LOD_S_IS_WIDER) ? GR_ASPECT_1x1 - asp : GR_ASPECT_1x1 + asp;
    d->tsplit = (tl & SST_LOD_TSPLIT) != 0;
    d->odd = (tl & SST_LOD_ODD) != 0;
    d->nccTable = (r.textureMode & SST_TNCCSELECT) ? 1 : 0;
    if (d->format == GR_TEXFMT_P_8 || d->format == GR_TEXFMT_AP_88)
        d->tableHash = g_board.paletteHash[tmu];
    else if (d->format == GR_TEXFMT_YIQ_422 || d->format == GR_TEXFMT_AYIQ_8422)
        d->tableHash = g_board.nccHash[tmu][d->nccTable] ^ FxU32(d->nccTable);
    else
        d->tableHash = 0;
    int w, h;
    LevelSize(d->lodMin, d->aspect, &w, &h);
    d->width = w;
    d->height = h;
    d->multibase = (tl & SST_TMULTIBASEADDR) != 0;
    d->bases[0] = d->base;
    d->bases[1] = r.texBaseAddr1 << 3;
    d->bases[2] = r.texBaseAddr2 << 3;
    d->bases[3] = r.texBaseAddr38 << 3;
    // The memory it spans (for the page generations): from its lowest level to the end of
    // its highest (one after the other, or wherever multibase put them).
    const int bpp = d->format >= GR_TEXFMT_16BIT ? 2 : 1;
    d->spanStart = ~0u;
    d->spanEnd = 0;
    for (int lod = d->lodMin; lod <= d->lodMax; lod++) {
        int lw, lh;
        LevelSize(lod, d->aspect, &lw, &lh);
        const FxU32 a = DescLevelAddress(*d, lod);
        d->spanStart = std::min(d->spanStart, a);
        d->spanEnd = std::max(d->spanEnd, a + FxU32(lw * lh * bpp));
    }
    return d->spanStart < g_texMem[tmu].bytes.size();
}

unsigned SpanGeneration(GrChipID_t tmu, FxU32 start, FxU32 end) {
    const TexMemory& m = g_texMem[tmu];
    unsigned sum = 0;
    if (m.pageGen.empty()) return 0;
    const size_t last = std::min<size_t>(m.pageGen.size() - 1, end ? (end - 1) >> 12 : 0);
    for (size_t p = start >> 12; p <= last; p++) sum = sum * 31 + m.pageGen[p];
    return sum;
}

FxU32 DescLevelAddress(const TexDesc& d, int lod) {
    // Multibase: Glide computes each base as if its range's largest LOD were the texture's.
    const FxU32 base = d.multibase ? d.bases[std::min(lod, 3)] : d.base;
    return LevelAddress(base, lod, d.aspect, d.format, d.tsplit);
}

void DecodeLevel(const TexDesc& d, int lod, FxU32* out, int outPitch) {
    int w, h;
    LevelSize(lod, d.aspect, &w, &h);
    const TexMemory& m = g_texMem[d.tmu];
    const int bpp = d.format >= GR_TEXFMT_16BIT ? 2 : 1;
    const FxU32 addr = DescLevelAddress(d, lod);
    const FxU32* pal = g_board.palette[d.tmu];
    const bool nccFmt = d.format == GR_TEXFMT_YIQ_422 || d.format == GR_TEXFMT_AYIQ_8422;
    std::optional<Ncc> nccTable;
    if (nccFmt) nccTable.emplace(g_board.ncc[d.tmu][d.nccTable]);
    const Ncc* ncc = nccTable ? &*nccTable : nullptr;
    const size_t size = m.bytes.size();
    for (int y = 0; y < h; y++) {
        FxU32* row = out + size_t(y) * outPitch;
        size_t a = size_t(addr) + size_t(y) * w * bpp;
        for (int x = 0; x < w; x++, a += bpp) {
            FxU32 v = 0;
            if (a + bpp <= size) v = bpp == 2 ? FxU32(m.bytes[a] | m.bytes[a + 1] << 8) : m.bytes[a];
            row[x] = Texel(d.format, v, pal, ncc);
        }
    }
}

}  // namespace gl2d9
