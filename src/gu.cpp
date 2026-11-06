// ---------------------------------------------------------------------------------------------
// This file is derived from 3dfx Interactive's Glide source code (Glide 2's g3df.c, gu.c,
// gutex.c, digutex.c, gump.c and ddgump.c), copyright 3dfx Interactive, Inc. 1999, and is
// distributed under the 3DFX GLIDE Source Code General Public License (LICENSE-3DFX); recipients
// who wish to copy, distribute or modify it can only do so under that license. It is provided
// without warranty (section 5 of that license). Changed by PlayNeth, 1 November 2026: translated
// to C++ for gl2d9, to run on Direct3D 9. The rest of gl2d9 is under the MIT license (LICENSE).
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
// See gu.h. Ported from 3dfx's Glide 2 (g3df.c, gu.c, gutex.c, digutex.c).
#include "gu.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "core.h"
#include "texmem.h"

namespace gl2d9 {

namespace {

constexpr size_t kMaxMipMaps = 1024;  // MAX_MIPMAPS_PER_SST

// The first four lines of a .3df file (the header), as _grGet3dfHeader reads them.
bool ReadHeader(FILE* f, char* buf, size_t size) {
    size_t pos = 0;
    int lines = 0;
    while (lines < 4) {
        if (!fgets(buf + pos, int(size - pos), f)) break;
        pos += strlen(buf + pos);
        if (pos == 0 || buf[pos - 1] != '\n') break;
        lines++;
    }
    return lines == 4;
}

// Glide 2 LOD of a size (256 -> 0 ... 1 -> 8; -1 if none).
int LodOf(int size) {
    for (int lod = 0; lod <= 8; lod++)
        if ((256 >> lod) == size) return lod;
    return -1;
}

// Host size of the levels largeLod to smallLod (texels, bytes per texel applied).
FxU32 HostBytes(int smallLod, int largeLod, GrAspectRatio_t aspect, GrTextureFormat_t format) {
    FxU32 n = 0;
    for (int lod = largeLod; lod <= smallLod; lod++) {
        int w, h;
        LevelSize(lod, aspect, &w, &h);
        n += FxU32(w * h) << (format >= GR_TEXFMT_16BIT ? 1 : 0);
    }
    return n;
}

bool ReadShort(FILE* f, FxU16* v) {  // big endian
    const int hi = getc(f);
    if (hi == EOF) return false;
    const int lo = getc(f);
    if (lo == EOF) return false;
    *v = FxU16((hi & 0xFF) << 8 | (lo & 0xFF));
    return true;
}

bool ReadLong(FILE* f, FxU32* v) {
    FxU8 b[4];
    if (fread(b, 1, 4, f) != 4) return false;
    *v = FxU32(b[0]) << 24 | FxU32(b[1]) << 16 | FxU32(b[2]) << 8 | b[3];
    return true;
}

// Parses the header into info (Glide 2 LODs and aspect); false if it isn't one.
bool ParseHeader(FILE* f, Gu3dfInfo* info) {
    char buf[100];
    if (!ReadHeader(f, buf, sizeof(buf))) return false;
    char version[5] = {}, format[10] = {};
    int smallLod, largeLod, aw, ah;
    if (sscanf(buf, "3df v%4s %9s lod range: %i %i aspect ratio: %i %i\n", version, format, &smallLod, &largeLod, &aw,
               &ah) != 6)
        return false;
    static const GrAspectRatio_t kTall[] = {GR_ASPECT_1x1, GR_ASPECT_1x2, GR_ASPECT_1x4, GR_ASPECT_1x8};
    static const GrAspectRatio_t kWide[] = {GR_ASPECT_1x1, GR_ASPECT_2x1, GR_ASPECT_4x1, GR_ASPECT_8x1};
    bool found = false;
    for (int i = 0; i < 4 && !found; i++)
        if ((aw << i) == ah) info->header.aspect_ratio = kTall[i], found = true;
    for (int i = 0; i < 4 && !found; i++)
        if ((ah << i) == aw) info->header.aspect_ratio = kWide[i], found = true;
    if (!found || aw <= 0 || ah <= 0) return false;
    if (aw >= ah) {
        info->header.width = FxU32(largeLod);
        info->header.height = FxU32(largeLod / aw);
    } else {
        info->header.height = FxU32(largeLod);
        info->header.width = FxU32(largeLod / ah);
    }
    const int sl = LodOf(smallLod), ll = LodOf(largeLod);
    if (sl < 0 || ll < 0) return false;
    info->header.small_lod = sl;
    info->header.large_lod = ll;
    for (char* p = format; *p; p++)
        if (*p >= 'a' && *p <= 'z') *p = char(*p - 'a' + 'A');
    // clang-format off: two to a line
    static const struct {
        const char* name;
        GrTextureFormat_t fmt;
    } kFormats[] = {{"I8", GR_TEXFMT_INTENSITY_8},        {"A8", GR_TEXFMT_ALPHA_8},
                    {"AI44", GR_TEXFMT_ALPHA_INTENSITY_44}, {"YIQ", GR_TEXFMT_YIQ_422},
                    {"RGB332", GR_TEXFMT_RGB_332},          {"RGB565", GR_TEXFMT_RGB_565},
                    {"ARGB8332", GR_TEXFMT_ARGB_8332},      {"ARGB1555", GR_TEXFMT_ARGB_1555},
                    {"AYIQ8422", GR_TEXFMT_AYIQ_8422},      {"ARGB4444", GR_TEXFMT_ARGB_4444},
                    {"AI88", GR_TEXFMT_ALPHA_INTENSITY_88}, {"P8", GR_TEXFMT_P_8},
                    {"AP88", GR_TEXFMT_AP_88}};
    // clang-format on
    for (const auto& e : kFormats) {
        if (strcmp(format, e.name) == 0) {
            info->header.format = e.fmt;
            info->mem_required = HostBytes(sl, ll, info->header.aspect_ratio, e.fmt);
            return true;
        }
    }
    return false;
}

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

// Which NCC table of a TMU holds a mipmap's (guTexSource downloads it where not).
GrMipMapId_t g_nccIds[kMaxTmu][2] = {{GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE},
                                     {GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE},
                                     {GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE}};
int g_nextNcc[kMaxTmu];

}  // namespace

FxBool Gu3dfGetInfo(const char* filename, Gu3dfInfo* info) {
    if (!filename || !info) return FXFALSE;
    FILE* f = fopen(filename, "rb");
    if (!f) return FXFALSE;
    const bool ok = ParseHeader(f, info);
    fclose(f);
    return ok ? FXTRUE : FXFALSE;
}

FxBool Gu3dfLoad(const char* filename, Gu3dfInfo* info) {
    if (!filename || !info) return FXFALSE;
    FILE* f = fopen(filename, "rb");
    if (!f) return FXFALSE;
    // (Glide reads the header again and trusts the info gu3dfGetInfo filled.)
    char buf[100];
    bool ok = ReadHeader(f, buf, sizeof(buf));
    const GrTextureFormat_t fmt = info->header.format;
    if (ok && (fmt == GR_TEXFMT_YIQ_422 || fmt == GR_TEXFMT_AYIQ_8422)) {
        GuNccTable& t = info->table.nccTable;
        FxU16 v = 0;
        for (int i = 0; i < 16 && ok; i++) {
            ok = ReadShort(f, &v);
            t.yRGB[i] = FxU8(v & 0xFF);
        }
        for (int i = 0; i < 4 && ok; i++)
            for (int c = 0; c < 3 && ok; c++) {
                ok = ReadShort(f, &v);
                t.iRGB[i][c] = FxI16(v & 0x1FF);
            }
        for (int i = 0; i < 4 && ok; i++)
            for (int c = 0; c < 3 && ok; c++) {
                ok = ReadShort(f, &v);
                t.qRGB[i][c] = FxI16(v & 0x1FF);
            }
        if (ok) {
            for (int i = 0; i < 4; i++) {
                t.packed_data[i] = FxU32(t.yRGB[i * 4]) | FxU32(t.yRGB[i * 4 + 1]) << 8 |
                                   FxU32(t.yRGB[i * 4 + 2]) << 16 | FxU32(t.yRGB[i * 4 + 3]) << 24;
                t.packed_data[i + 4] = (FxU32(t.iRGB[i][0]) << 18) | (FxU32(t.iRGB[i][1]) << 9) | FxU32(t.iRGB[i][2]);
                t.packed_data[i + 8] = (FxU32(t.qRGB[i][0]) << 18) | (FxU32(t.qRGB[i][1]) << 9) | FxU32(t.qRGB[i][2]);
            }
        }
    }
    if (ok && (fmt == GR_TEXFMT_P_8 || fmt == GR_TEXFMT_AP_88)) {
        for (int i = 0; i < 256 && ok; i++) ok = ReadLong(f, &info->table.palette.data[i]);
    }
    if (ok && info->data) {
        const bool wide = fmt >= GR_TEXFMT_16BIT;
        FxU8* dst = static_cast<FxU8*>(info->data);
        for (int lod = info->header.large_lod; lod <= info->header.small_lod && ok; lod++) {
            int w, h;
            LevelSize(lod, info->header.aspect_ratio, &w, &h);
            const size_t n = size_t(w) * h;
            if (!wide) {
                ok = fread(dst, 1, n, f) == n;
                dst += n;
            } else {
                FxU16* d = reinterpret_cast<FxU16*>(dst);
                for (size_t i = 0; i < n && ok; i++) ok = ReadShort(f, &d[i]);
                dst += n * 2;
            }
        }
    } else if (!info->data) {
        ok = false;
    }
    fclose(f);
    return ok ? FXTRUE : FXFALSE;
}

float GuFogTableIndexToW(int i) { return float(std::pow(2.0, 3.0 + double(i >> 2)) / (8 - (i & 3))); }

void GuFogGenerateExp(GrFog_t* table, float density) {
    if (!table) return;
    float dp = density * GuFogTableIndexToW(GR_FOG_TABLE_SIZE - 1);
    const float scale = 1.0f / (1.0f - float(std::exp(-dp)));
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        dp = density * GuFogTableIndexToW(i);
        const float f = std::clamp((1.0f - float(std::exp(-dp))) * scale, 0.0f, 1.0f);
        table[i] = GrFog_t(f * 255.0f);
    }
}

