// ---------------------------------------------------------------------------------------------
// This file is derived from 3dfx Interactive's Glide source code (the types and constants of
// glide.h, Glide 2.x and 3.x), copyright 3dfx Interactive, Inc. 1999, and is distributed under
// the 3DFX GLIDE Source Code General Public License (LICENSE-3DFX); recipients who wish to copy,
// distribute or modify it can only do so under that license. It is provided without warranty
// (section 5 of that license). Changed by PlayNeth, 1 November 2026: translated to C++ for
// gl2d9, to run on Direct3D 9. The rest of gl2d9 is under the MIT license (LICENSE).
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
// Glide's types and constants, as the games were compiled with them (3dfx's glide.h of
// Glide 2.x for Voodoo 2, and of Glide 3.x for Voodoo 3): the values, written out here.
// Where Glide 3 changed one, the Glide 3 value carries a G3 in its name.
#pragma once

// clang-format off: glide.h's tables, laid out as there
#include <cstdint>

typedef uint8_t FxU8;
typedef int16_t FxI16;
typedef uint16_t FxU16;
typedef int32_t FxI32;
typedef uint32_t FxU32;
typedef int FxBool;
#define FXTRUE 1
#define FXFALSE 0

typedef FxI32 GrChipID_t;
typedef FxU32 GrColor_t;
typedef FxU8 GrAlpha_t;
typedef FxU32 GrMipMapId_t;
typedef FxU8 GrFog_t;
typedef FxI32 GrScreenRefresh_t;
typedef FxI32 GrScreenResolution_t;
typedef FxI32 GrColorFormat_t;
typedef FxI32 GrOriginLocation_t;
typedef FxI32 GrBuffer_t;
typedef FxI32 GrCombineFunction_t;
typedef FxI32 GrCombineFactor_t;
typedef FxI32 GrCombineLocal_t;
typedef FxI32 GrCombineOther_t;
typedef FxI32 GrAlphaSource_t;
typedef FxI32 GrColorCombineFnc_t;
typedef FxI32 GrAlphaBlendFnc_t;
typedef FxI32 GrAspectRatio_t;
typedef FxI32 GrLOD_t;
typedef FxI32 GrCmpFnc_t;
typedef FxI32 GrCullMode_t;
typedef FxI32 GrDepthBufferMode_t;
typedef FxI32 GrDitherMode_t;
typedef FxI32 GrFogMode_t;
typedef FxU32 GrLock_t;
typedef FxI32 GrLfbBypassMode_t;
typedef FxI32 GrLfbWriteMode_t;
typedef FxI32 GrMipMapMode_t;
typedef FxI32 GrSmoothingMode_t;
typedef FxI32 GrTextureClampMode_t;
typedef FxI32 GrTextureCombineFnc_t;
typedef FxI32 GrTextureFilterMode_t;
typedef FxI32 GrTextureFormat_t;
typedef FxU32 GrTexTable_t;
typedef FxU32 GrNCCTable_t;
typedef FxU32 GrTexBaseRange_t;
typedef FxI32 GrChromakeyMode_t;
typedef FxI32 GrPassthruMode_t;
typedef FxU32 GrHint_t;
typedef FxU32 GrLfbSrcFmt_t;
typedef FxU32 GrControl_t;
typedef FxU32 GrEnableMode_t;
typedef FxU32 GrCoordinateSpaceMode_t;
typedef FxI32 GrSstType;
typedef FxU32 GrContext_t;  // Glide 3
typedef int (__attribute__((stdcall)) *GrProc)();
typedef void (*GrErrorCallbackFnc_t)(const char* string, FxBool fatal);

// Resolutions and refresh rates.
enum {
    GR_RESOLUTION_320x200, GR_RESOLUTION_320x240, GR_RESOLUTION_400x256, GR_RESOLUTION_512x384,
    GR_RESOLUTION_640x200, GR_RESOLUTION_640x350, GR_RESOLUTION_640x400, GR_RESOLUTION_640x480,
    GR_RESOLUTION_800x600, GR_RESOLUTION_960x720, GR_RESOLUTION_856x480, GR_RESOLUTION_512x256,
    GR_RESOLUTION_1024x768, GR_RESOLUTION_1280x1024, GR_RESOLUTION_1600x1200, GR_RESOLUTION_400x300,
    // Glide 3 (h3) adds these.
    GR_RESOLUTION_1152x864, GR_RESOLUTION_1280x960, GR_RESOLUTION_1600x1024, GR_RESOLUTION_1792x1344,
    GR_RESOLUTION_1856x1392, GR_RESOLUTION_1920x1440, GR_RESOLUTION_2048x1536, GR_RESOLUTION_2048x2048,
};
#define GR_RESOLUTION_NONE 0xff
enum { GR_REFRESH_60Hz, GR_REFRESH_70Hz, GR_REFRESH_72Hz, GR_REFRESH_75Hz, GR_REFRESH_80Hz, GR_REFRESH_90Hz,
       GR_REFRESH_100Hz, GR_REFRESH_85Hz, GR_REFRESH_120Hz };
