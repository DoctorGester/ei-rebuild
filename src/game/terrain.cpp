// The landscape's height queries (terrain.h): vertices, heights, the surface and its texture class.
#include "terrain.h"
#include "terrain_sector.h"

#include "ei/x87.h"
#include "lib/nan_audit.h"

#include <cmath>

using terrain::kLastVertex;
using terrain::kSectorCells;

namespace {

// The height at (x, y) of the plane through the three vertices; the average of their heights when the
// triangle is degenerate.
float PlaneHeight(const Vector3& v0, const Vector3& v1, float x, float y, const Vector3& v2) {
    float ax = v1.x - v0.x, ay = v1.y - v0.y;
    float bx = v2.x - v0.x, by = v2.y - v0.y;
    float det = by * ax - bx * ay;
    if (std::fabs(det) < 0.01f) return (v0.z + v1.z + v2.z) * 0.33333334f;

    // Plane normal (nx, ny, det) through v0; solve for z at (x, y).
    float dz1 = v1.z - v0.z, dz2 = v2.z - v0.z;
    float nx = dz2 * ay - dz1 * by;
    float ny = dz1 * bx - dz2 * ax;
    float d = -DotUnrounded<Sum3::XYZ>(nx, ny, det, v0.x, v0.y, v0.z);
    return -(OffsetDot2DUnrounded(d, nx, ny, x, y) / det);
}

// The texture class outside the map.
constexpr int kOutsideClass = 8;

} // namespace

const Vector3* Terrain::VertexAt(int ix, int iy, int layer) {
    if (ix < 0 || iy < 0) return nullptr;
    EI_NAN_AUDIT(width, height);
    if (width < static_cast<float>(ix) || height < static_cast<float>(iy)) return nullptr;

    // Vertex 32 of one sector is vertex 0 of the next; the last sector owns its far edge.
    int sx = ix / kSectorCells, sy = iy / kSectorCells;
    int lx = ix - sx * kSectorCells, ly = iy - sy * kSectorCells;
    if (lx == 0 && sx != 0) { --sx; lx = kLastVertex; }
    if (ly == 0 && sy != 0) { --sy; ly = kLastVertex; }

    terrain::SectorData* sector = SectorAt(sx, sy, SectorStage::Loaded, true);
    if (!sector) return nullptr;
    int layerOffset = 0;
    if (layer) {
        if (!sector->hasWater) return nullptr;
        layerOffset = terrain::kSectorVertices;
    }
    return &sector->vertices[lx + ly * terrain::kSectorRow + layerOffset];
}

float Terrain::HeightAt(float x, float y, int layer) {
    int ix0 = x87::fistp(x), iy0 = x87::fistp(y);
    int ix1 = ix0 + 1, iy1 = iy0 + 1;

    for (int iteration = 1;; ++iteration) {
        const Vector3* v00 = VertexAt(ix0, iy0, layer);
        const Vector3* v10 = VertexAt(ix1, iy0, layer);
        const Vector3* v01 = VertexAt(ix0, iy1, layer);
        const Vector3* v11 = VertexAt(ix1, iy1, layer);
        if (!v00 || !v11) return 0.0f;
        if (!v10) {
            if (v01) return PlaneHeight(*v00, *v11, x, y, *v01);
            continue;   // the same cell again: the iteration limit below is not reached, so this never ends
        }
        if (!v01) return PlaneHeight(*v00, *v10, x, y, *v11);

        // Both triangles of the cell must be counter-clockwise, else take the diagonal's midpoint.
        const Vector2 from10to00 = {v00->x - v10->x, v00->y - v10->y};
        const Vector2 from10to11 = {v11->x - v10->x, v11->y - v10->y};
        const float turnS = from10to11.x * from10to00.y - from10to00.x * from10to11.y;
        EI_NAN_AUDIT(turnS);
        if (turnS <= 0.0f) return (v11->z + v01->z) * 0.5f;
        const Vector2 from01to00 = {v00->x - v01->x, v00->y - v01->y};
        const Vector2 from01to11 = {v11->x - v01->x, v11->y - v01->y};
        const float turnT = from01to00.x * from01to11.y - from01to11.x * from01to00.y;
        EI_NAN_AUDIT(turnT);
        if (turnT <= 0.0f) return (v11->z + v01->z) * 0.5f;

        const Vector2 to10 = {v10->x - x, v10->y - y};
        const Vector2 to01 = {v01->x - x, v01->y - y};
        float side = (x - v00->x) * (v11->y - v00->y) - (y - v00->y) * (v11->x - v00->x);
        if (side > 0.0f) {
            // Lower-right triangle (v00, v10, v11); step to a neighbour if the point is outside it.
            if (iteration >= 10) return PlaneHeight(*v00, *v10, x, y, *v11);
            if (from10to00.y * to10.x - from10to00.x * to10.y > 0.0f) { --iy0; --iy1; continue; }
            if (from10to11.y * to10.x - from10to11.x * to10.y >= 0.0f) return PlaneHeight(*v00, *v10, x, y, *v11);
            ++ix0; ++ix1;
        } else {
            // Upper-left triangle (v00, v11, v01).
            if (iteration >= 10) return PlaneHeight(*v00, *v11, x, y, *v01);
            float across = from01to00.y * to01.x - from01to00.x * to01.y;
            EI_NAN_AUDIT(across);
            if (across < 0.0f) { --ix0; --ix1; continue; }
            const float beyond = from01to11.y * to01.x - from01to11.x * to01.y;
            EI_NAN_AUDIT(beyond);
            if (beyond <= 0.0f) return PlaneHeight(*v00, *v11, x, y, *v01);
            ++iy0; ++iy1;
        }
    }
}

float Terrain::SurfaceHeight(float x, float y) {
    float water = HeightAt(x, y, 1);
    double ground = HeightAt(x, y, 0);
    float depth = water > ground ? static_cast<float>(water - ground) : 0.0f;
    return static_cast<float>(static_cast<float>(ground) + depth);
}

int Terrain::SurfaceClass(float x, float y) {
    int ix = x87::fistp(x);
    int iy = x87::fistp(y);
    if (ix < 0 || iy < 0 || ix >= sectorsX * kSectorCells || iy >= sectorsY * kSectorCells) return kOutsideClass;
    terrain::SectorData* sector = SectorAt(ix / kSectorCells, iy / kSectorCells, SectorStage::Built, true);
    int tile = (iy % kSectorCells) / 2 * terrain::kSectorTiles + (ix % kSectorCells) / 2;
    if (!sector) return kOutsideClass;
    if (sector->hasWater && sector->waterTiles[tile] != -1) {
        float water = HeightAt(x, y, 1);
        const double ground = HeightAt(x, y, 0);
        EI_NAN_AUDIT(ground, water);
        if (ground < water) return textureClass[sector->tileTextures[tile + terrain::kLayerTiles]];
    }
    return textureClass[sector->tileTextures[tile]];
}
