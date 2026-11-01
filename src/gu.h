// ---------------------------------------------------------------------------------------------
// This file is derived from 3dfx Interactive's Glide source code (Glide 2's gu.c, gutex.c and
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
// Glide's utility library (gu*): .3df texture files, fog tables, the combine shorthands and
// Glide 2's texture memory manager. Glide 2 constants (api3.cpp converts).
#pragma once
#include "render.h"

namespace gl2d9 {

FxBool Gu3dfGetInfo(const char* filename, Gu3dfInfo* info);
FxBool Gu3dfLoad(const char* filename, Gu3dfInfo* info);
float GuFogTableIndexToW(int i);
void GuFogGenerateExp(GrFog_t* table, float density);
void GuFogGenerateExp2(GrFog_t* table, float density);
void GuFogGenerateLinear(GrFog_t* table, float nearZ, float farZ);

void GuAlphaSource(GrAlphaSource_t mode);
void GuColorCombineFunction(GrColorCombineFnc_t fnc);
void GuTexCombineFunction(GrChipID_t tmu, GrTextureCombineFnc_t tc);

// The texture manager (gutex.c / digutex.c).
GrMipMapId_t GuTexAllocateMemory(GrChipID_t tmu, FxU8 oddEven, int width, int height, GrTextureFormat_t format,
                                 GrMipMapMode_t mmMode, GrLOD_t smallLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                                 GrTextureClampMode_t sClamp, GrTextureClampMode_t tClamp,
                                 GrTextureFilterMode_t minFilter, GrTextureFilterMode_t magFilter, float lodBias,
                                 FxBool trilinear);
FxBool GuTexChangeAttributes(GrMipMapId_t mmid, int width, int height, GrTextureFormat_t format, GrMipMapMode_t mmMode,
                             GrLOD_t smallLod, GrLOD_t largeLod, GrAspectRatio_t aspect, GrTextureClampMode_t sClamp,
                             GrTextureClampMode_t tClamp, GrTextureFilterMode_t minFilter,
                             GrTextureFilterMode_t magFilter);
void GuTexDownloadMipMap(GrMipMapId_t mmid, const void* src, const GuNccTable* ncc);
void GuTexDownloadMipMapLevel(GrMipMapId_t mmid, GrLOD_t lod, const void** src);
GrMipMapId_t GuTexGetCurrentMipMap(GrChipID_t tmu);
GrMipMapInfo* GuTexGetMipMapInfo(GrMipMapId_t mmid);
FxU32 GuTexMemQueryAvail(GrChipID_t tmu);
void GuTexMemReset();
void GuTexSource(GrMipMapId_t mmid);
FxU16* GuTexCreateColorMipMap();

// Multipass texturing (guMP*): two textures on one TMU, in passes blended together.
void GuMPInit();
void GuMPTexCombineFunction(int fnc);  // GR_MPTEXTURECOMBINE_*
void GuMPTexSource(GrChipID_t virtualTmu, GrMipMapId_t mmid);
void GuMPDrawTriangle(const RVtx& a, const RVtx& b, const RVtx& c);

}  // namespace gl2d9
