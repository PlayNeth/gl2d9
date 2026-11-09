// The shader generator's internals that the shader cache shares (shader.cpp, shadercache.cpp).
#pragma once
#include "shader.h"

#include <string>

namespace gl2d9 {

// The register bits a ShaderKey keeps (MakeShaderKey), and that a key read from the disk
// cache may have (ValidKey).
constexpr FxU32 kColorPathKeyBits = SST_RGBSELECT | SST_ASELECT | SST_LOCALSELECT | SST_ALOCALSELECT |
                                    SST_LOCALSELECT_OVERRIDE_WITH_ATEX | 0x03FFFF00u | SST_ENTEXTUREMAP;
constexpr FxU32 kFogModeKeyBits =
    SST_ENFOGGING | SST_FOGADD | SST_FOGMULT | SST_FOG_ALPHA | SST_FOG_Z | SST_FOG_CONSTANT;
constexpr FxU32 kBlendKeyBits =
    SST_ENALPHAFUNC | SST_ALPHAFUNC | SST_RGBSRCFACT | SST_RGBDSTFACT | SST_ASRCFACT | SST_ADSTFACT;
constexpr FxU32 kTmuModeKeyBits = SST_TCOMBINE | SST_TACOMBINE;

// The HLSL source of the shader a key selects.
std::string Generate(const ShaderKey& k);

}  // namespace gl2d9