#define GR_REFRESH_NONE 0xff

#define GR_TMU0 0
#define GR_TMU1 1
#define GR_TMU2 2
#define GR_FBI 3

enum { GR_COLORFORMAT_ARGB, GR_COLORFORMAT_ABGR, GR_COLORFORMAT_RGBA, GR_COLORFORMAT_BGRA };
#define GR_ORIGIN_UPPER_LEFT 0
#define GR_ORIGIN_LOWER_LEFT 1
#define GR_ORIGIN_ANY 0xFF

enum { GR_BUFFER_FRONTBUFFER, GR_BUFFER_BACKBUFFER, GR_BUFFER_AUXBUFFER, GR_BUFFER_DEPTHBUFFER,
       GR_BUFFER_ALPHABUFFER, GR_BUFFER_TRIPLEBUFFER };

// Combine units.
enum {
    GR_COMBINE_FUNCTION_ZERO = 0, GR_COMBINE_FUNCTION_LOCAL = 1, GR_COMBINE_FUNCTION_LOCAL_ALPHA = 2,
    GR_COMBINE_FUNCTION_SCALE_OTHER = 3, GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL = 4,
    GR_COMBINE_FUNCTION_SCALE_OTHER_ADD_LOCAL_ALPHA = 5, GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL = 6,
    GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL = 7,
    GR_COMBINE_FUNCTION_SCALE_OTHER_MINUS_LOCAL_ADD_LOCAL_ALPHA = 8,
    GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL = 9,
    GR_COMBINE_FUNCTION_SCALE_MINUS_LOCAL_ADD_LOCAL_ALPHA = 0x10,
};
enum {
    GR_COMBINE_FACTOR_ZERO = 0, GR_COMBINE_FACTOR_LOCAL = 1, GR_COMBINE_FACTOR_OTHER_ALPHA = 2,
    GR_COMBINE_FACTOR_LOCAL_ALPHA = 3, GR_COMBINE_FACTOR_TEXTURE_ALPHA = 4, GR_COMBINE_FACTOR_TEXTURE_RGB = 5,
    GR_COMBINE_FACTOR_DETAIL_FACTOR = 4, GR_COMBINE_FACTOR_LOD_FRACTION = 5, GR_COMBINE_FACTOR_ONE = 8,
    GR_COMBINE_FACTOR_ONE_MINUS_LOCAL = 9, GR_COMBINE_FACTOR_ONE_MINUS_OTHER_ALPHA = 0xa,
    GR_COMBINE_FACTOR_ONE_MINUS_LOCAL_ALPHA = 0xb, GR_COMBINE_FACTOR_ONE_MINUS_TEXTURE_ALPHA = 0xc,
    GR_COMBINE_FACTOR_ONE_MINUS_DETAIL_FACTOR = 0xc, GR_COMBINE_FACTOR_ONE_MINUS_LOD_FRACTION = 0xd,
};
enum { GR_COMBINE_LOCAL_ITERATED = 0, GR_COMBINE_LOCAL_CONSTANT = 1, GR_COMBINE_LOCAL_DEPTH = 2 };
enum { GR_COMBINE_OTHER_ITERATED = 0, GR_COMBINE_OTHER_TEXTURE = 1, GR_COMBINE_OTHER_CONSTANT = 2 };
enum { GR_ALPHASOURCE_CC_ALPHA, GR_ALPHASOURCE_ITERATED_ALPHA, GR_ALPHASOURCE_TEXTURE_ALPHA,
       GR_ALPHASOURCE_TEXTURE_ALPHA_TIMES_ITERATED_ALPHA };
