// The fake unit scene's vertex welding (0044C4A0, called by LoadFakeUnitScene after loading; Ghidra
// names it LinkNodes): for every mesh, positions closer than the weld distance (0073B860) to an
// earlier one are merged, the face corners and the node's morph key points are renumbered to the
// merged list, then the texture coordinates the same way. Names provisional; see
// docs/notes/objects.md ("Sixth round").
#include "scene_loader.h"

#include "../lib/crt.h"
#include "lib/vecmath.h"
#include "ei/hook.h"
#include "originals_library.h"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace scene {
namespace {

// The aligned arrays' instantiations used here: points (Add 0044F2E0), texture coordinates (Add
// 0044FBB0) go through their original addresses (the Add copies take the element as separate
// arguments).
__declspec(naked) int __fastcall PointsAdd(PointBuffer*, void*, Vector3) EI_ORIGINAL(0x0044F2E0)
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
void __fastcall WeldMeshVertices(FakeUnitScene* unit, VtArray* nodes) {
    PointBuffer points{};
    DwordBuffer pointMap{};
    UvBuffer coords{};
    DwordBuffer coordMap{};
    VtArray& meshes = unit->meshes;
    if (meshes.size <= 0) return;
    const double weld = WeldDistance();
    for (int m = 0; m < meshes.size; ++m) {
        SceneMesh* mesh = &Typed<MeshArray>(meshes).data[m];
        PointBuffer& positions = mesh->positions;

        points.SetSize(0);
        pointMap.SetSize(0);
        pointMap.SetSize(positions.size);
        for (int i = 0; i < positions.size; ++i) {
            const Vector3& p = reinterpret_cast<Vector3*>(positions.data)[i];
            int index = 0;
            for (; index < points.size; ++index) {
                const Vector3& q = reinterpret_cast<Vector3*>(points.data)[index];
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
        std::memcpy(positions.data, points.data, static_cast<std::size_t>(points.size) * sizeof(Vector3));

        // The node's morph keys (0x14 each, an aligned point array at +4) move to the merged list.
        SceneNode* node = &Typed<NodeArray>(*nodes).data[m];
        auto& morph = node->morphKeys;
        for (int k = 0; k < morph.size; ++k) {
            lib::AlignedArray<Vector3>& keyPoints = morph.data[k].points;
            int count = points.size;
            Vector3* moved = count ? lib::AlignedArray<Vector3>::Allocate(count) : nullptr;
            for (int i = 0; i < keyPoints.size; ++i) moved[pointMap.data[i]] = keyPoints.data[i];
            keyPoints.SetSize(count);
            std::memcpy(keyPoints.data, moved, static_cast<std::size_t>(count) * sizeof(Vector3));
            if (moved) lib::FreeAligned32(moved);
        }

        UvBuffer& texCoords = mesh->texCoords;
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

        FaceBuffer& faces = mesh->faces;
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
