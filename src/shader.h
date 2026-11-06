// The Voodoo's pixel pipeline as generated Direct3D 9 pixel shaders.
#pragma once
#include "voodoo.h"

#include <d3d9.h>

#include <string>

namespace gl2d9 {

// What selects a pixel shader: the combine and fog bits of the registers.
struct ShaderKey {
    FxU32 colorPath;  // fbzColorPath: selects, combine bits, texture enable
    FxU32 fogMode;
    FxU32 tmuMode[2];  // each TMU's combine bits (0: not used)
    FxU8 tmus;         // TMUs taking part (0 without texturing)
    FxU8 chromaKey;
    FxU8 delta0;      // the ITRGB_DELTA0 constant replaces the iterated color
    FxU8 stipple;     // 0: off, 1: pattern, 2: rotated
    FxU32 blend;      // alphaMode (blend factors, alpha test): blended in the shader (0: by Direct3D)
    FxU8 depthConst;  // GR_DEPTHBUFFER_*_COMPARE_TO_BIAS: the depth tested is zaColor's (c11.x)
    FxU8 depthOut;    // the shader writes the depth (DEPTH_* bits below)
    bool operator==(const ShaderKey& o) const;
};
enum : FxU8 { DEPTH_Z = 1, DEPTH_W = 2, DEPTH_BIAS = 4, DEPTH_OVERLAY = 8 };
ShaderKey MakeShaderKey(const GlideState& st, int numTmu);
// A pass laid on a surface already drawn (overlaybias): one that doesn't write depth, or that
// adds or multiplies. Drawn without writing depth, pulled forward.
bool IsOverlay(const Regs& r);
bool ShaderUsesTexture(const ShaderKey& k, int tmu);  // the TMU's texel is read
// Whether a TMU's texture combine (its textureMode) uses its own texel, as Glide's tmuMask has it.
bool TmuActive(FxU32 textureMode);
// Whether the registers' blending needs the shader (Direct3D 9 has no PREFOG_COLOR factor):
// it then reads the target from a copy (register s3) and does the alpha test itself.
bool BlendInShader(const Regs& r);
// HLSL doing it: the alpha test and blending of alphaMode on c, a and prefog against dstExpr's
// pixel (the alpha test's reference in target.y, kPsTarget).
std::string BlendCode(FxU32 alphaMode, const char* dstExpr);
IDirect3DPixelShader9* GetShader(IDirect3DDevice9* dev, const ShaderKey& k);  // cached; null if it can't be made
void ReleaseShaders();
IDirect3DPixelShader9* CompileFixed(IDirect3DDevice9* dev, const char* src);

// Pixel shader constant registers (see shader.cpp's generated source).
enum {
    kPsColor0 = 0,
    kPsColor1,
    kPsFogColor,
    kPsChromaKey,
    kPsTexScale0,
    kPsTexScale1,
    kPsDetail0,
    kPsDetail1,
    kPsDelta0,
    kPsStipple,      // the pattern's four bytes (y & 3 selects one)
    kPsTarget,       // x: 1 / scale, y: the alpha test's reference (0..255), zw: 1 / the target's size
    kPsDepth,        // x: the depth of zaColor (compare to bias), as drawn; y: the depth bias; z: overlaybias
    kPsStippleRows,  // rotate mode's row table (s4): x: screen height - 1, y: 1 if Y flipped, zw: 1 / its size
};

}  // namespace gl2d9
