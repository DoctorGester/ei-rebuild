// Feeds a CFigureMesh's triangles to a rasteriser (the map object shadows, map_object_shadow.cpp). The
// texture coordinates (buffers_.texCoords) are scaled to integers in a function-local static array (freed at
// exit; resized without keeping its contents); each triangle (three corner indices of buffers_.indices,
// corners of buffers_.corners: position index, -, texture index) gets its three projected points (from
// `points`) and texture coordinates into `target`'s triangle, which goes to the pixel converter's keyed draw
// when its colour key is set, else the plain one. Returns 4 * the vertex packet count. See
// docs/notes/models.md.
#include "core_maths.h"
#include "figure_mesh.h"
#include "texture.h"   // CPixelConverter, RasterTriangle

#include "ei/x87.h"
#include "lib/containers.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace {

constinit std::vector<RasterTriangle::Point> g_texturePoints = /*007C2E00*/ {};
void FreeTexturePoints() { lib::FreeStorage(g_texturePoints); }
std::uint8_t g_texturePointsGuard = /*007C2E10*/ {};


} // namespace

int CFigureMesh::Rasterise(CPixelConverter* rasteriser, const RasterTriangle::Point* points, RasterTarget* target) {
    const MeshBuffers& buffers = buffers_;
    std::vector<RasterTriangle::Point>& uv = g_texturePoints;
    if (!(g_texturePointsGuard & 1)) {
        g_texturePointsGuard |= 1;
        uv = std::vector<RasterTriangle::Point>{};
        std::atexit(FreeTexturePoints);
    }
    uv.resize(buffers.texCoordCount);
    float scale = 255.0f;
    for (int i = 0; i < buffers.texCoordCount; ++i) {
        const float* source = &buffers.texCoords[i].u;
        uv[i].x = x87::fistp(static_cast<float>(source[0] * scale));
        uv[i].y = x87::fistp(static_cast<float>(source[1] * scale));
    }
    for (int t = 0; t < buffers.indexCount; t += 3) {
        for (int k = 0; k < 3; ++k) {
            std::uint16_t corner = buffers.indices[t + k];
            const MeshCorner& record = buffers.corners[corner];
            std::memcpy(&target->corners[k].screen, &points[record.position], sizeof(RasterTriangle::Point));
            std::memcpy(&target->corners[k].texture, &g_texturePoints[record.texCoord], sizeof(RasterTriangle::Point));
        }
        rasteriser->RasteriseTriangle(target);
    }
    return buffers.vertexCount * 4;
}
