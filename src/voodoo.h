// The emulated Voodoo: the registers Glide programs (as 3dfx's Glide does, call for call),
// the texture memory of each TMU, and the tables downloaded into it. The renderer
// (render.cpp) draws what these registers describe on Direct3D 9.
#pragma once
#include "common.h"
#include "glidedefs.h"

#include <algorithm>

namespace gl2d9 {

// ---------------------------------------------------------------- register bits (Voodoo 1/2)
#define BIT(i) (1u << (i))
// fbzColorPath
#define SST_RGBSELECT_SHIFT 0
#define SST_RGBSELECT (0x3u << SST_RGBSELECT_SHIFT)
#define SST_ASELECT_SHIFT 2
#define SST_ASELECT (0x3u << SST_ASELECT_SHIFT)
#define SST_LOCALSELECT_SHIFT 4
#define SST_LOCALSELECT BIT(4)
#define SST_ALOCALSELECT_SHIFT 5
#define SST_ALOCALSELECT (0x3u << SST_ALOCALSELECT_SHIFT)
#define SST_LOCALSELECT_OVERRIDE_WITH_ATEX BIT(7)
#define SST_CC_ZERO_OTHER BIT(8)
#define SST_CC_SUB_CLOCAL BIT(9)
#define SST_CC_MSELECT_SHIFT 10
#define SST_CC_MSELECT (0x7u << SST_CC_MSELECT_SHIFT)
#define SST_CC_REVERSE_BLEND BIT(13)
#define SST_CC_ADD_CLOCAL BIT(14)
#define SST_CC_ADD_ALOCAL BIT(15)
#define SST_CC_INVERT_OUTPUT BIT(16)
#define SST_CCA_ZERO_OTHER BIT(17)
#define SST_CCA_SUB_CLOCAL BIT(18)
#define SST_CCA_MSELECT_SHIFT 19
#define SST_CCA_MSELECT (0x7u << SST_CCA_MSELECT_SHIFT)
#define SST_CCA_REVERSE_BLEND BIT(22)
#define SST_CCA_ADD_CLOCAL BIT(23)
#define SST_CCA_ADD_ALOCAL BIT(24)
#define SST_CCA_INVERT_OUTPUT BIT(25)
#define SST_PARMADJUST BIT(26)
#define SST_ENTEXTUREMAP BIT(27)
#define SST_RGBAZ_CLAMP BIT(28)
// fbzMode
#define SST_ENRECTCLIP BIT(0)
#define SST_ENCHROMAKEY BIT(1)
#define SST_ENSTIPPLE BIT(2)
#define SST_WBUFFER BIT(3)
#define SST_ENDEPTHBUFFER BIT(4)
#define SST_ZFUNC_SHIFT 5
#define SST_ZFUNC (0x7u << SST_ZFUNC_SHIFT)
#define SST_ENDITHER BIT(8)
#define SST_RGBWRMASK BIT(9)
#define SST_ZAWRMASK BIT(10)
#define SST_DITHER2x2 BIT(11)
#define SST_ENSTIPPLEPATTERN BIT(12)
#define SST_ENALPHAMASK BIT(13)
#define SST_DRAWBUFFER_SHIFT 14
#define SST_DRAWBUFFER (0x3u << SST_DRAWBUFFER_SHIFT)
#define SST_DRAWBUFFER_FRONT (0u << SST_DRAWBUFFER_SHIFT)
#define SST_DRAWBUFFER_BACK (1u << SST_DRAWBUFFER_SHIFT)
#define SST_ENZBIAS BIT(16)
#define SST_YORIGIN BIT(17)
#define SST_ENALPHABUFFER BIT(18)
#define SST_ENDITHERSUBTRACT BIT(19)
#define SST_ZCOMPARE_TO_ZACOLOR BIT(20)
// alphaMode
#define SST_ENALPHAFUNC BIT(0)
#define SST_ALPHAFUNC_SHIFT 1
#define SST_ALPHAFUNC (0x7u << SST_ALPHAFUNC_SHIFT)
#define SST_ENALPHABLEND BIT(4)
#define SST_RGBSRCFACT_SHIFT 8
#define SST_RGBSRCFACT (0xFu << SST_RGBSRCFACT_SHIFT)
#define SST_RGBDSTFACT_SHIFT 12
#define SST_RGBDSTFACT (0xFu << SST_RGBDSTFACT_SHIFT)
#define SST_ASRCFACT_SHIFT 16
#define SST_ASRCFACT (0xFu << SST_ASRCFACT_SHIFT)
#define SST_ADSTFACT_SHIFT 20
#define SST_ADSTFACT (0xFu << SST_ADSTFACT_SHIFT)
#define SST_ALPHAREF_SHIFT 24
#define SST_ALPHAREF (0xFFu << SST_ALPHAREF_SHIFT)
// fogMode
#define SST_ENFOGGING BIT(0)
#define SST_FOGADD BIT(1)
#define SST_FOGMULT BIT(2)
#define SST_FOG_ALPHA BIT(3)
#define SST_FOG_Z BIT(4)
#define SST_FOG_CONSTANT BIT(5)
// zaColor
#define SST_ZACOLOR_DEPTH 0xFFFFu
#define SST_ZACOLOR_ALPHA_SHIFT 24
#define SST_ZACOLOR_ALPHA (0xFFu << SST_ZACOLOR_ALPHA_SHIFT)
// clip registers
#define SST_CLIPLEFT_SHIFT 16
#define SST_CLIPRIGHT_SHIFT 0
#define SST_CLIPBOTTOM_SHIFT 16
#define SST_CLIPTOP_SHIFT 0
// lfbMode
#define SST_LFB_FORMAT_SHIFT 0
#define SST_LFB_FORMAT (0xFu << SST_LFB_FORMAT_SHIFT)
#define SST_LFB_WRITEBUFSELECT_SHIFT 4
#define SST_LFB_WRITEBUFSELECT (0x3u << SST_LFB_WRITEBUFSELECT_SHIFT)
#define SST_LFB_READBUFSELECT_SHIFT 6
#define SST_LFB_READBUFSELECT (0x3u << SST_LFB_READBUFSELECT_SHIFT)
#define SST_LFB_ENPIXPIPE BIT(8)
#define SST_LFB_RGBALANES_SHIFT 9
#define SST_LFB_RGBALANES (0x3u << SST_LFB_RGBALANES_SHIFT)
#define SST_LFB_WRITE_SWAP16 BIT(11)
#define SST_LFB_WRITE_BYTESWAP BIT(12)
#define SST_LFB_YORIGIN BIT(13)
#define SST_LFB_WSELECT BIT(14)
// textureMode
#define SST_TPERSP_ST BIT(0)
#define SST_TMINFILTER BIT(1)
#define SST_TMAGFILTER BIT(2)
#define SST_TCLAMPW BIT(3)
#define SST_TLODDITHER BIT(4)
#define SST_TNCCSELECT BIT(5)
#define SST_TCLAMPS BIT(6)
#define SST_TCLAMPT BIT(7)
#define SST_TFORMAT_SHIFT 8
#define SST_TFORMAT (0xFu << SST_TFORMAT_SHIFT)
#define SST_TC_ZERO_OTHER BIT(12)
#define SST_TC_SUB_CLOCAL BIT(13)
#define SST_TC_MSELECT_SHIFT 14
#define SST_TC_MSELECT (0x7u << SST_TC_MSELECT_SHIFT)
#define SST_TC_REVERSE_BLEND BIT(17)
#define SST_TC_ADD_CLOCAL BIT(18)
#define SST_TC_ADD_ALOCAL BIT(19)
#define SST_TC_INVERT_OUTPUT BIT(20)
#define SST_TCA_ZERO_OTHER BIT(21)
#define SST_TCA_SUB_CLOCAL BIT(22)
#define SST_TCA_MSELECT_SHIFT 23
#define SST_TCA_MSELECT (0x7u << SST_TCA_MSELECT_SHIFT)
#define SST_TCA_REVERSE_BLEND BIT(26)
#define SST_TCA_ADD_CLOCAL BIT(27)
#define SST_TCA_ADD_ALOCAL BIT(28)
#define SST_TCA_INVERT_OUTPUT BIT(29)
#define SST_TRILINEAR BIT(30)
#define SST_TCOMBINE (0x1FFu << 12)
#define SST_TACOMBINE (0x1FFu << 21)
#define SST_TC_BLEND_LODFRAC (5u << SST_TC_MSELECT_SHIFT)
// tLOD
#define SST_LODMIN_SHIFT 0
#define SST_LODMIN (0x3Fu << SST_LODMIN_SHIFT)
#define SST_LODMAX_SHIFT 6
#define SST_LODMAX (0x3Fu << SST_LODMAX_SHIFT)
#define SST_LODBIAS_SHIFT 12
#define SST_LODBIAS (0x3Fu << SST_LODBIAS_SHIFT)
#define SST_LOD_ODD BIT(18)
#define SST_LOD_TSPLIT BIT(19)
#define SST_LOD_S_IS_WIDER BIT(20)
#define SST_LOD_ASPECT_SHIFT 21
#define SST_LOD_ASPECT (0x3u << SST_LOD_ASPECT_SHIFT)
#define SST_TMULTIBASEADDR BIT(24)
// Glide 3's GR_TEXTURECLAMP_MIRROR_EXT (the Voodoo 3's tLOD bits).
#define SST_LOD_TMIRRORS BIT(28)
#define SST_LOD_TMIRRORT BIT(29)
#define SST_TLOD_MINMAX_INT(lodmin, lodmax) \
    ((((lodmin) << 2) << SST_LODMIN_SHIFT) | (((lodmax) << 2) << SST_LODMAX_SHIFT))
// tDetail
#define SST_DETAIL_MAX_SHIFT 0
#define SST_DETAIL_MAX (0xFFu << SST_DETAIL_MAX_SHIFT)
#define SST_DETAIL_BIAS_SHIFT 8
#define SST_DETAIL_BIAS (0x3Fu << SST_DETAIL_BIAS_SHIFT)
#define SST_DETAIL_SCALE_SHIFT 14
#define SST_DETAIL_SCALE (0x7u << SST_DETAIL_SCALE_SHIFT)

constexpr int kMaxTmu = 3;

struct TmuRegs {
    FxU32 textureMode;
    FxU32 tLOD;
    FxU32 tDetail;
    FxU32 texBaseAddr;                                // in 8-byte units: where a 256 level would start (grTexSource)
    FxU32 texBaseAddr1, texBaseAddr2, texBaseAddr38;  // grTexMultibase
};

// The registers of the FBI and the TMUs that Glide keeps a shadow of.
struct Regs {
    FxU32 fbzColorPath;
    FxU32 fogMode;
    FxU32 alphaMode;
    FxU32 fbzMode;
    FxU32 lfbMode;
    FxU32 clipLeftRight;
    FxU32 clipBottomTop;
    FxU32 fogColor;
    FxU32 zaColor;
    FxU32 chromaKey;
    FxU32 stipple;
    FxU32 color0;
    FxU32 color1;
    TmuRegs tmu[kMaxTmu];
    FxU8 fogTable[GR_FOG_TABLE_SIZE];
};

// Glide's per-TMU bookkeeping beside the registers (gc->state.tmu_config).
struct TmuConfig {
    GrLOD_t smallLod, largeLod;
    FxU32 evenOdd;
    GrNCCTable_t nccTable;
    GrMipMapMode_t mmMode;
};

// Glide's state (gc->state): what grGlideGetState saves and grGlideSetState puts back. It
// must fit the 312 bytes games reserve for a GrState.
struct GlideState {
    Regs r;
    TmuConfig tmu[kMaxTmu];
    FxI32 cullMode;
    FxU32 paramHints;
    FxU8 ccRequiresTexture, acRequiresTexture, ccDelta0;
    FxU8 allowLodDither;
    float ccR, ccG, ccB, ccA;  // grConstantColorValue4 (the ITRGB_DELTA0 color)
    FxU32 lfbConstantAlpha;
    FxU32 lfbConstantDepth;
};
static_assert(sizeof(GlideState) <= GLIDE_STATE_PAD_SIZE, "GrState is 312 bytes in Glide 2");

// Glide's per-board context (gc): the state plus what isn't saved with it.
struct Board {
    bool open = false;  // grSstWinOpen done
    GlideState s{};
    int numTmu = 2;
    FxU32 tmuMem = 4u << 20;  // bytes of texture memory a TMU has
    int screenW = 640, screenH = 480;
    GrScreenRefresh_t refresh = GR_REFRESH_NONE;
    GrColorFormat_t colorFormat = GR_COLORFORMAT_ARGB;
    int numColorBuffers = 2, numAuxBuffers = 1;
    HWND hwnd = nullptr;         // the window the screen is shown in
    unsigned stippleWrites = 0;  // writes to the stipple register (rotate mode starts over at each)
    GrErrorCallbackFnc_t errorCallback = nullptr;
    // NCC tables and palette as downloaded to each TMU (grTexDownloadTable).
    GuNccTable ncc[kMaxTmu][2]{};
    FxU32 palette[kMaxTmu][256]{};
    // A hash of each table's contents: a texture decoded with one is the same texture with
    // the same table again (games download a palette for every texture they draw).
    unsigned paletteHash[kMaxTmu] = {};
    unsigned nccHash[kMaxTmu][2] = {};
    // Glide 3: the last table downloaded was a 6666 palette (3dfx's gc->state.tex_table);
    // grTexSource then draws a P_8 texture as P_8_6666, as 3dfx's gtex.c does.
    bool palette6666 = false;
    // gu texture manager (guTexAllocateMemory): the free base per TMU and its records.
    FxU32 freeMemBase[kMaxTmu] = {};
    std::vector<GrMipMapInfo> mipmaps;
    GrMipMapId_t currentMipMap[kMaxTmu] = {GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE};
    // Which NCC table of a TMU guTexSource last gave to a mipmap, the one it uses for that
    // mipmap again, and the one it gives to the next.
    GrMipMapId_t nccMipMap[kMaxTmu][2] = {{GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE},
                                          {GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE},
                                          {GR_NULL_MIPMAP_HANDLE, GR_NULL_MIPMAP_HANDLE}};
    int nextNcc[kMaxTmu] = {};
    // grTriStats / grSstPerfStats counters.
    FxU32 trisIn = 0, trisOut = 0, swaps = 0, pointsDrawn = 0, linesDrawn = 0;
};
extern Board g_board;
inline GlideState& St() { return g_board.s; }

// Texture memory, one byte array per TMU, and the generation of each of its 4 KB pages
// (bumped by every write): the renderer's textures made from it are checked against them.
struct TexMemory {
    std::vector<FxU8> bytes;
    std::vector<unsigned> pageGen;
    void Resize(size_t n) {
        bytes.assign(n, 0);
        pageGen.assign((n + 4095) / 4096 + 1, 1);
    }
    void Touch(size_t start, size_t len) {
        if (!len) return;
        const size_t end = std::min(bytes.size(), start + len);
        for (size_t p = start >> 12; p <= (end - 1) >> 12 && p < pageGen.size(); p++) pageGen[p]++;
    }
};
extern TexMemory g_texMem[kMaxTmu];

// Mipmap size tables (texels), as Glide's ditex.c has them: [aspect class][lod], the
// aspect class 0 = 8:1 ... 3 = 1:1 (a tall aspect mirrored onto its wide one).
FxU32 MipMapSize(int aspectClass, int lod);
FxU32 MipMapOffset(int aspectClass, int lod);        // all levels larger than lod
FxU32 MipMapOffsetTsplit(int aspectClass, int lod);  // levels of lod's parity larger than lod
int AspectClass(GrAspectRatio_t aspect);             // Glide 2 aspect -> class
// Glide's _grTexTextureMemRequired and _grTexCalcBaseAddress (Glide 2 LODs and aspects).
FxU32 TexMemRequired(GrLOD_t smallLod, GrLOD_t largeLod, GrAspectRatio_t aspect, GrTextureFormat_t format,
                     FxU32 evenOdd);
FxU32 TexCalcBaseAddress(FxU32 start, GrLOD_t largeLod, GrAspectRatio_t aspect, GrTextureFormat_t format,
                         FxU32 evenOdd);
// Width and height of a level (Glide 2 LOD and aspect).
void LevelSize(GrLOD_t lod, GrAspectRatio_t aspect, int* w, int* h);

// tLOD's aspect bits and its split bits for an even/odd mask (Glide 2 values).
FxU32 TLodAspectBits(GrAspectRatio_t aspect);
FxU32 TLodEvenOddBits(FxU32 evenOdd);

// The color as the registers hold it (ARGB), from a GrColor_t in the format of grSstWinOpen.
FxU32 SwizzleColor(GrColor_t c);

// Glide's error callback (grErrorSetCallback), or a log line.
void GlideError(const char* msg, bool fatal);

}  // namespace gl2d9