enum {
    GR_COLORCOMBINE_ZERO, GR_COLORCOMBINE_CCRGB, GR_COLORCOMBINE_ITRGB, GR_COLORCOMBINE_ITRGB_DELTA0,
    GR_COLORCOMBINE_DECAL_TEXTURE, GR_COLORCOMBINE_TEXTURE_TIMES_CCRGB, GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB,
    GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_DELTA0, GR_COLORCOMBINE_TEXTURE_TIMES_ITRGB_ADD_ALPHA,
    GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA, GR_COLORCOMBINE_TEXTURE_TIMES_ALPHA_ADD_ITRGB,
    GR_COLORCOMBINE_TEXTURE_ADD_ITRGB, GR_COLORCOMBINE_TEXTURE_SUB_ITRGB,
    GR_COLORCOMBINE_CCRGB_BLEND_ITRGB_ON_TEXALPHA, GR_COLORCOMBINE_DIFF_SPEC_A, GR_COLORCOMBINE_DIFF_SPEC_B,
    GR_COLORCOMBINE_ONE,
};
enum {
    GR_TEXTURECOMBINE_ZERO, GR_TEXTURECOMBINE_DECAL, GR_TEXTURECOMBINE_OTHER, GR_TEXTURECOMBINE_ADD,
    GR_TEXTURECOMBINE_MULTIPLY, GR_TEXTURECOMBINE_SUBTRACT, GR_TEXTURECOMBINE_DETAIL, GR_TEXTURECOMBINE_DETAIL_OTHER,
    GR_TEXTURECOMBINE_TRILINEAR_ODD, GR_TEXTURECOMBINE_TRILINEAR_EVEN, GR_TEXTURECOMBINE_ONE,
};

// Blending and tests.
enum {
    GR_BLEND_ZERO = 0, GR_BLEND_SRC_ALPHA = 1, GR_BLEND_SRC_COLOR = 2, GR_BLEND_DST_COLOR = 2, GR_BLEND_DST_ALPHA = 3,
    GR_BLEND_ONE = 4, GR_BLEND_ONE_MINUS_SRC_ALPHA = 5, GR_BLEND_ONE_MINUS_SRC_COLOR = 6,
    GR_BLEND_ONE_MINUS_DST_COLOR = 6, GR_BLEND_ONE_MINUS_DST_ALPHA = 7, GR_BLEND_ALPHA_SATURATE = 0xf,
    GR_BLEND_PREFOG_COLOR = 0xf,
};
enum { GR_CMP_NEVER, GR_CMP_LESS, GR_CMP_EQUAL, GR_CMP_LEQUAL, GR_CMP_GREATER, GR_CMP_NOTEQUAL, GR_CMP_GEQUAL,
       GR_CMP_ALWAYS };
enum { GR_CULL_DISABLE, GR_CULL_NEGATIVE, GR_CULL_POSITIVE };
enum { GR_DEPTHBUFFER_DISABLE, GR_DEPTHBUFFER_ZBUFFER, GR_DEPTHBUFFER_WBUFFER,
       GR_DEPTHBUFFER_ZBUFFER_COMPARE_TO_BIAS, GR_DEPTHBUFFER_WBUFFER_COMPARE_TO_BIAS };
enum { GR_DITHER_DISABLE, GR_DITHER_2x2, GR_DITHER_4x4 };
enum { GR_CHROMAKEY_DISABLE, GR_CHROMAKEY_ENABLE };
#define GR_ZDEPTHVALUE_NEAREST 0xFFFF
#define GR_ZDEPTHVALUE_FARTHEST 0x0000
#define GR_WDEPTHVALUE_NEAREST 0x0000
#define GR_WDEPTHVALUE_FARTHEST 0xFFFF

// Fog. Glide 2: the table is indexed by w. Glide 3 renamed the modes (WITH_TABLE_ON_Q is
// Glide 2's WITH_TABLE) and dropped iterated alpha (an extension brings it back as 4).
enum { GR_FOG_DISABLE = 0, GR_FOG_WITH_ITERATED_ALPHA = 1, GR_FOG_WITH_TABLE = 2, GR_FOG_WITH_ITERATED_Z = 3 };
#define GR_FOG_MULT2 0x100
#define GR_FOG_ADD2 0x200
#define G3_FOG_WITH_TABLE_ON_FOGCOORD_EXT 1
#define G3_FOG_WITH_TABLE_ON_Q 2
#define G3_FOG_WITH_ITERATED_Z 3
#define G3_FOG_WITH_ITERATED_ALPHA_EXT 4
#define GR_FOG_TABLE_SIZE 64

