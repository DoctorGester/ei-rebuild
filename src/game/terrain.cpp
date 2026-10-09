#include "terrain.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <cmath>

namespace {
// x87 FCOM + "TEST AH,1" is true for "less than" and for unordered (NaN) operands.
bool LessOrUnordered(float v) { return !(v >= 0.0f); }
} // namespace

const float* Terrain::VertexAt(int ix, int iy, int layer) {
    if (ix < 0 || iy < 0) return nullptr;
    if (!(width >= static_cast<float>(ix)) || !(height >= static_cast<float>(iy))) return nullptr;

    // Vertex 32 of one sector is vertex 0 of the next; the last sector owns its far edge.
    int sx = ix / 32, sy = iy / 32;
    int lx = ix - sx * 32, ly = iy - sy * 32;
    if (lx == 0 && sx != 0) { --sx; lx = 32; }
    if (ly == 0 && sy != 0) { --sy; ly = 32; }

    TerrainSector* sector = SectorAt(sx, sy, 1, 1);
    if (!sector) return nullptr;
    int layerOffset = 0;
    if (layer) {
        if (!sector->hasWater) return nullptr;
        layerOffset = 33 * 33;
    }
    return sector->vertices + (lx + ly * 33 + layerOffset) * 3;
}

float __fastcall PlaneHeight(const float* v0, const float* v1, float x, float y, const float* v2) {
    float ax = v1[0] - v0[0], ay = v1[1] - v0[1];
    float bx = v2[0] - v0[0], by = v2[1] - v0[1];
    float det = by * ax - bx * ay;
    if (std::fabs(det) < 0.01f) return (v0[2] + v1[2] + v2[2]) * 0.33333334f;

    // Plane normal (nx, ny, det) through v0; solve for z at (x, y).
    float dz1 = v1[2] - v0[2], dz2 = v2[2] - v0[2];
    float nx = dz2 * ay - dz1 * by;
    float ny = dz1 * bx - dz2 * ax;
    float d = -(nx * v0[0] + ny * v0[1] + det * v0[2]);
    return -((d + nx * x + ny * y) / det);
}

float Terrain::HeightAt(float x, float y, int layer) {
    int ix0 = x87::fistp(x / 1.0f), iy0 = x87::fistp(y / 1.0f);
    int ix1 = ix0 + 1, iy1 = iy0 + 1;

    for (int iteration = 1;; ++iteration) {
        const float* v00 = VertexAt(ix0, iy0, layer);
        const float* v10 = VertexAt(ix1, iy0, layer);
        const float* v01 = VertexAt(ix0, iy1, layer);
        const float* v11 = VertexAt(ix1, iy1, layer);
        if (!v00 || !v11) return 0.0f;
        if (!v10) {
            if (v01) return PlaneHeight(v00, v11, x, y, v01);
            continue;   // the original loops here without counting toward the limit
        }
        if (!v01) return PlaneHeight(v00, v10, x, y, v11);

        // Both triangles of the cell must be counter-clockwise, else take the diagonal's midpoint.
        float s1 = v00[1] - v10[1], s2 = v11[0] - v10[0], s3 = v11[1] - v10[1], s4 = v00[0] - v10[0];
        if (!(s2 * s1 - s4 * s3 > 0.0f)) return (v11[2] + v01[2]) * 0.5f;
        float t1 = v11[1] - v01[1], t2 = v00[0] - v01[0], t3 = v00[1] - v01[1], t4 = v11[0] - v01[0];
        if (!(t2 * t1 - t4 * t3 > 0.0f)) return (v11[2] + v01[2]) * 0.5f;

        float u1 = v10[0] - x, u2 = v10[1] - y;
        float w1 = v01[0] - x, w2 = v01[1] - y;
        float side = (x - v00[0]) * (v11[1] - v00[1]) - (y - v00[1]) * (v11[0] - v00[0]);
        if (side > 0.0f) {
            // Lower-right triangle (v00, v10, v11); step to a neighbour if the point is outside it.
            if (iteration >= 10) return PlaneHeight(v00, v10, x, y, v11);
            if (s1 * u1 - s4 * u2 > 0.0f) { --iy0; --iy1; continue; }
            if (!LessOrUnordered(s3 * u1 - s2 * u2)) return PlaneHeight(v00, v10, x, y, v11);
            ++ix0; ++ix1;
        } else {
            // Upper-left triangle (v00, v11, v01).
            if (iteration >= 10) return PlaneHeight(v00, v11, x, y, v01);
            if (LessOrUnordered(t3 * w1 - t2 * w2)) { --ix0; --ix1; continue; }
            if (!(t1 * w1 - t4 * w2 > 0.0f)) return PlaneHeight(v00, v11, x, y, v01);
            ++iy0; ++iy1;
        }
    }
}

float TerrainOwner::SurfaceHeight(float x, float y) {
    float ground, depth;
    terrain_->GroundAndWaterDepth(x, y, &ground, &depth);
    return ground + depth;
}

EI_REPLACE_METHOD(0x00564070, TerrainOwner::SurfaceHeight);
EI_REPLACE_METHOD(0x006C8680, Terrain::HeightAt);
EI_REPLACE_METHOD(0x006C85A0, Terrain::VertexAt);
EI_REPLACE(0x006BE3A0, PlaneHeight);
