// What core.cpp shares with aa.cpp (the antialiased primitives): vertices brought back to the
// screen, the triangle setup's culling, and the triangles of one call.
#pragma once
#include <cmath>

#include "core.h"

namespace gl2d9 {

// Games snapping their vertices to 1/16 pixel add (3 << 18) and take it off again; one that
// leaves it on (Glide's own debug output allows for it) is brought back to the screen.
inline float Unsnap(float v) { return std::fabs(v) >= 262144.0f ? v - 786432.0f : v; }

inline RVtx Snapped(const RVtx& v) {
    RVtx o = v;
    o.x = Unsnap(v.x);
    o.y = Unsnap(v.y);
    return o;
}

bool VisibleSnapped(const RVtx& a, const RVtx& b, const RVtx& c);

extern std::vector<RVtx> g_tris;  // triangles of one call, drawn together

}  // namespace gl2d9
