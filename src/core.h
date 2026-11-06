// What the Glide 2 and Glide 3 entry points share: opening the screen, drawing, clearing and
// swapping, texture downloads, the hardware as reported. The entry points (api2.cpp,
// api3.cpp) convert their arguments (vertices, LODs, aspects) and take the lock; these take
// Glide 2's constants and the renderer's vertices.
#pragma once
#include "render.h"
#include "state.h"

namespace gl2d9 {

void CoreInit();      // grGlideInit
void CoreShutdown();  // grGlideShutdown
// Width and height of a resolution (false: not one there is).
bool ResolutionSize(GrScreenResolution_t res, int* w, int* h);
FxBool CoreWinOpen(HWND hwnd, GrScreenResolution_t res, GrScreenRefresh_t refresh, GrColorFormat_t format,
                   GrOriginLocation_t origin, int numColorBuffers, int numAuxBuffers);
void CoreWinClose();

// Primitives in screen coordinates, as the triangle setup takes them.
void CoreTriangle(const RVtx& a, const RVtx& b, const RVtx& c);
void CorePoint(const RVtx& p);
void CoreLine(const RVtx& a, const RVtx& b);
void CoreFan(const RVtx* v, int n);           // a convex polygon (a fan from v[0])
void CoreTriangleList(const RVtx* v, int n);  // independent triangles
void CoreStrip(const RVtx* v, int n, bool odd);
// Antialiased (Glide's grAADraw*): with alpha-blended fringes on the edges.
void CoreAATriangle(const RVtx& a, const RVtx& b, const RVtx& c, bool ab, bool bc, bool ca);
void CoreAAFan(const RVtx* v, int n);  // every outline edge
void CoreAAPoint(const RVtx& p);
void CoreAALine(const RVtx& a, const RVtx& b);

void CoreClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth);
void CoreSwap(int interval);

// Texture memory.
FxU32 CoreTexMaxAddress(GrChipID_t tmu);
void CoreTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, const GrTexInfo* info);
void CoreTexDownloadLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                          GrTextureFormat_t format, FxU32 evenOdd, const void* data, int t, int maxT);
// grTexDownloadTable(Partial): an NCC table (0, 1) or the palette (2), Glide 2 semantics.
void CoreTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, const void* data, int start, int end);

// The board as grSstQueryHardware reports it (type: GR_SSTTYPE_VOODOO or _Voodoo2).
void CoreHwConfig(GrHwConfiguration* hw, GrSstType type);

}  // namespace gl2d9
