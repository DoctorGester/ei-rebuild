// The fake unit scene's vertex welding (0044C4A0, called by LoadFakeUnitScene after loading; Ghidra
// names it LinkNodes): for every mesh, positions closer than the weld distance (0073B860) to an
// earlier one are merged, the face corners and the node's morph key points are renumbered to the
// merged list, then the texture coordinates the same way. Names provisional; see
// docs/notes/objects.md ("Sixth round").
#include "scene_loader.h"

#include "../lib/crt.h"
#include "ei/hook.h"
#include "originals_library.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace scene {
namespace {

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

struct Point {
    float x, y, z;
};
static_assert(sizeof(Point) == 12);

// The aligned arrays' instantiations used here: points (Add 0044F2E0), texture coordinates (Add
// 0044FBB0) go through their original addresses (the Add copies take the element as separate
// arguments).
__declspec(naked) int __fastcall PointsAdd(PointBuffer*, void*, Point) EI_ORIGINAL(0x0044F2E0)
__declspec(naked) int __fastcall CoordsAdd(UvBuffer*, void*, float, float) EI_ORIGINAL(0x0044FBB0)

double WeldDistance() {
    double d;
    std::memcpy(&d, reinterpret_cast<const void*>(0x0073B860), sizeof(d));
    return d;
}

} // namespace

// 0044C4A0. `unit`: the fake unit (meshes: CArray at +0x544, 0x18C each); `nodes`: the scene's
// nodes, one per mesh (morph keys at +0xD0). A position or coordinate within the weld distance of
// an earlier kept one (or a NaN distance) maps to it, otherwise it is kept.
void __fastcall WeldMeshVertices(void* unit, VtArray* nodes) {
    PointBuffer points{};
    DwordBuffer pointMap{};
    UvBuffer coords{};
    DwordBuffer coordMap{};
    VtArray& meshes = At<VtArray>(unit, 0x544);
    if (meshes.size <= 0) return;
    const double weld = WeldDistance();
    for (int m = 0; m < meshes.size; ++m) {
        char* mesh = reinterpret_cast<char*>(meshes.data) + m * mesh::kSize;
        PointBuffer& positions = At<PointBuffer>(mesh, mesh::kPositions);

        points.SetSize(0);
        pointMap.SetSize(0);
        pointMap.SetSize(positions.size);
        for (int i = 0; i < positions.size; ++i) {
            const Point& p = reinterpret_cast<Point*>(positions.data)[i];
            int index = 0;
            for (; index < points.size; ++index) {
                const Point& q = reinterpret_cast<Point*>(points.data)[index];
                float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                double length = std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy +
                                          static_cast<double>(dz) * dz);
                if (!(length >= weld)) break;
            }
            if (index == points.size) {
                PointsAdd(&points, nullptr, p);
                index = points.size - 1;
            }
            pointMap.data[i] = static_cast<std::uint32_t>(index);
        }
        positions.SetSize(points.size);
        std::memcpy(positions.data, points.data, static_cast<std::size_t>(points.size) * sizeof(Point));

        // The node's morph keys (0x14 each, an aligned point array at +4) move to the merged list.
        char* node = reinterpret_cast<char*>(nodes->data) + m * node::kSize;
        VtArray& morph = At<VtArray>(node, node::kMorphKeys);
        for (int k = 0; k < morph.size; ++k) {
            PointBuffer& keyPoints = At<PointBuffer>(reinterpret_cast<char*>(morph.data) + k * 0x14, 4);
            int count = points.size;
            Point* moved = count ? reinterpret_cast<Point*>(PointBuffer::Allocate(count)) : nullptr;
            for (int i = 0; i < keyPoints.size; ++i) moved[pointMap.data[i]] = reinterpret_cast<Point*>(keyPoints.data)[i];
            keyPoints.SetSize(count);
            std::memcpy(keyPoints.data, moved, static_cast<std::size_t>(count) * sizeof(Point));
            if (moved) lib::FreeAligned32(moved);
        }

        UvBuffer& texCoords = At<UvBuffer>(mesh, mesh::kTexCoords);
        coords.SetSize(0);
        coordMap.SetSize(0);
        coordMap.SetSize(texCoords.size);
        for (int i = 0; i < texCoords.size; ++i) {
            const float* uv = reinterpret_cast<float*>(texCoords.data) + i * 2;
            int index = 0;
            for (; index < coords.size; ++index) {
                const float* known = reinterpret_cast<float*>(coords.data) + index * 2;
                float du = known[0] - uv[0], dv = known[1] - uv[1];
                if (!(Hypot(du, dv) >= weld)) break;
            }
            if (index == coords.size) {
                CoordsAdd(&coords, nullptr, uv[0], uv[1]);
                index = coords.size - 1;
            }
            coordMap.data[i] = static_cast<std::uint32_t>(index);
        }
        texCoords.SetSize(coords.size);
        std::memcpy(texCoords.data, coords.data, static_cast<std::size_t>(coords.size) * 8);

        FaceBuffer& faces = At<FaceBuffer>(mesh, mesh::kFaces);
        for (int f = 0; f < faces.size; ++f) {
            MeshFace& face = faces.data[f];
            for (auto& v : face.vertex) v = pointMap.data[v];
            for (auto& v : face.vertex2) v = coordMap.data[v];
        }
    }
    points.FreeData();
    coords.FreeData();
    coordMap.FreeData();
    pointMap.FreeData();
}
EI_REPLACE(0x0044C4A0, WeldMeshVertices);

} // namespace scene
