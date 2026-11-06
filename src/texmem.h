// Texture memory: downloads into the emulated TMUs and decoding what the registers point at.
#pragma once
#include "voodoo.h"

namespace gl2d9 {

// Glide's grTexDownloadMipMapLevelPartial: rows t to maxT of a level, at the addresses Glide
// computes from startAddress (of the texture's largeLod level).
void DownloadLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                   GrTextureFormat_t format, FxU32 evenOdd, const void* data, int t, int maxT);
// The tables go to every TMU (as 3dfx's Glide broadcasts them). Downloads that change
// nothing don't disturb the renderer; others tell it (RenderTexturesChanged).
void DownloadNcc(GrChipID_t tmu, int which, const GuNccTable* table);
void DownloadPalette(GrChipID_t tmu, const FxU32* entries, int start, int end);
FxU32 LevelAddress(FxU32 baseBytes, GrLOD_t lod, GrAspectRatio_t aspect, GrTextureFormat_t format, bool tsplit);

// A texture as a TMU's registers describe it.
struct TexDesc {
    GrChipID_t tmu;
    int format;      // GR_TEXFMT_*
    FxU32 base;      // bytes: where a 256 level would start
    bool multibase;  // levels 256, 128, 64 and the rest each from their own base (grTexMultibase)
    FxU32 bases[4];
    int lodMin, lodMax;        // Glide 2 LODs: the largest and the smallest level used
    GrAspectRatio_t aspect;    // Glide 2 aspect
    bool tsplit, odd;          // levels of one parity only (two-TMU trilinear)
    unsigned tableHash;        // the palette / NCC table it is decoded with (0: none used)
    int nccTable;              // which NCC table
    int width, height;         // its largest level used
    FxU32 spanStart, spanEnd;  // the memory it occupies
};
bool DescribeTexture(GrChipID_t tmu, const TmuRegs& r, TexDesc* d);
// A checksum of the generations of the memory pages in [start, end): changes when any is written.
unsigned SpanGeneration(GrChipID_t tmu, FxU32 start, FxU32 end);
// One level as 0xAARRGGBB, rows outPitch texels apart.
void DecodeLevel(const TexDesc& d, int lod, FxU32* out, int outPitch);
// Where a level of it is in its TMU's memory.
FxU32 DescLevelAddress(const TexDesc& d, int lod);

}  // namespace gl2d9
