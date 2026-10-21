// 006CCF20: the terrain part of a ground decal (the shadow projector 0056C750). The decal quad is
// slid along the builder's slope until its lowest point matches the lowest ground vertex under
// its bounding cells; the ground cells of that rectangle then go to the builder as two triangles
// each.
#include "terrain.h"
#include "ground_decal.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/vecmath.h"
#include "game_data.h"

namespace {

constexpr float kMargin = 1.0f;   // 0073B84C: one cell around the quad

float CellSize() { return 1.0f /*007441D0*/; }

// The original's select idioms (FCOMP; C0 picks): the lower of `a` and `b` (a when unordered), and
// `a` raised to `bound` (bound when unordered).
float Lower(float a, float b) { return a >= b ? b : a; }
float Raised(float a, float bound) { return a >= bound ? a : bound; }

// A cell bound: FISTP of (v -/+ the margin) / cell size, stored as a float first.
int CellBound(double v) { return x87::fistp(static_cast<float>(v / CellSize())); }

Vector3 Corner(const float* m, int column) { return {m[column], m[4 + column], m[8 + column]}; }

} // namespace

void Terrain::AddDecalTriangles(DecalBuilder* builder, float* const* corners) {
    const float* m = *corners;
    Vector3 origin = Corner(m, 3);
    Vector3 u = Corner(m, 2);
    Vector3 v = Corner(m, 0);
    Vector3 slope = builder->slope_;
    float lowest = origin.z;
    float previous = origin.z;
    int x0 = 0x7FFFFFFF, y0 = 0x7FFFFFFF, x1 = 0, y1 = 0;
    for (;;) {
        float shift = static_cast<float>(previous - lowest);
        v = slope * shift + v;
        origin = slope * shift + origin;
        u = slope * shift + u;
        // The fourth corner's x and y (u + v - origin).
        float fourthX = static_cast<float>(u.x + v.x - origin.x);
        float fourthY = static_cast<float>(u.y + v.y - origin.y);
        int bound = CellBound(Lower(Lower(Lower(fourthX, u.x), v.x), origin.x) - kMargin);
        if (x0 >= bound) x0 = bound;
        bound = CellBound(Lower(Lower(Lower(fourthY, u.y), v.y), origin.y) - kMargin);
        if (y0 >= bound) y0 = bound;
        bound = CellBound(Raised(origin.x, Raised(v.x, Raised(u.x, fourthX))) + kMargin);
        if (x1 <= bound) x1 = bound;
        bound = CellBound(Raised(origin.y, Raised(v.y, Raised(u.y, fourthY))) + kMargin);
        if (y1 <= bound) y1 = bound;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        int last = static_cast<int>(gamecrt::Ftol(width - 1.0f));
        if (last < x1) x1 = last;
        last = static_cast<int>(gamecrt::Ftol(height - 1.0f));
        if (last < y1) y1 = last;
        previous = lowest;
        if (y0 > y1) break;
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) lowest = Lower(VertexAt(x, y, 0)[2], lowest);
        }
        if (!(lowest < previous)) break;   // it no longer drops (Lower never raises it); a NaN ends it too
    }
    builder->Reserve(((x1 - x0 + 1) * (y1 - y0 + 1) + (y1 - y0 + x1 - x0 + 1) * 5) * 4);
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            auto* a = reinterpret_cast<const Vector3*>(VertexAt(x, y, 0));
            auto* b = reinterpret_cast<const Vector3*>(VertexAt(x, y + 1, 0));
            auto* c = reinterpret_cast<const Vector3*>(VertexAt(x + 1, y, 0));
            auto* d = reinterpret_cast<const Vector3*>(VertexAt(x + 1, y + 1, 0));
            builder->AddTerrainTriangle(a, c, d);
            builder->AddTerrainTriangle(a, d, b);
        }
    }
}

EI_REPLACE_METHOD(0x006CCF20, Terrain::AddDecalTriangles);
