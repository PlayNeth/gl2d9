// ---------------------------------------------------------------------------------------------
// This file is derived from 3dfx Interactive's Glide source code (Glide 2's gump.c and
// ddgump.c), copyright 3dfx Interactive, Inc. 1999, and is distributed under the 3DFX GLIDE
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
// See gu.h: multipass texturing (guMP*). Ported from 3dfx's Glide 2 (gump.c, ddgump.c).
// Two "virtual TMUs" on one: each triangle drawn twice (three times with fog when
// multiplying) with TMU0, the passes combined by alpha blending, the later ones at
// depth-equal without depth writes.
#include "gu.h"

#include "core.h"

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
