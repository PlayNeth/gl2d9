// ---------------------------------------------------------------------------------------------
// This file is derived from 3dfx Interactive's Glide source code (Glide 2's gutex.c and
// digutex.c), copyright 3dfx Interactive, Inc. 1999, and is distributed under the 3DFX GLIDE
// Source Code General Public License (LICENSE-3DFX); recipients who wish to copy, distribute or
// modify it can only do so under that license. It is provided without warranty (section 5 of
// that license). Changed by PlayNeth, 1 November 2026: translated to C++ for gl2d9, to run on
// Direct3D 9. The rest of gl2d9 is under the MIT license (LICENSE).
//
// THIS SOFTWARE IS SUBJECT TO COPYRIGHT PROTECTION AND IS OFFERED ONLY PURSUANT TO THE 3DFX
// GLIDE GENERAL PUBLIC LICENSE. THERE IS NO RIGHT TO USE THE GLIDE TRADEMARK WITHOUT PRIOR
// WRITTEN PERMISSION OF 3DFX INTERACTIVE, INC. A COPY OF THIS LICENSE MAY BE OBTAINED FROM THE
// DISTRIBUTOR OR BY CONTACTING 3DFX INTERACTIVE INC (info@3dfx.com). THIS PROGRAM. IS PROVIDED
// "AS IS" WITHOUT WARRANTY OF ANY KIND, EITHER EXPRESSED OR IMPLIED. SEE THE 3DFX GLIDE GENERAL
// PUBLIC LICENSE FOR A FULL TEXT OF THE NON-WARRANTY PROVISIONS.
//
// USE, DUPLICATION OR DISCLOSURE BY THE GOVERNMENT IS SUBJECT TO RESTRICTIONS AS SET FORTH IN
// SUBDIVISION (C)(1)(II) OF THE RIGHTS IN TECHNICAL DATA AND COMPUTER SOFTWARE CLAUSE AT DFARS
// 252.227-7013, AND/OR IN SIMILAR OR SUCCESSOR CLAUSES IN THE FAR, DOD OR NASA FAR SUPPLEMENT.
// UNPUBLISHED RIGHTS RESERVED UNDER THE COPYRIGHT LAWS OF THE UNITED STATES.
//
// COPYRIGHT 3DFX INTERACTIVE, INC. 1999, ALL RIGHTS RESERVED
// ---------------------------------------------------------------------------------------------
// See gu.h: the texture manager. Ported from 3dfx's Glide 2 (gutex.c, digutex.c).
#include "gu.h"

#include <cstdlib>

#include "texmem.h"