// Textures. Glide 2: LODs 0 (256) to 8 (1), aspects 0 (8x1) to 6 (1x8). Glide 3: log2
// values, LOD 8 (256) to 0 (1), aspect 3 (8x1) to -3 (1x8).
enum { GR_LOD_256, GR_LOD_128, GR_LOD_64, GR_LOD_32, GR_LOD_16, GR_LOD_8, GR_LOD_4, GR_LOD_2, GR_LOD_1 };
enum { GR_ASPECT_8x1, GR_ASPECT_4x1, GR_ASPECT_2x1, GR_ASPECT_1x1, GR_ASPECT_1x2, GR_ASPECT_1x4, GR_ASPECT_1x8 };
enum {
    GR_TEXFMT_8BIT = 0, GR_TEXFMT_RGB_332 = 0, GR_TEXFMT_YIQ_422 = 1, GR_TEXFMT_ALPHA_8 = 2, GR_TEXFMT_INTENSITY_8 = 3,
    GR_TEXFMT_ALPHA_INTENSITY_44 = 4, GR_TEXFMT_P_8 = 5, GR_TEXFMT_RSVD0 = 6, GR_TEXFMT_P_8_6666 = 6, GR_TEXFMT_RSVD1 = 7,
    GR_TEXFMT_16BIT = 8, GR_TEXFMT_ARGB_8332 = 8, GR_TEXFMT_AYIQ_8422 = 9, GR_TEXFMT_RGB_565 = 0xa,
    GR_TEXFMT_ARGB_1555 = 0xb, GR_TEXFMT_ARGB_4444 = 0xc, GR_TEXFMT_ALPHA_INTENSITY_88 = 0xd, GR_TEXFMT_AP_88 = 0xe,
    GR_TEXFMT_RSVD2 = 0xf,
};
#define GR_MIPMAPLEVELMASK_EVEN 1
#define GR_MIPMAPLEVELMASK_ODD 2
#define GR_MIPMAPLEVELMASK_BOTH 3
enum { GR_MIPMAP_DISABLE, GR_MIPMAP_NEAREST, GR_MIPMAP_NEAREST_DITHER };
enum { GR_TEXTURECLAMP_WRAP, GR_TEXTURECLAMP_CLAMP, GR_TEXTURECLAMP_MIRROR_EXT };
enum { GR_TEXTUREFILTER_POINT_SAMPLED, GR_TEXTUREFILTER_BILINEAR };
enum { GR_TEXTABLE_NCC0, GR_TEXTABLE_NCC1, GR_TEXTABLE_PALETTE, GR_TEXTABLE_PALETTE_6666_EXT };
enum { GR_NCCTABLE_NCC0, GR_NCCTABLE_NCC1 };
enum { GR_TEXBASE_256, GR_TEXBASE_128, GR_TEXBASE_64, GR_TEXBASE_32_TO_1 };  // Glide 2 values
#define G3_TEXBASE_2048 0x7  // Glide 3 counts the other way: 256 is 3, 32 to 1 is 0
#define G3_TEXBASE_256 0x3
#define G3_TEXBASE_128 0x2
#define G3_TEXBASE_64 0x1
#define G3_TEXBASE_32_TO_1 0x0

// The linear frame buffer.
#define GR_LFB_READ_ONLY 0x00
#define GR_LFB_WRITE_ONLY 0x01
#define GR_LFB_IDLE 0x00
#define GR_LFB_NOIDLE 0x10
enum {
    GR_LFBWRITEMODE_565 = 0, GR_LFBWRITEMODE_555 = 1, GR_LFBWRITEMODE_1555 = 2, GR_LFBWRITEMODE_888 = 4,
    GR_LFBWRITEMODE_8888 = 5, GR_LFBWRITEMODE_565_DEPTH = 0xc, GR_LFBWRITEMODE_555_DEPTH = 0xd,
    GR_LFBWRITEMODE_1555_DEPTH = 0xe, GR_LFBWRITEMODE_ZA16 = 0xf, GR_LFBWRITEMODE_ANY = 0xff,
};
#define GR_LFB_SRC_FMT_RLE16 0x80
enum { GR_LFBBYPASS_DISABLE, GR_LFBBYPASS_ENABLE };
enum { GR_PASSTHRU_SHOW_VGA, GR_PASSTHRU_SHOW_SST1 };