void GuFogGenerateExp2(GrFog_t* table, float density) {
    if (!table) return;
    float dp = density * GuFogTableIndexToW(GR_FOG_TABLE_SIZE - 1);
    const float scale = 1.0f / (1.0f - float(std::exp(-(dp * dp))));
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        dp = density * GuFogTableIndexToW(i);
        const float f = std::clamp((1.0f - float(std::exp(-(dp * dp)))) * scale, 0.0f, 1.0f);
        table[i] = GrFog_t(f * 255.0f);
    }
}

void GuFogGenerateLinear(GrFog_t* table, float nearZ, float farZ) {
    if (!table) return;
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        const float w = std::min(GuFogTableIndexToW(i), 65535.0f);
        const float f = std::clamp((w - nearZ) / (farZ - nearZ), 0.0f, 1.0f);
        table[i] = GrFog_t(f * 255.0f);
    }
}

void GuAlphaSource(GrAlphaSource_t mode) {
    switch (mode) {
    case GR_ALPHASOURCE_CC_ALPHA:
        st::AlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO, GR_COMBINE_LOCAL_CONSTANT,
                         GR_COMBINE_OTHER_CONSTANT, FXFALSE);
        break;
    case GR_ALPHASOURCE_ITERATED_ALPHA:
        st::AlphaCombine(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO, GR_COMBINE_LOCAL_ITERATED,
                         GR_COMBINE_OTHER_CONSTANT, FXFALSE);
        break;
    case GR_ALPHASOURCE_TEXTURE_ALPHA:
        st::AlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_CONSTANT,
                         GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA:
        st::AlphaCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                         GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    default: GlideError("grAlphaSource: unknown alpha source mode", false); break;
    }
}

