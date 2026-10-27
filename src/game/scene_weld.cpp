// The fake unit scene's vertex welding (LinkNodes, called by LoadFakeUnitScene after loading): for every
// mesh, positions closer than the weld distance to an earlier one are merged, the face corners and the
// node's morph key points are renumbered to the merged list, then the texture coordinates the same way.
// Names provisional; see docs/notes/objects.md ("Rewritten").
#include "scene_loader.h"

#include "../lib/crt.h"
#include "lib/vecmath.h"
#include "ei/hook.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace scene {
namespace {

// The aligned arrays' instantiations used here: points, texture coordinates; the element is passed as its
// bits.

double WeldDistance() { return 0.00001 /*0073B860*/; }

} // namespace

// `unit`: the fake unit (meshes: CArray, 0x18C each); `nodes`: the scene's
// nodes, one per mesh (morph keys). A position or coordinate within the weld distance of
// an earlier kept one (or a NaN distance) maps to it, otherwise it is kept.
void __fastcall WeldMeshVertices(FakeUnitScene* unit, NodeArray* nodes) {
    PointBuffer points{};
    DwordBuffer pointMap{};
    UvBuffer coords{};
    DwordBuffer coordMap{};
    MeshArray& meshes = unit->meshes;
    if (meshes.size <= 0) return;
    const double weld = WeldDistance();
    for (int m = 0; m < meshes.size; ++m) {
        SceneMesh* mesh = &meshes.data[m];
        PointBuffer& positions = mesh->positions;

        points.SetSize(0);
        pointMap.SetSize(0);
        pointMap.SetSize(positions.size);
        for (int i = 0; i < positions.size; ++i) {
            const Vector3& p = positions.data[i].point;
            int index = 0;
            for (; index < points.size; ++index) {
                const Vector3& q = points.data[index].point;
                float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                double length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(dx, dy, dz));
                EI_NAN_AUDIT(length);
                if (length < weld) break;
            }
            if (index == points.size) {
                points.Add(std::bit_cast<PointBits>(p));
                index = points.size - 1;
            }
            pointMap.data[i] = static_cast<std::uint32_t>(index);
        }
        positions.SetSize(points.size);
        std::memcpy(positions.data, points.data, static_cast<std::size_t>(points.size) * sizeof(Vector3));

        // The node's morph keys (0x14 each, an aligned point array at +4) move to the merged list.
        SceneNode* node = &nodes->data[m];
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
            const float* uv = texCoords.data[i].uv;
            int index = 0;
            for (; index < coords.size; ++index) {
                const float* known = coords.data[index].uv;
                float du = known[0] - uv[0], dv = known[1] - uv[1];
                const double distance = libcrt::Hypot(du, dv);
                EI_NAN_AUDIT(distance);
                if (distance < weld) break;
            }
            if (index == coords.size) {
                coords.Add(UvBits{std::bit_cast<std::uint32_t>(uv[0]), std::bit_cast<std::uint32_t>(uv[1])});
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
EI_REWRITTEN(0x0044C4A0, WeldMeshVertices);

} // namespace scene