enum { GR_SSTTYPE_VOODOO = 0, GR_SSTTYPE_SST96 = 1, GR_SSTTYPE_AT3D = 2, GR_SSTTYPE_Voodoo2 = 3 };

enum { GR_HINT_STWHINT = 0, GR_HINT_FIFOCHECKHINT = 1, GR_HINT_FPUPRECISION = 2, GR_HINT_ALLOW_MIPMAP_DITHER = 3,
       GR_HINT_LFB_WRITE = 4, GR_HINT_LFB_PROTECT = 5, GR_HINT_LFB_RESET = 6 };
enum { GR_CONTROL_ACTIVATE = 1, GR_CONTROL_DEACTIVATE = 2, GR_CONTROL_RESIZE = 3, GR_CONTROL_MOVE = 4 };

// Glide 3 only.
enum { GR_POINTS, GR_LINE_STRIP, GR_LINES, GR_POLYGON, GR_TRIANGLE_STRIP, GR_TRIANGLE_FAN, GR_TRIANGLES,
       GR_TRIANGLE_STRIP_CONTINUE, GR_TRIANGLE_FAN_CONTINUE };
#define GR_PARAM_XY 0x01
#define GR_PARAM_Z 0x02
#define GR_PARAM_W 0x03
#define GR_PARAM_Q 0x04
#define GR_PARAM_FOG_EXT 0x05
#define GR_PARAM_A 0x10
#define GR_PARAM_RGB 0x20
#define GR_PARAM_PARGB 0x30
#define GR_PARAM_ST0 0x40
#define GR_PARAM_ST1 0x41
#define GR_PARAM_ST2 0x42
#define GR_PARAM_Q0 0x50
#define GR_PARAM_Q1 0x51
#define GR_PARAM_Q2 0x52
#define GR_PARAM_DISABLE 0
#define GR_PARAM_ENABLE 1
#define GR_WINDOW_COORDS 0
#define GR_CLIP_COORDS 1
#define GR_AA_ORDERED 0x01
#define GR_ALLOW_MIPMAP_DITHER 0x02
#define GR_PASSTHRU 0x03
#define GR_SHAMELESS_PLUG 0x04
#define GR_VIDEO_SMOOTHING 0x05
enum { GR_STIPPLE_DISABLE, GR_STIPPLE_PATTERN, GR_STIPPLE_ROTATE };
// grGet (h3 values).
enum {
    GR_BITS_DEPTH = 0x01, GR_BITS_RGBA = 0x02, GR_FIFO_FULLNESS = 0x03, GR_FOG_TABLE_ENTRIES = 0x04,
    GR_GAMMA_TABLE_ENTRIES = 0x05, GR_GLIDE_STATE_SIZE = 0x06, GR_GLIDE_VERTEXLAYOUT_SIZE = 0x07, GR_IS_BUSY = 0x08,
    GR_LFB_PIXEL_PIPE = 0x09, GR_MAX_TEXTURE_SIZE = 0x0a, GR_MAX_TEXTURE_ASPECT_RATIO = 0x0b, GR_MEMORY_FB = 0x0c,
    GR_MEMORY_TMU = 0x0d, GR_MEMORY_UMA = 0x0e, GR_NUM_BOARDS = 0x0f, GR_NON_POWER_OF_TWO_TEXTURES = 0x10,
    GR_NUM_FB = 0x11, GR_NUM_SWAP_HISTORY_BUFFER = 0x12, GR_NUM_TMU = 0x13, GR_PENDING_BUFFERSWAPS = 0x14,
    GR_REVISION_FB = 0x15, GR_REVISION_TMU = 0x16, GR_STATS_LINES = 0x17, GR_STATS_PIXELS_AFUNC_FAIL = 0x18,
    GR_STATS_PIXELS_CHROMA_FAIL = 0x19, GR_STATS_PIXELS_DEPTHFUNC_FAIL = 0x1a, GR_STATS_PIXELS_IN = 0x1b,
    GR_STATS_PIXELS_OUT = 0x1c, GR_STATS_PIXELS = 0x1d, GR_STATS_POINTS = 0x1e, GR_STATS_TRIANGLES_IN = 0x1f,
    GR_STATS_TRIANGLES_OUT = 0x20, GR_STATS_TRIANGLES = 0x21, GR_SWAP_HISTORY = 0x22, GR_SUPPORTS_PASSTHRU = 0x23,
    GR_TEXTURE_ALIGN = 0x24, GR_VIDEO_POSITION = 0x25, GR_VIEWPORT = 0x26, GR_WDEPTH_MIN_MAX = 0x27,
    GR_ZDEPTH_MIN_MAX = 0x28, GR_VERTEX_PARAMETER = 0x29, GR_BITS_GAMMA = 0x2a,
};
enum { GR_EXTENSION = 0xa0, GR_HARDWARE = 0xa1, GR_RENDERER = 0xa2, GR_VENDOR = 0xa3, GR_VERSION = 0xa4 };