void GuColorCombineFunction(GrColorCombineFnc_t fnc) {
    // ("gross hack to get ITRGB_DELTA0 modes working")
    st::ColorCombineDelta0Mode(FXFALSE);
    using namespace st;
    const int F = GR_COMBINE_FACTOR_ZERO, LN = GR_COMBINE_LOCAL_CONSTANT, ON = GR_COMBINE_OTHER_CONSTANT;
    switch (fnc) {
    case GR_COLORCOMBINE_ZERO: ColorCombine(GR_COMBINE_FUNCTION_ZERO, F, LN, ON, FXFALSE); break;
    case GR_COLORCOMBINE_CCRGB:
        ColorCombine(GR_COMBINE_FUNCTION_LOCAL, F, GR_COMBINE_LOCAL_CONSTANT, ON, FXFALSE);
        break;
    case GR_COLORCOMBINE_ITRGB_DELTA0: ColorCombineDelta0Mode(FXTRUE); [[fallthrough]];
    case GR_COLORCOMBINE_ITRGB:
        ColorCombine(GR_COMBINE_FUNCTION_LOCAL, F, GR_COMBINE_LOCAL_ITERATED, ON, FXFALSE);
        break;
    case GR_COLORCOMBINE_DECAL_TEXTURE:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, LN, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_CCRGB:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_CONSTANT,
                     GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0: ColorCombineDelta0Mode(FXTRUE); [[fallthrough]];
    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, GR_COMBINE_LOCAL_ITERATED,
                     GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_ADD_ALPHA:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, GR_COMBINE_FACTOR_LOCAL,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL_ALPHA, LN, GR_COMBINE_OTHER_TEXTURE,
                     FXFALSE);
        break;
    case GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA_ADD_ITRGB:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_TEXTURE_ADD_ITRGB:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED,
                     GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_TEXTURE_SUB_ITRGB:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE, GR_COMBINE_LOCAL_ITERATED,
                     GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_CCRGB_BLEND_ITRGB_ON_TEXALPHA:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL, GR_COMBINE_FACTOR_TEXTURE_ALPHA,
                     GR_COMBINE_LOCAL_CONSTANT, GR_COMBINE_OTHER_ITERATED, FXFALSE);
        break;
    case GR_COLORCOMBINE_DIFF_SPEC_A:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_LOCAL_ALPHA,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_DIFF_SPEC_B:
        ColorCombine(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA, GR_COMBINE_FACTOR_LOCAL,
                     GR_COMBINE_LOCAL_ITERATED, GR_COMBINE_OTHER_TEXTURE, FXFALSE);
        break;
    case GR_COLORCOMBINE_ONE: ColorCombine(GR_COMBINE_FUNCTION_ZERO, F, LN, ON, FXTRUE); break;
    default: GlideError("grColorCombineFunction: unsupported color combine function", false); break;
    }
}

void GuTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t tc) {
    auto both = [&](GrCombineFunction_t f, GrCombineFactor_t k, FxBool inv) {
        st::TexCombine(tmu, f, k, f, k, inv, inv);
    };
    switch (tc) {
    case GR_TEXTURECOMBINE_ZERO: both(GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO, FXFALSE); break;
    case GR_TEXTURECOMBINE_DECAL: both(GR_COMBINE_FUNCTION_LOCAL, GR_COMBINE_FACTOR_ZERO, FXFALSE); break;
    case GR_TEXTURECOMBINE_ONE: both(GR_COMBINE_FUNCTION_ZERO, GR_COMBINE_FACTOR_ZERO, FXTRUE); break;
    case GR_TEXTURECOMBINE_ADD: both(GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL, GR_COMBINE_FACTOR_ONE, FXFALSE); break;
    case GR_TEXTURECOMBINE_MULTIPLY: both(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_LOCAL, FXFALSE); break;
    case GR_TEXTURECOMBINE_DETAIL:
        both(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL, GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_DETAIL_OTHER:
        both(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL, GR_COMBINE_FACTOR_DETAIL_FACTOR, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_TRILINEAR_ODD:
        both(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL, GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_TRILINEAR_EVEN:
        both(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL, GR_COMBINE_FACTOR_LOD_FRACTION, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_SUBTRACT:
        both(GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL, GR_COMBINE_FACTOR_ONE, FXFALSE);
        break;
    case GR_TEXTURECOMBINE_OTHER: both(GR_COMBINE_FUNCTION_SCALE_OTHER, GR_COMBINE_FACTOR_ONE, FXFALSE); break;
    default: GlideError("guTexCombineFunction:  Unsupported function", true); break;
    }
}

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
    Rebuild(m);
    if (trilinear) {
        if (oddEven & GR_MIPMAPLEVELMASK_ODD) m.tLOD |= SST_LOD_ODD;
        if (oddEven != GR_MIPMAPLEVELMASK_BOTH) m.tLOD |= SST_LOD_TSPLIT;
    }
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
    if (m.format == GR_TEXFMT_YIQ_422 && ncc) m.ncc_table = *ncc;
    const void* p = src;
    for (int lod = m.lod_max; lod <= m.lod_min; lod++) GuTexDownloadMipMapLevel(mmid, lod, &p);
}

void GuTexDownloadMipMapLevel(GrMipMapId_t mmid, GrLOD_t lod, const void** src) {
    if (!ValidId(mmid) || !src || !*src) return;
    const GrMipMapInfo& m = g_board.mipmaps[mmid];
    const FxU32 i = TexCalcBaseAddress(0, m.lod_max, m.aspect_ratio, m.format, m.odd_even_mask);
    int w, h;
    LevelSize(lod, m.aspect_ratio, &w, &h);
    CoreTexDownloadLevel(m.tmu, m.tmu_base_address - i, lod, m.lod_max, m.aspect_ratio, m.format, m.odd_even_mask, *src,
                         0, h - 1);
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
        g_nccIds[t][0] = g_nccIds[t][1] = GR_NULL_MIPMAP_HANDLE;
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
        if (g_nccIds[tmu][0] == mmid) {
            table = 0;
        } else if (g_nccIds[tmu][1] == mmid) {
            table = 1;
        } else {
            table = g_nextNcc[tmu];
            CoreTexDownloadTable(tmu, GrTexTable_t(table), &m.ncc_table, 0, 11);
            // (The table reaches every TMU: none holds another mipmap's in it any more.)
            for (int t = 0; t < kMaxTmu; t++) g_nccIds[t][table] = mmid;
            g_nextNcc[tmu] = table ? 0 : 1;
        }
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

// ---------------------------------------------------------------- multipass (gump.c, ddgump.c)
// Two "virtual TMUs" on one: each triangle drawn twice (three times with fog when
// multiplying) with TMU0, the passes combined by alpha blending, the later ones at
// depth-equal without depth writes.

namespace gl2d9 {

namespace {
enum {
    kMpAdd,
    kMpMultiply,
    kMpDetail0,
    kMpDetail1,
    kMpTrilinear0,
    kMpTrilinear1,
    kMpSubtract,
};
struct MpState {
    GrMipMapId_t mmid[2] = {GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE};
    int fnc = kMpAdd;
};
MpState g_mp;

constexpr FxU32 kTcReplace = SST_TC_ZERO_OTHER | SST_TC_ADD_CLOCAL | SST_TCA_ZERO_OTHER | SST_TCA_ADD_CLOCAL;
constexpr FxU32 kTcBlend = SST_TC_SUB_CLOCAL | SST_TC_ADD_CLOCAL | SST_TCA_SUB_CLOCAL | SST_TCA_ADD_CLOCAL;
constexpr FxU32 kTcZeroOther = SST_TC_ZERO_OTHER | SST_TCA_ZERO_OTHER;
constexpr FxU32 kTcReverse = SST_TC_REVERSE_BLEND | SST_TCA_REVERSE_BLEND;
constexpr FxU32 Mselect(FxU32 m) { return m << SST_TC_MSELECT_SHIFT | m << SST_TCA_MSELECT_SHIFT; }
constexpr FxU32 kLod = 4, kLodFrac = 5;  // the detail factor, the LOD fraction

// _gumpTexCombineFunction: TMU0's combine for the pass of a virtual TMU.
bool MpTexCombine(int vtmu) {
    FxU32 m = St().r.tmu[0].textureMode & ~(SST_TCOMBINE | SST_TACOMBINE);
    switch (g_mp.fnc) {
    case kMpAdd:
    case kMpMultiply: m |= kTcReplace; break;
    case kMpDetail0: m |= kTcBlend | Mselect(kLod) | kTcZeroOther | (vtmu ? kTcReverse : 0); break;
    case kMpDetail1: m |= kTcBlend | Mselect(kLod) | kTcZeroOther | (vtmu ? 0 : kTcReverse); break;
    case kMpTrilinear0: m |= kTcBlend | Mselect(kLodFrac) | kTcZeroOther | (vtmu ? kTcReverse : 0); break;
    case kMpTrilinear1: m |= kTcBlend | Mselect(kLodFrac) | kTcZeroOther | (vtmu ? 0 : kTcReverse); break;
    default: GlideError("_gumpTexCombineFunction:  Unsupported function", false); return false;
    }
    St().r.tmu[0].textureMode = m;
    return true;
}

FxU32 Blend(FxU32 rgbSrc, FxU32 rgbDst, FxU32 aSrc, FxU32 aDst) {
    return SST_ENALPHABLEND | rgbSrc << SST_RGBSRCFACT_SHIFT | rgbDst << SST_RGBDSTFACT_SHIFT |
           aSrc << SST_ASRCFACT_SHIFT | aDst << SST_ADSTFACT_SHIFT;
}
constexpr FxU32 kAZero = 0, kAColor = 2, kAOne = 4;
}  // namespace

void GuMPInit() { g_mp = MpState{}; }
void GuMPTexCombineFunction(int fnc) { g_mp.fnc = fnc; }
void GuMPTexSource(GrChipID_t vtmu, GrMipMapId_t mmid) {
    if (vtmu == 0 || vtmu == 1) g_mp.mmid[vtmu] = mmid;
}

void GuMPDrawTriangle(const RVtx& a, const RVtx& b, const RVtx& c) {
    Regs& r = St().r;
    if ((r.alphaMode & (SST_ENALPHABLEND | SST_ENALPHAFUNC)) || (r.fbzColorPath & SST_LOCALSELECT_OVERRIDE_WITH_ATEX) ||
        (r.fbzMode & SST_ENCHROMAKEY))
        GlideError("guMPDrawTriangle:  Illegal state", false);
    if (g_mp.fnc == kMpSubtract) {
        GlideError("gumpDrawTriangle:  GR_MPTEXCOMBINE_SUBTRACT not implemented", false);
        return;
    }
    const FxU32 alphaOrig = r.alphaMode, pathOrig = r.fbzColorPath, fbzOrig = r.fbzMode, fogOrig = r.fogMode;
    const bool depth = (fbzOrig & SST_ENDEPTHBUFFER) != 0, fog = (fogOrig & SST_ENFOGGING) != 0;
    const FxU32 bias = SST_CC_ADD_CLOCAL | SST_CC_ADD_ALOCAL | SST_CC_SUB_CLOCAL;
    const FxU32 blendBits = SST_ENALPHABLEND | SST_RGBSRCFACT | SST_RGBDSTFACT | SST_ASRCFACT | SST_ADSTFACT;
    // The later passes: on the first one's pixels only (depth equal, depth unwritten). (3dfx's
    // ORs GR_CMP_EQUAL in unshifted: the chroma key bit, the depth test left at never.)
    auto laterPass = [&] {
        if (depth) r.fbzMode = (fbzOrig & ~(SST_ZAWRMASK | SST_ZFUNC)) | (FxU32(GR_CMP_EQUAL) << SST_ZFUNC_SHIFT);
    };
    GuTexSource(g_mp.mmid[0]);
    if (!MpTexCombine(0)) return;
    if (g_mp.fnc != kMpMultiply) {
        // Sum: the first texture's pass with every effect, the second's added without the
        // color combine's bias or the fog color.
        CoreTriangle(a, b, c);
        GuTexSource(g_mp.mmid[1]);
        MpTexCombine(1);
        r.fbzColorPath = pathOrig & ~bias;
        if (fog) r.fogMode = fogOrig | SST_FOGADD;
        r.alphaMode = (alphaOrig & ~blendBits) | Blend(kAOne, kAOne, kAOne, kAOne);
        laterPass();
        CoreTriangle(a, b, c);
    } else {
        // Product: the first texture's pass without the bias or fog color, the second
        // texture multiplied in (decal), and the bias and fog color added in a third pass.
        r.fbzColorPath = pathOrig & ~bias;
        if (fog) r.fogMode = fogOrig | SST_FOGADD;
        CoreTriangle(a, b, c);
        GuTexSource(g_mp.mmid[1]);
        MpTexCombine(1);
        r.fbzColorPath =
            (pathOrig & ~(SST_RGBSELECT | SST_LOCALSELECT | SST_CC_ZERO_OTHER | SST_CC_SUB_CLOCAL | SST_CC_MSELECT |
                          SST_CC_REVERSE_BLEND | SST_CC_ADD_CLOCAL | SST_CC_ADD_ALOCAL | SST_CC_INVERT_OUTPUT)) |
            (1u << SST_RGBSELECT_SHIFT);
        if (fog) r.fogMode = 0;
        r.alphaMode = (alphaOrig & ~blendBits) | Blend(kAColor, kAZero, kAOne, kAZero);
        laterPass();
        CoreTriangle(a, b, c);
        if (fog) {
            r.fogMode = fogOrig | SST_FOGMULT;
            r.alphaMode = (alphaOrig & ~blendBits) | Blend(kAOne, kAOne, kAOne, kAOne);
            CoreTriangle(a, b, c);
        }
    }
    r.fbzColorPath = pathOrig;
    r.fogMode = fogOrig;
    r.alphaMode = alphaOrig;
    r.fbzMode = fbzOrig;
}

}  // namespace gl2d9
