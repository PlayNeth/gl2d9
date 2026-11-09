// ---------------------------------------------------------------------------------------------
// This file is derived from 3dfx Interactive's Glide source code (Glide 2's g3df.c), copyright
// 3dfx Interactive, Inc. 1999, and is distributed under the 3DFX GLIDE Source Code General
// Public License (LICENSE-3DFX); recipients who wish to copy, distribute or modify it can only
// do so under that license. It is provided without warranty (section 5 of that license). Changed
// by PlayNeth, 1 November 2026: translated to C++ for gl2d9, to run on Direct3D 9. The rest of
// gl2d9 is under the MIT license (LICENSE).
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
// See gu.h: .3df texture files. Ported from 3dfx's Glide 2 (g3df.c).
#include "gu.h"

#include <cstdio>

namespace gl2d9 {

namespace {

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
            const size_t n = size_t(w) * h * (wide ? 2 : 1);
            ok = fread(dst, 1, n, f) == n;
            if (wide)  // (big endian)
                for (size_t i = 0; i < n; i += 2) std::swap(dst[i], dst[i + 1]);
            dst += n;
        }
    } else if (!info->data) {
        ok = false;
    }
    fclose(f);
    return ok ? FXTRUE : FXFALSE;
}

}  // namespace gl2d9