// Structures.
struct GrTmuVertex {
    float sow, tow, oow;
};
// Glide 2's vertex (compiled with GLIDE_NUM_TMU 2).
struct GrVertex {
    float x, y, z;
    float r, g, b;
    float ooz;
    float a;
    float oow;
    GrTmuVertex tmuvtx[2];
};
// Glide 2's texture description; Glide 3 has the same layout with log2 LODs and aspect.
struct GrTexInfo {
    GrLOD_t smallLod;
    GrLOD_t largeLod;
    GrAspectRatio_t aspectRatio;
    GrTextureFormat_t format;
    void* data;
};
struct GrLfbInfo_t {
    int size;
    void* lfbPtr;
    FxU32 strideInBytes;
    GrLfbWriteMode_t writeMode;
    GrOriginLocation_t origin;
};
struct GrTMUConfig_t {
    int tmuRev;
    int tmuRam;
};
struct GrVoodooConfig_t {
    int fbRam;
    int fbiRev;
    int nTexelfx;
    FxBool sliDetect;
    GrTMUConfig_t tmuConfig[2];
};
struct GrSst96Config_t {
    int fbRam;
    int nTexelfx;
    GrTMUConfig_t tmuConfig;
};
struct GrHwConfiguration {
    int num_sst;
    struct {
        GrSstType type;
        union {
            GrVoodooConfig_t VoodooConfig;
            GrSst96Config_t SST96Config;
            int AT3DConfig;
        } sstBoard;
    } SSTs[4];
};
struct GrSstPerfStats_t {
    FxU32 pixelsIn, chromaFail, zFuncFail, aFuncFail, pixelsOut;
};
struct GrResolution {  // Glide 3's grQueryResolutions
    GrScreenResolution_t resolution;
    GrScreenRefresh_t refresh;
    int numColorBuffers;
    int numAuxBuffers;
};
struct Gu3dfHeader {
    FxU32 width, height;
    int small_lod, large_lod;
    GrAspectRatio_t aspect_ratio;
    GrTextureFormat_t format;
};
struct GuNccTable {
    FxU8 yRGB[16];
    FxI16 iRGB[4][3];
    FxI16 qRGB[4][3];
    FxU32 packed_data[12];
};
struct GuTexPalette {
    FxU32 data[256];
};
union GuTexTable {
    GuNccTable nccTable;
    GuTexPalette palette;
};
struct Gu3dfInfo {
    Gu3dfHeader header;
    GuTexTable table;
    void* data;
    FxU32 mem_required;
};
// Glide 2's gu texture manager record (guTexAllocateMemory and friends).
struct GrMipMapInfo {
    int sst;
    FxBool valid;
    int width, height;
    GrAspectRatio_t aspect_ratio;
    void* data;
    GrTextureFormat_t format;
    GrMipMapMode_t mipmap_mode;
    GrTextureFilterMode_t magfilter_mode;
    GrTextureFilterMode_t minfilter_mode;
    GrTextureClampMode_t s_clamp_mode;
    GrTextureClampMode_t t_clamp_mode;
    FxU32 tLOD;
    FxU32 tTextureMode;
    FxU32 lod_bias;
    GrLOD_t lod_min, lod_max;
    int tmu;
    FxU32 odd_even_mask;
    FxU32 tmu_base_address;
    FxBool trilinear;
    GuNccTable ncc_table;
};
#define GR_NULL_MIPMAP_HANDLE ((GrMipMapId_t)-1)
#define GLIDE_STATE_PAD_SIZE 312  // Glide 2's GrState (grGlideGetState's buffer), bytes
// clang-format on
