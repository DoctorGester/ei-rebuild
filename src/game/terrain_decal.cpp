// The terrain part of a ground decal (the shadow projector). The decal quad is
// slid along the builder's slope until its lowest point matches the lowest ground vertex under
// its bounding cells; the ground cells of that rectangle then go to the builder as two triangles
// each.
#include "terrain.h"
#include "figure_mesh.h"   // VertexPacket
#include "ground_decal.h"

#include "lib/crt.h"
#include "lib/vecmath.h"
#include "lib/float_select.h"

#include <climits>
#include <cmath>

namespace terrain {
namespace {

constexpr float kMargin = 1.0f;   // one cell around the quad

// A cell bound: (v -/+ the margin) / cell size, stored as a float first and rounded to nearest.
int CellBound(double v) { return std::lround(v); }

Vector3 Corner(const VertexPacket& quad, int corner) { return {quad.x()[corner], quad.y()[corner], quad.z()[corner]}; }

} // namespace

void Terrain::AddDecalTriangles(DecalBuilder* builder, const VertexPacket& quad) {
    Vector3 origin = Corner(quad, 3);
    Vector3 u = Corner(quad, 2);
    Vector3 v = Corner(quad, 0);
    Vector3 slope = builder->slope;
    float lowest = origin.z;
    float previous = origin.z;
    int x0 = INT_MAX, y0 = INT_MAX, x1 = 0, y1 = 0;
    for (;;) {
        float shift = previous - lowest;
        v = slope * shift + v;
        origin = slope * shift + origin;
        u = slope * shift + u;
        // The fourth corner's x and y (u + v - origin).
        float fourthX = u.x + v.x - origin.x;
        float fourthY = u.y + v.y - origin.y;
        int bound = CellBound(MinKeepA(MinKeepA(MinKeepA(fourthX, u.x), v.x), origin.x) - kMargin);
        if (x0 >= bound) x0 = bound;
        bound = CellBound(MinKeepA(MinKeepA(MinKeepA(fourthY, u.y), v.y), origin.y) - kMargin);
        if (y0 >= bound) y0 = bound;
        bound = CellBound(MaxKeepB(origin.x, MaxKeepB(v.x, MaxKeepB(u.x, fourthX))) + kMargin);
        if (x1 <= bound) x1 = bound;
        bound = CellBound(MaxKeepB(origin.y, MaxKeepB(v.y, MaxKeepB(u.y, fourthY))) + kMargin);
        if (y1 <= bound) y1 = bound;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        int last = static_cast<int>(libcrt::Ftol(width - 1.0f));
        if (last < x1) x1 = last;
        last = static_cast<int>(libcrt::Ftol(height - 1.0f));
        if (last < y1) y1 = last;
        previous = lowest;
        if (y0 > y1) break;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) lowest = MinKeepA(VertexAt(x, y, Layer::Ground)->z, lowest);
        }
        if (!(lowest < previous)) break;   // it no longer drops (Lower never raises it); a NaN ends it too
    }
    builder->Reserve(((x1 - x0 + 1) * (y1 - y0 + 1) + (y1 - y0 + x1 - x0 + 1) * 5) * 4);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const Vector3* a = VertexAt(x, y, Layer::Ground);
            const Vector3* b = VertexAt(x, y + 1, Layer::Ground);
            const Vector3* c = VertexAt(x + 1, y, Layer::Ground);
            const Vector3* d = VertexAt(x + 1, y + 1, Layer::Ground);
            builder->AddTerrainTriangle(*a, *c, *d);
            builder->AddTerrainTriangle(*a, *d, *b);
        }
    }
}

} // namespace terrain
