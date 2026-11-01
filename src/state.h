// Glide's state functions (Glide 2 constants), as they program the registers.
#pragma once
#include "voodoo.h"

namespace gl2d9 {
namespace st {

void ColorCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local,
                  GrCombineOther_t other, FxBool invert);
void AlphaCombine(GrCombineFunction_t function, GrCombineFactor_t factor, GrCombineLocal_t local,
                  GrCombineOther_t other, FxBool invert);
void AlphaControlsITRGBLighting(FxBool enable);
void AlphaBlendFunction(GrAlphaBlendFnc_t rgbSf, GrAlphaBlendFnc_t rgbDf, GrAlphaBlendFnc_t aSf, GrAlphaBlendFnc_t aDf);
void AlphaTestFunction(GrCmpFnc_t fnc);
void AlphaTestReferenceValue(GrAlpha_t v);
void ChromakeyMode(GrChromakeyMode_t mode);
void ChromakeyValue(GrColor_t c);
void ClipWindow(FxU32 minx, FxU32 miny, FxU32 maxx, FxU32 maxy);
void ColorMask(FxBool rgb, FxBool alpha);
void ConstantColorValue(GrColor_t c);
void ConstantColorValue4(float a, float r, float g, float b);
void ColorCombineDelta0Mode(FxBool enable);
void CullMode(GrCullMode_t mode);
void DepthBiasLevel(FxI16 level);
void DepthBufferFunction(GrCmpFnc_t fnc);
void DepthBufferMode(GrDepthBufferMode_t mode);
void DepthMask(FxBool enable);
void DisableAllEffects();
void DitherMode(GrDitherMode_t mode);
void FogMode(GrFogMode_t mode);
void FogColorValue(GrColor_t c);
void FogTable(const GrFog_t* table);
void RenderBuffer(GrBuffer_t buffer);
void SstOrigin(GrOriginLocation_t origin);
void Hints(GrHint_t type, FxU32 hints);
void LfbConstantAlpha(GrAlpha_t a);
void LfbConstantDepth(FxU32 d);
void StippleMode(FxI32 mode);  // Glide 3
void StipplePattern(FxU32 pattern);
void TexClampMode(GrChipID_t tmu, GrTextureClampMode_t s, GrTextureClampMode_t t);
void TexCombine(GrChipID_t tmu, GrCombineFunction_t rgbFn, GrCombineFactor_t rgbFactor, GrCombineFunction_t aFn,
                GrCombineFactor_t aFactor, FxBool rgbInvert, FxBool aInvert);
void TexDetailControl(GrChipID_t tmu, int lodBias, FxU8 detailScale, float detailMax);
void TexFilterMode(GrChipID_t tmu, GrTextureFilterMode_t minf, GrTextureFilterMode_t magf);
void TexLodBiasValue(GrChipID_t tmu, float bias);
void TexMipMapMode(GrChipID_t tmu, GrMipMapMode_t mode, FxBool lodBlend);
void TexNCCTable(GrChipID_t tmu, GrNCCTable_t table);
void TexSource(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, const GrTexInfo* info);
void TexMultibase(GrChipID_t tmu, FxBool enable);
void TexMultibaseAddress(GrChipID_t tmu, GrTexBaseRange_t range, FxU32 startAddress, FxU32 evenOdd,
                         const GrTexInfo* info);
// The state grSstWinOpen sets up (after the screen size is known).
void InitState(GrOriginLocation_t origin);

}  // namespace st
}  // namespace gl2d9