namespace gl2d9 {

namespace {

constexpr size_t kMaxMipMaps = 1024;  // MAX_MIPMAPS_PER_SST

std::vector<GrMipMapInfo>& Table() {
    if (g_board.mipmaps.capacity() < kMaxMipMaps)
        g_board.mipmaps.reserve(kMaxMipMaps);  // (pointers into it stay valid)
    return g_board.mipmaps;
}

bool ValidId(GrMipMapId_t id) { return id != GR_NULL_MIPMAP_HANDLE && id < g_board.mipmaps.size(); }

// _guTexRebuildRegisterShadows.
void Rebuild(GrMipMapInfo& m) {
    FxU32 mode = FxU32(m.format) << SST_TFORMAT_SHIFT | SST_TCLAMPW | SST_TPERSP_ST;
    if (m.minfilter_mode == GR_TEXTUREFILTER_BILINEAR) mode |= SST_TMINFILTER;
    if (m.magfilter_mode == GR_TEXTUREFILTER_BILINEAR) mode |= SST_TMAGFILTER;
    if (m.s_clamp_mode == GR_TEXTURECLAMP_CLAMP) mode |= SST_TCLAMPS;
    if (m.t_clamp_mode == GR_TEXTURECLAMP_CLAMP) mode |= SST_TCLAMPT;
    if (m.mipmap_mode == GR_MIPMAP_NEAREST_DITHER) mode |= SST_TLODDITHER;
    if (m.trilinear) mode |= SST_TRILINEAR;
    FxU32 lod =
        SST_TLOD_MINMAX_INT(FxU32(m.lod_max), FxU32(m.mipmap_mode == GR_MIPMAP_DISABLE ? m.lod_max : m.lod_min));
    lod |= TLodEvenOddBits(m.odd_even_mask);
    lod |= TLodAspectBits(m.aspect_ratio);
    lod |= m.lod_bias << SST_LODBIAS_SHIFT;
    m.tTextureMode = mode;
    m.tLOD = lod;
}

}  // namespace

GrMipMapId_t GuTexAllocateMemory(GrChipID_t tmu, FxU8 oddEven, int width, int height, GrTextureFormat_t format,
                                 GrMipMapMode_t mmMode, GrLOD_t smallLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                                 GrTextureClampMode_t sClamp, GrTextureClampMode_t tClamp,
                                 GrTextureFilterMode_t minFilter, GrTextureFilterMode_t magFilter, float lodBias,
                                 FxBool trilinear) {
    if (tmu < 0 || tmu >= g_board.numTmu) return GR_NULL_MIPMAP_HANDLE;
    const FxU32 need = TexMemRequired(smallLod, largeLod, aspect, format, oddEven);
    FxU32& freeBase = g_board.freeMemBase[tmu];
    // A texture doesn't straddle the 2 MB boundary (Glide's comment: "hardware constraint").
    if (freeBase < 0x200000 && freeBase + need > 0x200000) freeBase = 0x200000;
    if (GuTexMemQueryAvail(tmu) < need) return GR_NULL_MIPMAP_HANDLE;
    std::vector<GrMipMapInfo>& t = Table();
    if (t.size() >= kMaxMipMaps) return GR_NULL_MIPMAP_HANDLE;
    const GrMipMapId_t id = GrMipMapId_t(t.size());
    t.emplace_back();
    GrMipMapInfo& m = t.back();
    memset(&m, 0, sizeof(m));
    m.tmu_base_address = TexCalcBaseAddress(freeBase, largeLod, aspect, format, oddEven);
    freeBase += need;
    m.format = format;
    m.mipmap_mode = mmMode;
    m.magfilter_mode = magFilter;
    m.minfilter_mode = minFilter;
    m.s_clamp_mode = sClamp;
    m.t_clamp_mode = tClamp;
    m.lod_bias = FxU32(int((lodBias + 0.125f) / 0.25f) & 0x3F);
    m.lod_min = smallLod;
    m.lod_max = largeLod;
    m.tmu = tmu;
    m.odd_even_mask = oddEven;
    m.trilinear = trilinear;
    m.aspect_ratio = aspect;
    m.valid = FXTRUE;
    m.width = width;
    m.height = height;
    // (3dfx's sets tLOD's split bits again here for trilinear: Rebuild has set them, as its
    // _gr_evenOdd_xlate_table does.)
    Rebuild(m);
    return id;
}

FxBool GuTexChangeAttributes(GrMipMapId_t mmid, int width, int height, GrTextureFormat_t format, GrMipMapMode_t mmMode,
                             GrLOD_t smallLod, GrLOD_t largeLod, GrAspectRatio_t aspect, GrTextureClampMode_t sClamp,
                             GrTextureClampMode_t tClamp, GrTextureFilterMode_t minFilter,
                             GrTextureFilterMode_t magFilter) {
    if (!ValidId(mmid)) return FXFALSE;
    GrMipMapInfo& m = g_board.mipmaps[mmid];
    if (format != -1) m.format = format;
    if (mmMode != -1) m.mipmap_mode = mmMode;
    if (smallLod != -1) m.lod_min = smallLod;
    if (largeLod != -1) m.lod_max = largeLod;
    if (minFilter != -1) m.minfilter_mode = minFilter;
    if (magFilter != -1) m.magfilter_mode = magFilter;
    if (sClamp != -1) m.s_clamp_mode = sClamp;
    if (tClamp != -1) m.t_clamp_mode = tClamp;
    if (aspect != -1) m.aspect_ratio = aspect;
    if (width != -1) m.width = width;
    if (height != -1) m.height = height;
    Rebuild(m);
    return FXTRUE;
}

void GuTexDownloadMipMap(GrMipMapId_t mmid, const void* src, const GuNccTable* ncc) {
    if (!ValidId(mmid) || !src) return;
    GrMipMapInfo& m = g_board.mipmaps[mmid];
    m.data = const_cast<void*>(src);
    // Both NCC formats keep their table. (3dfx's keeps it for YIQ_422 only, so an AYIQ_8422
    // mipmap was drawn with an all-zero table, black, by guTexSource, which downloads it for
    // both.)
    if ((m.format == GR_TEXFMT_YIQ_422 || m.format == GR_TEXFMT_AYIQ_8422) && ncc) m.ncc_table = *ncc;
    const void* p = src;
    for (int lod = m.lod_max; lod <= m.lod_min; lod++) GuTexDownloadMipMapLevel(mmid, lod, &p);
}

void GuTexDownloadMipMapLevel(GrMipMapId_t mmid, GrLOD_t lod, const void** src) {
    if (!ValidId(mmid) || !src || !*src) return;
    const GrMipMapInfo& m = g_board.mipmaps[mmid];
    // The start address guTexAllocateMemory gave the mipmap, back from its base address (base =
    // start - offset, and the offset is the base of a texture starting at 0, wrapped).
    const FxU32 start =
        m.tmu_base_address - TexCalcBaseAddress(0, m.lod_max, m.aspect_ratio, m.format, m.odd_even_mask);
    int w, h;
    LevelSize(lod, m.aspect_ratio, &w, &h);
    DownloadLevel(m.tmu, start, lod, m.lod_max, m.aspect_ratio, m.format, m.odd_even_mask, *src, 0, h - 1);
    *src = static_cast<const FxU8*>(*src) + (size_t(w) * h << (m.format >= GR_TEXFMT_16BIT ? 1 : 0));
}

GrMipMapId_t GuTexGetCurrentMipMap(GrChipID_t tmu) {
    return tmu >= 0 && tmu < kMaxTmu ? g_board.currentMipMap[tmu] : GR_NULL_MIPMAP_HANDLE;
}

GrMipMapInfo* GuTexGetMipMapInfo(GrMipMapId_t mmid) { return ValidId(mmid) ? &g_board.mipmaps[mmid] : nullptr; }

FxU32 GuTexMemQueryAvail(GrChipID_t tmu) {
    if (tmu < 0 || tmu >= g_board.numTmu) return 0;
    return g_board.freeMemBase[tmu] < g_board.tmuMem ? g_board.tmuMem - g_board.freeMemBase[tmu] : 0;
}

void GuTexMemReset() {
    Table().clear();
    for (int t = 0; t < kMaxTmu; t++) {
        g_board.currentMipMap[t] = GR_NULL_MIPMAP_HANDLE;
        g_board.freeMemBase[t] = 0;
        g_board.nccMipMap[t][0] = g_board.nccMipMap[t][1] = GR_NULL_MIPMAP_HANDLE;
        g_board.nextNcc[t] = 0;
    }
}

void GuTexSource(GrMipMapId_t mmid) {
    if (!ValidId(mmid)) return;
    const GrMipMapInfo& m = g_board.mipmaps[mmid];
    const int tmu = m.tmu;
    if (tmu < 0 || tmu >= g_board.numTmu) return;
    g_board.currentMipMap[tmu] = mmid;
    TmuConfig& c = St().tmu[tmu];
    c.mmMode = m.mipmap_mode;
    c.smallLod = m.lod_min;
    c.largeLod = m.lod_max;
    c.evenOdd = m.odd_even_mask;
    c.nccTable = 0;
    TmuRegs& r = St().r.tmu[tmu];
    FxU32 mode = m.tTextureMode;
    mode |= r.textureMode & ~(SST_TFORMAT | SST_TCLAMPT | SST_TCLAMPS | SST_TNCCSELECT | SST_TLODDITHER | SST_TCLAMPW |
                              SST_TMAGFILTER | SST_TMINFILTER | SST_TRILINEAR);
    if (!St().allowLodDither) mode &= ~SST_TLODDITHER;
    if (m.format == GR_TEXFMT_YIQ_422 || m.format == GR_TEXFMT_AYIQ_8422) {
        int table;
        if (g_board.nccMipMap[tmu][0] == mmid) {
            table = 0;
        } else if (g_board.nccMipMap[tmu][1] == mmid) {
            table = 1;
        } else {
            table = g_board.nextNcc[tmu];
            // (The table reaches every TMU: none holds another mipmap's in it any more.)
            for (int t = 0; t < kMaxTmu; t++) g_board.nccMipMap[t][table] = mmid;
            g_board.nextNcc[tmu] = table ? 0 : 1;
        }
        // Downloaded every time, unlike 3dfx's, which skips it where the table already went to
        // this mipmap: a grTexDownloadTable of the game's own since, or a guTexDownloadMipMap
        // with a new table, left the mipmap drawn with the wrong one. A table that is already
        // there costs a compare (DownloadNcc changes nothing then).
        DownloadNcc(tmu, table, &m.ncc_table);
        if (table) mode |= SST_TNCCSELECT;
        else mode &= ~SST_TNCCSELECT;
    }
    r.textureMode = mode;
    r.tLOD = m.tLOD;
    r.texBaseAddr = m.tmu_base_address >> 3;
}

FxU16* GuTexCreateColorMipMap() {
    const size_t n = 256 * 256 + 128 * 128 + 64 * 64 + 32 * 32 + 16 * 16 + 8 * 8 + 4 * 4 + 2 * 2 + 1;
    FxU16* data = static_cast<FxU16*>(malloc(n * 2));
    if (!data) return nullptr;
    static const FxU16 kColors[] = {0xF800, 0x07E0, 0x001F, 0xFFFF, 0x0000, 0xF800, 0x07E0, 0x001F, 0xFFFF};
    FxU16* p = data;
    for (int lod = 0; lod <= 8; lod++) {
        const int s = 256 >> lod;
        for (int i = 0; i < s * s; i++) *p++ = kColors[lod];
    }
    return data;
}

}  // namespace gl2d9
