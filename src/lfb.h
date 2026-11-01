// The linear frame buffer: grLfbLock gives the game memory laid out as the Voodoo's (1024
// pixels a row), filled from the buffer; what it changes in it goes to the buffer at
// grLfbUnlock. grLfbReadRegion and grLfbWriteRegion copy directly.
#pragma once
#include "voodoo.h"

namespace gl2d9 {

FxBool LfbLock(GrLock_t type, GrBuffer_t buffer, GrLfbWriteMode_t writeMode, GrOriginLocation_t origin,
               FxBool pixelPipeline, GrLfbInfo_t* info);
FxBool LfbUnlock(GrLock_t type, GrBuffer_t buffer);
FxBool LfbReadRegion(GrBuffer_t src, FxU32 x, FxU32 y, FxU32 w, FxU32 h, FxU32 dstStride, void* dst);
FxBool LfbWriteRegion(GrBuffer_t dst, FxU32 x, FxU32 y, GrLfbSrcFmt_t format, FxU32 w, FxU32 h, FxBool pixelPipeline,
                      FxI32 srcStride, const void* src);
void LfbWriteColorFormat(GrColorFormat_t format);
void LfbBeforeSwap();  // what a write lock held across a swap changed goes to its buffer first
void LfbReset();       // grSstWinClose: the locks dropped

}  // namespace gl2d9
