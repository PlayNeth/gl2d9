// ---------------------------------------------------------------------------------------------
// The antialiasing in this file is derived from 3dfx Interactive's Glide source code (Glide 2's
// gaa.c), copyright 3dfx Interactive, Inc. 1999, and is distributed under the 3DFX GLIDE Source
// Code General Public License (LICENSE-3DFX); recipients who wish to copy, distribute or modify
// it can only do so under that license. It is provided without warranty (section 5 of that
// license). Changed by PlayNeth, 1 November 2026: translated to C++ for gl2d9, to run on
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
// Glide's antialiased primitives (grAADraw*, see core.h).
#include <algorithm>

#include "coreint.h"

namespace gl2d9 {

namespace {

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

void CoreAATriangle(const RVtx& a0, const RVtx& b0, const RVtx& c0, bool ab, bool bc, bool ca) {
    g_board.trisIn++;
    if (!g_board.open) return;
    const RVtx v[3] = {Snapped(a0), Snapped(b0), Snapped(c0)};
    if (!VisibleSnapped(v[0], v[1], v[2])) return;
    g_board.trisOut++;
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

}  // namespace gl2d9
