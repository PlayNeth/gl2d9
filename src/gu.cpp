// ---------------------------------------------------------------------------------------------
// This file is derived from 3dfx Interactive's Glide source code (Glide 2's gu.c and digutex.c),
// copyright 3dfx Interactive, Inc. 1999, and is distributed under the 3DFX GLIDE Source Code
// General Public License (LICENSE-3DFX); recipients who wish to copy, distribute or modify it
// can only do so under that license. It is provided without warranty (section 5 of that
// license). Changed by PlayNeth, 1 November 2026: translated to C++ for gl2d9, to run on
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
// See gu.h: fog tables and the combine shorthands. Ported from 3dfx's Glide 2 (gu.c, digutex.c).
#include "gu.h"

#include <cmath>

#include "core.h"

namespace gl2d9 {

namespace {
// A fog table entry from a fraction. Degenerate arguments (a density of 0, near equal to far)
// give 0/0: NaN is taken as 0 (converting it to an integer is undefined), so the table is
// all 0, or a step at near.
GrFog_t FogEntry(float f) {
    if (!(f >= 0.0f)) return 0;
    return GrFog_t(std::min(f, 1.0f) * 255.0f);
}
}  // namespace

float GuFogTableIndexToW(int i) { return float(std::pow(2.0, 3.0 + double(i >> 2)) / (8 - (i & 3))); }

void GuFogGenerateExp(GrFog_t* table, float density) {
    if (!table) return;
    float dp = density * GuFogTableIndexToW(GR_FOG_TABLE_SIZE - 1);
    const float scale = 1.0f / (1.0f - float(std::exp(-dp)));
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        dp = density * GuFogTableIndexToW(i);
        table[i] = FogEntry((1.0f - float(std::exp(-dp))) * scale);
    }
}

void GuFogGenerateExp2(GrFog_t* table, float density) {
    if (!table) return;
    float dp = density * GuFogTableIndexToW(GR_FOG_TABLE_SIZE - 1);
    const float scale = 1.0f / (1.0f - float(std::exp(-(dp * dp))));
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        dp = density * GuFogTableIndexToW(i);
        table[i] = FogEntry((1.0f - float(std::exp(-(dp * dp)))) * scale);
    }
}

void GuFogGenerateLinear(GrFog_t* table, float nearZ, float farZ) {
    if (!table) return;
    for (int i = 0; i < GR_FOG_TABLE_SIZE; i++) {
        const float w = std::min(GuFogTableIndexToW(i), 65535.0f);
        table[i] = FogEntry((w - nearZ) / (farZ - nearZ));
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

}  // namespace gl2d9
