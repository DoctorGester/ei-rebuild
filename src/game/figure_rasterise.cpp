// 005A0E80 (thiscall on a CFigureMesh, RET 0xC): feeds the mesh's triangles to a rasteriser (the
// map object shadows, map_object_shadow.cpp). The texture coordinates (buffers_.texCoords) are
// scaled by 0073C370 to integers in a function-local static array (007C2E00, guard 007C2E10 bit
// 1, freed at exit by 005A1170; resized without keeping its contents); each triangle (three corner
// indices of buffers_.indices, corners of buffers_.corners: position index, -, texture index) gets
// its three projected points (from `points`, 8 bytes each) and texture coordinates into
// `triangle`, which goes to the pixel converter's slot 9 when its colour key is set, else slot 8.
// Returns 4 * the vertex packet count. See docs/notes/models.md.
#include "core_maths.h"
#include "figure_mesh.h"
#include "texture.h"   // CPixelConverter, RasterTriangle

#include "ei/hook.h"
#include "ei/x87.h"
#include "originals_library.h"
#include "originals_render.h"
#include "lib/containers.h"

#include <cstdint>
#include <cstring>

namespace {

using IntPoint = RasterTriangle::Point;
using PointArray = lib::AlignedArray<IntPoint>;   // its inlined SetSize allocates through 004F65B0
PointArray& TexturePoints() { return *reinterpret_cast<PointArray*>(0x007C2E00); }
std::uint8_t& Guard() { return *reinterpret_cast<std::uint8_t*>(0x007C2E10); }

float GameFloat(std::uintptr_t address) { return *reinterpret_cast<const volatile float*>(address); }

} // namespace

#pragma float_control(precise, on, push)

int __fastcall RasteriseFigureMesh(CFigureMesh* mesh, void*, CPixelConverter* rasteriser, const IntPoint* points,
                                   RasterTriangle* triangle) {
    const MeshBuffers& buffers = mesh->buffers_;
    PointArray& uv = TexturePoints();
    if (!(Guard() & 1)) {
        Guard() |= 1;
        uv = PointArray{};
        GameAtExit(reinterpret_cast<void(__cdecl*)()>(0x005A1170));
    }
    uv.SetSizeDiscard(buffers.texCoordCount);
    float scale = GameFloat(0x0073C370);
    for (int i = 0; i < buffers.texCoordCount; ++i) {
        const float* source = static_cast<const float*>(buffers.texCoords) + i * 2;
        uv.data[i].x = x87::fistp(StoreFloat(source[0] * scale));
        uv.data[i].y = x87::fistp(StoreFloat(source[1] * scale));
    }
    for (int t = 0; t < buffers.indexCount; t += 3) {
        for (int k = 0; k < 3; ++k) {
            std::uint16_t corner = static_cast<const std::uint16_t*>(buffers.indices)[t + k];
            const MeshCorner& record = static_cast<const MeshCorner*>(buffers.corners)[corner];
            std::memcpy(&triangle->corners[k].screen, &points[record.index[0]], 8);
            std::memcpy(&triangle->corners[k].texture, &TexturePoints().data[record.index[2]], 8);
        }
        if (triangle->colourKey) rasteriser->RasteriseKeyedTriangle(triangle);
        else rasteriser->RasteriseTriangle(triangle);
    }
    return buffers.vertexCount * 4;
}

#pragma float_control(pop)

EI_REPLACE(0x005A0E80, RasteriseFigureMesh);
