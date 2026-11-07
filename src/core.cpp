// ---------------------------------------------------------------------------------------------
// The antialiasing in this file is derived from 3dfx Interactive's Glide source code (Glide 2's
// gaa.c: the antialiasing, under "antialiasing" below), copyright 3dfx Interactive, Inc. 1999,
// and is distributed under the 3DFX GLIDE Source Code General Public License (LICENSE-3DFX);
// recipients who wish to copy, distribute or modify it can only do so under that license. It is
// provided without warranty (section 5 of that license). Changed by PlayNeth, 1 November 2026:
// translated to C++ for gl2d9, to run on Direct3D 9. The rest of gl2d9 is under the MIT license
// (LICENSE).
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
// See core.h.
#include "core.h"

#include "gu.h"

#include <algorithm>
#include <cmath>
#include <exception>

#include "lfb.h"
#include "texmem.h"
#include "window.h"

namespace gl2d9 {

namespace {

struct ResSize {
    int w, h;
};
const ResSize kRes[] = {
    {320, 200},  {320, 240},  {400, 256},   {512, 384},   {640, 200},   {640, 350},   {640, 400},   {640, 480},
    {800, 600},  {960, 720},  {856, 480},   {512, 256},   {1024, 768},  {1280, 1024}, {1600, 1200}, {400, 300},
    {1152, 864}, {1280, 960}, {1600, 1024}, {1792, 1344}, {1856, 1392}, {1920, 1440}, {2048, 1536}, {2048, 2048},
};

// Games snapping their vertices to 1/16 pixel add (3 << 18) and take it off again; one that
// leaves it on (Glide's own debug output allows for it) is brought back to the screen.
inline float Unsnap(float v) { return std::fabs(v) >= 262144.0f ? v - 786432.0f : v; }

// Glide's triangle setup: zero-area triangles are dropped, and the culled side by the sign
// of the area (dxAB * dyBC - dxBC * dyAB): GR_CULL_NEGATIVE drops it negative, POSITIVE
// positive.
// (Of vertices already brought back to the screen: Snapped.)
bool VisibleSnapped(const RVtx& a, const RVtx& b, const RVtx& c) {
    const float dxAB = a.x - b.x, dxBC = b.x - c.x;
    const float dyAB = a.y - b.y, dyBC = b.y - c.y;
    const float area = dxAB * dyBC - dxBC * dyAB;
    if (area == 0.0f || std::isnan(area)) return false;
    const int cull = St().cullMode;
    if (cull == GR_CULL_NEGATIVE && area < 0) return false;
    if (cull == GR_CULL_POSITIVE && area > 0) return false;
    return true;
}

inline RVtx Snapped(const RVtx& v) {
    RVtx o = v;
    o.x = Unsnap(v.x);
    o.y = Unsnap(v.y);
    return o;
}

bool Visible(const RVtx& a, const RVtx& b, const RVtx& c) { return VisibleSnapped(Snapped(a), Snapped(b), Snapped(c)); }

std::vector<RVtx> g_tris;  // triangles of one call, drawn together

// Triangle a, b, c added to g_tris, brought back to the screen, unless culled (each vertex
// copied and brought back once, not once for the test and again for the drawing).
inline void AddTriangle(const RVtx& a, const RVtx& b, const RVtx& c) {
    g_board.trisIn++;
    const size_t at = g_tris.size();
    g_tris.push_back(Snapped(a));
    g_tris.push_back(Snapped(b));
    g_tris.push_back(Snapped(c));
    if (!VisibleSnapped(g_tris[at], g_tris[at + 1], g_tris[at + 2])) {
        g_tris.resize(at);
        return;
    }
    g_board.trisOut++;
}

// ---- antialiasing, as 3dfx's Glide does it (gaa.c): the primitive drawn, then a strip a
// pixel wide outside each antialiased edge, its outer side at alpha 0 (the game blends with
// alpha), unculled and without writing depth.
enum Sense { kSenseTop, kSenseLeft, kSenseBottom, kSenseRight };

// Which way edge a-b faces away from c (aaCalcEdgeSense).
Sense EdgeSense(const RVtx* a, const RVtx* b, const RVtx* c) {
    if (a->y >= b->y) std::swap(a, b);
    const float dx = a->x - b->x, dy = a->y - b->y;
    if (dx == 0.0f) return c->x > a->x ? kSenseLeft : kSenseRight;
    if (dy == 0.0f) return c->y > a->y ? kSenseBottom : kSenseTop;
    const float cp = (a->x - c->x) * (b->y - c->y) - (a->y - c->y) * (b->x - c->x);
    if (dx > 0.0f) {
        if (dx >= -dy) return cp > 0.0f ? kSenseTop : kSenseBottom;
        return cp > 0.0f ? kSenseRight : kSenseLeft;
    }
    if (dx <= dy) return cp < 0.0f ? kSenseTop : kSenseBottom;
    return cp < 0.0f ? kSenseLeft : kSenseRight;
}

// The two triangles along edge a-b (grAADrawTriEdge), into g_tris.
void EdgeTris(const RVtx& a, const RVtx& b, const RVtx& c) {
    RVtx e = a, f = b;
    e.a = f.a = 0.0f;
    switch (EdgeSense(&a, &b, &c)) {
    case kSenseTop: e.y += 1, f.y += 1; break;
    case kSenseLeft: e.x -= 1, f.x -= 1; break;
    case kSenseBottom: e.y -= 1, f.y -= 1; break;
    case kSenseRight: e.x += 1, f.x += 1; break;
    }
    const RVtx* order[6] = {&a, &e, &f, &a, &f, &b};
    for (const RVtx* v : order) g_tris.push_back(*v);
}

// g_tris drawn without depth writes (edges and fringes).
void DrawFringe() {
    if (g_tris.empty()) return;
    FxU32& fbz = St().r.fbzMode;
    const FxU32 saved = fbz;
    fbz &= ~SST_ZAWRMASK;
    RenderTriangles(g_tris.data(), int(g_tris.size()));
    fbz = saved;
    g_tris.clear();
}

}  // namespace

bool ResolutionSize(GrScreenResolution_t res, int* w, int* h) {
    const int n = g_version == kGlide3x ? int(sizeof(kRes) / sizeof(kRes[0])) : GR_RESOLUTION_400x300 + 1;
    if (res < 0 || res >= n) return false;
    *w = kRes[res].w;
    *h = kRes[res].h;
    return true;
}

void CallFailed(const char* where) {
    const char* what = "an exception";
    if (std::exception_ptr e = std::current_exception()) {
        try {
            std::rethrow_exception(e);
        } catch (const std::exception& x) { what = x.what(); } catch (...) {
        }
    }
    // The first five logged, then every hundredth (a game out of memory fails call after call).
    static LONG failures;
    const LONG n = InterlockedIncrement(&failures);
    if (n <= 5 || n % 100 == 0) GL_LOG("%s failed: %s (%ld so far): what it did dropped", where, what, n);
    try {
        Guard guard;
        RenderRecover();
    } catch (...) {}
}

void CoreInit() {
    g_board.numTmu = std::clamp(g_config.tmus, 1, 2);
    // Auto: a Voodoo2's 4 MB per TMU for Glide 2, a Voodoo3's 16 MB for Glide 3. Glide 3 games
    // sized for the later boards run out at 4 MB (NFS3's texture allocator returns null).
    const int mb = g_config.tmuMB > 0 ? g_config.tmuMB : g_version == kGlide3x ? 16 : 4;
    g_board.tmuMem = FxU32(std::clamp(mb, 1, 32)) << 20;
    for (int t = 0; t < kMaxTmu; t++) {
        if (t < g_board.numTmu) {
            if (g_texMem[t].bytes.size() != g_board.tmuMem) g_texMem[t].Resize(g_board.tmuMem);
        } else {
            g_texMem[t].Resize(0);
        }
    }
}

void CoreShutdown() {
    if (g_board.open) CoreWinClose();
}

FxBool CoreWinOpen(HWND hwnd, GrScreenResolution_t res, GrScreenRefresh_t refresh, GrColorFormat_t format,
                   GrOriginLocation_t origin, int numColorBuffers, int numAuxBuffers) {
    if (g_board.open) CoreWinClose();
    if (g_texMem[0].bytes.empty()) CoreInit();
    int w = 640, h = 480;
    if (res == GR_RESOLUTION_NONE) {
        // A window's own size (Voodoo Rush): its client area.
        RECT c;
        if (hwnd && GetClientRect(hwnd, &c) && c.right > 0 && c.bottom > 0) {
            w = std::min<int>(c.right, 2048);
            h = std::min<int>(c.bottom, 2048);
        }
    } else if (!ResolutionSize(res, &w, &h)) {
        GL_LOG("grSstWinOpen: resolution %ld unknown", static_cast<long>(res));
        return FXFALSE;
    }
    g_board.screenW = w;
    g_board.screenH = h;
    g_board.res = res;
    g_board.refresh = refresh;
    g_board.colorFormat = format;
    g_board.numColorBuffers = std::clamp(numColorBuffers, 2, 3);
    g_board.numAuxBuffers = std::clamp(numAuxBuffers, 0, 1);
    g_board.trisIn = g_board.trisOut = g_board.swaps = g_board.pointsDrawn = g_board.linesDrawn = 0;
    // guTexMemReset, as 3dfx's grSstWinOpen: the mipmaps, and which ones the NCC tables hold
    // (kept, a YIQ mipmap of the reopened board with an old one's id skipped its table).
    GuTexMemReset();
    st::InitState(origin == GR_ORIGIN_LOWER_LEFT ? GR_ORIGIN_LOWER_LEFT : GR_ORIGIN_UPPER_LEFT);
    GL_LOG("grSstWinOpen: %dx%d, %s origin, %d color / %d aux buffers, refresh %ld", w, h,
           origin == GR_ORIGIN_LOWER_LEFT ? "lower left" : "upper left", g_board.numColorBuffers, g_board.numAuxBuffers,
           static_cast<long>(refresh));
    try {
        if (!WindowOpen(hwnd)) return FXFALSE;
        if (!RenderOpen(g_board.hwnd)) {
            WindowClose();
            return FXFALSE;
        }
    } catch (...) {
        // Half open (memory run out): closed again, the game's next grSstWinOpen starting from
        // nothing, and the failure passed on (CallFailed: the call returns 0).
        RenderClose();
        WindowClose();
        throw;
    }
    RenderSetGamma(1.3f);  // Glide's default (the Voodoo's own gamma table)
    g_board.open = true;
    WindowOpened();
    return FXTRUE;
}

void CoreWinClose() {
    if (!g_board.open) return;
    GL_LOG("grSstWinClose (%lu frames)", static_cast<unsigned long>(g_board.swaps));
    LfbReset();
    RenderClose();
    WindowClose();
    g_board.open = false;
}

void CoreTriangle(const RVtx& a, const RVtx& b, const RVtx& c) {
    g_board.trisIn++;
    if (!g_board.open || !Visible(a, b, c)) return;
    g_board.trisOut++;
    const RVtx v[3] = {Snapped(a), Snapped(b), Snapped(c)};
    RenderTriangles(v, 3);
}

void CoreFan(const RVtx* v, int n) {
    if (n < 3 || !g_board.open) return;
    g_tris.clear();
    for (int i = 1; i + 1 < n; i++) AddTriangle(v[0], v[i], v[i + 1]);
    if (!g_tris.empty()) RenderTriangles(g_tris.data(), int(g_tris.size()));
}

// Independent triangles (v[0..2], v[3..5], ...), drawn together.
void CoreTriangleList(const RVtx* v, int n) {
    if (!g_board.open) return;
    g_tris.clear();
    for (int i = 0; i + 2 < n; i += 3) AddTriangle(v[i], v[i + 1], v[i + 2]);
    if (!g_tris.empty()) RenderTriangles(g_tris.data(), int(g_tris.size()));
}

// A strip, its triangles culled each with the strip's alternating orientation (odd: the first
// is the strip's odd one), drawn together.
void CoreStrip(const RVtx* v, int n, bool odd) {
    if (!g_board.open) return;
    g_tris.clear();
    for (int i = 0; i + 2 < n; i++, odd = !odd) {
        const RVtx& a = odd ? v[i + 1] : v[i];
        const RVtx& b = odd ? v[i] : v[i + 1];
        AddTriangle(a, b, v[i + 2]);
    }
    if (!g_tris.empty()) RenderTriangles(g_tris.data(), int(g_tris.size()));
}

// Glide's grDrawPoint: the pixel the point is in, filled (a square from its corner at the
// truncated coordinates).
void CorePoint(const RVtx& p) {
    if (!g_board.open) return;
    g_board.pointsDrawn++;
    const float x = std::floor(Unsnap(p.x)), y = std::floor(Unsnap(p.y));
    RVtx v[4] = {p, p, p, p};
    v[0].x = x, v[0].y = y;
    v[1].x = x + 1, v[1].y = y;
    v[2].x = x, v[2].y = y + 1;
    v[3].x = x + 1, v[3].y = y + 1;
    const RVtx tri[6] = {v[0], v[1], v[2], v[1], v[3], v[2]};
    RenderTriangles(tri, 6);
}

// Glide's grDrawLine: a parallelogram one pixel across, its ends offset half a pixel either
// way along the minor axis.
void CoreLine(const RVtx& a0, const RVtx& b0) {
    if (!g_board.open) return;
    const RVtx a = Snapped(a0), b = Snapped(b0);
    const float adx = std::fabs(b.x - a.x), ady = std::fabs(b.y - a.y);
    if (adx == 0 && ady == 0) return;
    g_board.linesDrawn++;
    RVtx v[4] = {a, a, b, b};
    if (adx >= ady) {  // x major
        v[0].y -= 0.5f, v[1].y += 0.5f, v[2].y -= 0.5f, v[3].y += 0.5f;
    } else {
        v[0].x -= 0.5f, v[1].x += 0.5f, v[2].x -= 0.5f, v[3].x += 0.5f;
    }
    const RVtx tri[6] = {v[0], v[1], v[2], v[1], v[3], v[2]};
    RenderTriangles(tri, 6);
}

void CoreAATriangle(const RVtx& a0, const RVtx& b0, const RVtx& c0, bool ab, bool bc, bool ca) {
    g_board.trisIn++;
    if (!g_board.open || !Visible(a0, b0, c0)) return;
    g_board.trisOut++;
    const RVtx v[3] = {Snapped(a0), Snapped(b0), Snapped(c0)};
    RenderTriangles(v, 3);
    g_tris.clear();
    if (ab) EdgeTris(v[0], v[1], v[2]);
    if (bc) EdgeTris(v[1], v[2], v[0]);
    if (ca) EdgeTris(v[2], v[0], v[1]);
    DrawFringe();
}

// grAADrawPolygon: the fan, and the edges on its outline.
void CoreAAFan(const RVtx* v, int n) {
    if (n < 3 || !g_board.open) return;
    for (int i = 1; i + 1 < n; i++) CoreAATriangle(v[0], v[i], v[i + 1], i == 1, true, i == n - 2);
}

// A square of four triangles around the point, its corners at alpha 0.
void CoreAAPoint(const RVtx& p0) {
    if (!g_board.open) return;
    g_board.pointsDrawn++;
    const RVtx p = Snapped(p0);
    RVtx c[4] = {p, p, p, p};
    static const float kCorner[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};
    for (int i = 0; i < 4; i++) c[i].x += kCorner[i][0], c[i].y += kCorner[i][1], c[i].a = 0.0f;
    g_tris.clear();
    for (int i = 0; i < 4; i++) {
        g_tris.push_back(c[i]);
        g_tris.push_back(c[(i + 1) & 3]);
        g_tris.push_back(p);
    }
    RenderTriangles(g_tris.data(), int(g_tris.size()));
    g_tris.clear();
}

// The line two pixels across, its sides at alpha 0 (grAADrawLine).
void CoreAALine(const RVtx& a0, const RVtx& b0) {
    if (!g_board.open) return;
    RVtx v1 = Snapped(a0), v2 = Snapped(b0);
    if (v2.y < v1.y) std::swap(v1, v2);
    const float adx = std::fabs(v2.x - v1.x), ady = std::fabs(v2.y - v1.y);
    if (adx == 0 && ady == 0) return;
    g_board.linesDrawn++;
    RVtx a = v1, b = v2, e = v1, f = v2;
    a.a = b.a = e.a = f.a = 0.0f;
    if (adx >= ady) a.y -= 1, b.y -= 1, e.y += 1, f.y += 1;
    else a.x += 1, b.x += 1, e.x -= 1, f.x -= 1;
    const RVtx tri[12] = {a, v2, b, a, v2, v1, v1, f, e, v1, v2, f};
    RenderTriangles(tri, 12);
}

void CoreClear(GrColor_t color, GrAlpha_t alpha, FxU32 depth) {
    if (!g_board.open) return;
    RenderClear(SwizzleColor(color), alpha, FxU16(depth));
}

void CoreSwap(int interval) {
    if (!g_board.open) return;
    LfbBeforeSwap();
    g_board.swaps++;
    // The waits (fpslimit=, the swap interval) without the lock: held, the window's
    // procedure (focus changes, repaints) and any other thread calling Glide waited too.
    // (The entry's one hold let go: the swap isn't called with the lock held otherwise.)
    auto unlocked = [](auto wait) {
        LeaveCriticalSection(&g_lock);
        wait();
        EnterCriticalSection(&g_lock);
    };
    const int paced = g_config.vsync == kVSyncAuto ? interval : 1;
    // Under the desktop compositor Present doesn't wait: the swap waits for its slot after
    // presenting, as many refreshes as Direct3D 9's vsync and LimitSwapInterval waited (at
    // least one, vsync=off aside: then not paced at all).
    const bool compositor = RenderPacedByCompositor();
    unlocked([paced, compositor] {
        LimitFrame();
        if (!compositor) LimitSwapInterval(paced);
    });
    if (!g_board.open) return;  // (closed by another thread meanwhile)
    const bool shown = RenderSwap(interval);
    // (Asked again: the swap may have made the device anew.)
    if (shown && g_board.open && RenderPacedByCompositor()) unlocked([paced] { PaceSwap(std::max(paced, 1)); });
    WindowPump();
    // Nothing shown (minimized by Alt+Tab, the device lost): the swap didn't wait for the
    // display, so a refresh's time is waited instead, without the lock (else the game's loop
    // took a whole core, all of it with cpus=1, and its window's thread waited for the lock).
    if (!shown && g_board.open) unlocked([] { Sleep(16); });
}

FxU32 CoreTexMaxAddress(GrChipID_t tmu) {
    (void)tmu;
    return g_board.tmuMem - 8;
}

void CoreTexDownloadLevel(GrChipID_t tmu, FxU32 startAddress, GrLOD_t thisLod, GrLOD_t largeLod, GrAspectRatio_t aspect,
                          GrTextureFormat_t format, FxU32 evenOdd, const void* data, int t, int maxT) {
    DownloadLevel(tmu, startAddress, thisLod, largeLod, aspect, format, evenOdd, data, t, maxT);
}

// Glide's grTexDownloadMipMap: every level from the largest, packed one after the other in
// the data, downloaded where its parity is in evenOdd.
void CoreTexDownloadMipMap(GrChipID_t tmu, FxU32 startAddress, FxU32 evenOdd, const GrTexInfo* info) {
    if (!info || !info->data) return;
    const FxU8* src = static_cast<const FxU8*>(info->data);
    const int bpp = info->format >= GR_TEXFMT_16BIT ? 2 : 1;
    for (int lod = info->largeLod; lod <= info->smallLod && lod <= GR_LOD_1; lod++) {
        int w, h;
        LevelSize(lod, info->aspectRatio, &w, &h);
        DownloadLevel(tmu, startAddress, lod, info->largeLod, info->aspectRatio, info->format, evenOdd, src, 0, h - 1);
        src += size_t(w) * h * bpp;
    }
}

void CoreTexDownloadTable(GrChipID_t tmu, GrTexTable_t type, const void* data, int start, int end) {
    if (!data) return;
    if (type == GR_TEXTABLE_PALETTE || type == GR_TEXTABLE_PALETTE_6666_EXT) {
        DownloadPalette(tmu, static_cast<const GuTexPalette*>(data)->data, start, end);
    } else if (type == GR_TEXTABLE_NCC0 || type == GR_TEXTABLE_NCC1) {
        DownloadNcc(tmu, int(type), static_cast<const GuNccTable*>(data));
    }
}

void CoreHwConfig(GrHwConfiguration* hw, GrSstType type) {
    if (!hw) return;
    memset(hw, 0, sizeof(*hw));
    hw->num_sst = 1;
    hw->SSTs[0].type = type;
    GrVoodooConfig_t& v = hw->SSTs[0].sstBoard.VoodooConfig;
    v.fbRam = std::clamp(g_config.fbMB, 1, 16);
    // A Voodoo 2 tells itself apart by its FBI revision (3dfx's gpci.c: + 0x100, or 0x80
    // with one TMU).
    v.fbiRev = type == GR_SSTTYPE_Voodoo2 ? 2 + (g_board.numTmu == 1 ? 0x80 : 0x100) : 2;
    v.nTexelfx = g_board.numTmu;
    v.sliDetect = FXFALSE;
    for (int t = 0; t < g_board.numTmu && t < 2; t++) {
        v.tmuConfig[t].tmuRev = type == GR_SSTTYPE_Voodoo2 ? 4 : 1;
        v.tmuConfig[t].tmuRam = int(g_board.tmuMem >> 20);
    }
}

}  // namespace gl2d9
