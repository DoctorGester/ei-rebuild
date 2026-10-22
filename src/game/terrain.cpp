#include "terrain.h"
#include "terrain_sector.h"   // SectorData
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"

#include <cmath>

const Vector3* Terrain::VertexAt(int ix, int iy, int layer) {
    if (ix < 0 || iy < 0) return nullptr;
    EI_NAN_AUDIT(width, height);
    if (width < static_cast<float>(ix) || height < static_cast<float>(iy)) return nullptr;

    // Vertex 32 of one sector is vertex 0 of the next; the last sector owns its far edge.
    int sx = ix / 32, sy = iy / 32;
    int lx = ix - sx * 32, ly = iy - sy * 32;
    if (lx == 0 && sx != 0) { --sx; lx = 32; }
    if (ly == 0 && sy != 0) { --sy; ly = 32; }

    terrain::SectorData* sector = SectorAt(sx, sy, 1, 1);
    if (!sector) return nullptr;
    int layerOffset = 0;
    if (layer) {
        if (!sector->hasWater) return nullptr;
        layerOffset = 33 * 33;
    }
    return &sector->vertices.data[lx + ly * 33 + layerOffset];
}

float __fastcall PlaneHeight(const Vector3* v0, const Vector3* v1, float x, float y, const Vector3* v2) {
    float ax = v1->x - v0->x, ay = v1->y - v0->y;
    float bx = v2->x - v0->x, by = v2->y - v0->y;
    float det = by * ax - bx * ay;
    if (std::fabs(det) < 0.01f) return (v0->z + v1->z + v2->z) * 0.33333334f;

    // Plane normal (nx, ny, det) through v0; solve for z at (x, y).
    float dz1 = v1->z - v0->z, dz2 = v2->z - v0->z;
    float nx = dz2 * ay - dz1 * by;
    float ny = dz1 * bx - dz2 * ax;
    float d = -DotUnrounded<Sum3::XYZ>(nx, ny, det, v0->x, v0->y, v0->z);
    return -(OffsetDot2DUnrounded(d, nx, ny, x, y) / det);
}

float Terrain::HeightAt(float x, float y, int layer) {
    int ix0 = x87::fistp(x / 1.0f), iy0 = x87::fistp(y / 1.0f);
    int ix1 = ix0 + 1, iy1 = iy0 + 1;

    for (int iteration = 1;; ++iteration) {
        const Vector3* v00 = VertexAt(ix0, iy0, layer);
        const Vector3* v10 = VertexAt(ix1, iy0, layer);
        const Vector3* v01 = VertexAt(ix0, iy1, layer);
        const Vector3* v11 = VertexAt(ix1, iy1, layer);
        if (!v00 || !v11) return 0.0f;
        if (!v10) {
            if (v01) return PlaneHeight(v00, v11, x, y, v01);
            continue;   // the original loops here without counting toward the limit
        }
        if (!v01) return PlaneHeight(v00, v10, x, y, v11);

        // Both triangles of the cell must be counter-clockwise, else take the diagonal's midpoint.
        float s1 = v00->y - v10->y, s2 = v11->x - v10->x, s3 = v11->y - v10->y, s4 = v00->x - v10->x;
        const float turnS = s2 * s1 - s4 * s3;
        EI_NAN_AUDIT(turnS);
        if (turnS <= 0.0f) return (v11->z + v01->z) * 0.5f;
        float t1 = v11->y - v01->y, t2 = v00->x - v01->x, t3 = v00->y - v01->y, t4 = v11->x - v01->x;
        const float turnT = t2 * t1 - t4 * t3;
        EI_NAN_AUDIT(turnT);
        if (turnT <= 0.0f) return (v11->z + v01->z) * 0.5f;

        float u1 = v10->x - x, u2 = v10->y - y;
        float w1 = v01->x - x, w2 = v01->y - y;
        float side = (x - v00->x) * (v11->y - v00->y) - (y - v00->y) * (v11->x - v00->x);
        if (side > 0.0f) {
            // Lower-right triangle (v00, v10, v11); step to a neighbour if the point is outside it.
            if (iteration >= 10) return PlaneHeight(v00, v10, x, y, v11);
            if (s1 * u1 - s4 * u2 > 0.0f) { --iy0; --iy1; continue; }
            if (s3 * u1 - s2 * u2 >= 0.0f) return PlaneHeight(v00, v10, x, y, v11);
            ++ix0; ++ix1;
        } else {
            // Upper-left triangle (v00, v11, v01).
            if (iteration >= 10) return PlaneHeight(v00, v11, x, y, v01);
            float across = t3 * w1 - t2 * w2;
            EI_NAN_AUDIT(across);
            if (across < 0.0f) { --ix0; --ix1; continue; }
            const float beyond = t1 * w1 - t4 * w2;
            EI_NAN_AUDIT(beyond);
            if (beyond <= 0.0f) return PlaneHeight(v00, v11, x, y, v01);
            ++iy0; ++iy1;
        }
    }
}

double CWorldClient::ClientRawSurfaceHeight(float x, float y) {
    float ground, depth;
    terrain_->GroundAndWaterDepth(x, y, &ground, &depth);
    return static_cast<float>(ground + depth);
}

EI_REWRITTEN(0x00564070, CWorldClient::ClientRawSurfaceHeight);
EI_REWRITTEN(0x006C8680, Terrain::HeightAt);
EI_REWRITTEN(0x006C85A0, Terrain::VertexAt);
EI_REWRITTEN(0x006BE3A0, PlaneHeight);
