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

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/containers.h"
#include "game_data.h"

#include <cstdint>
#include <cstring>

namespace {

using IntPoint = RasterTriangle::Point;
using PointArray = lib::AlignedArray<IntPoint>;
constinit lib::AlignedArray<RasterTriangle::Point> g_texturePoints = /*007C2E00*/ {};
lib::AlignedArray<RasterTriangle::Point>& TexturePoints() { return g_texturePoints; }
void __cdecl FreeTexturePoints() { g_texturePoints.FreeData(); }
std::uint8_t g_texturePointsGuard = /*007C2E10*/ {};


} // namespace

int CFigureMesh::Rasterise(CPixelConverter* rasteriser, const IntPoint* points, RasterTarget* target) {
    const MeshBuffers& buffers = buffers_;
    PointArray& uv = TexturePoints();
    if (!(g_texturePointsGuard & 1)) {
        g_texturePointsGuard |= 1;
        uv = PointArray{};
        libcrt::AtExit(FreeTexturePoints);
    }
    uv.SetSizeDiscard(buffers.texCoordCount);
    float scale = 255.0f /*0073C370*/;
    for (int i = 0; i < buffers.texCoordCount; ++i) {
        const float* source = &buffers.texCoords[i].u;
        uv.data[i].x = x87::fistp(static_cast<float>(source[0] * scale));
        uv.data[i].y = x87::fistp(static_cast<float>(source[1] * scale));
    }
    for (int t = 0; t < buffers.indexCount; t += 3) {
        for (int k = 0; k < 3; ++k) {
            std::uint16_t corner = buffers.indices[t + k];
            const MeshCorner& record = buffers.corners[corner];
            std::memcpy(&target->corners[k].screen, &points[record.position], sizeof(IntPoint));
            std::memcpy(&target->corners[k].texture, &TexturePoints().data[record.texCoord], sizeof(IntPoint));
        }
        if (target->keyTexels) rasteriser->RasteriseKeyedTriangle(target);
        else rasteriser->RasteriseTriangle(target);
    }
    return buffers.vertexCount * 4;
}

EI_REWRITTEN(0x005A0E80, CFigureMesh::Rasterise);
EI_REWRITTEN(0x005A1170, FreeTexturePoints);
