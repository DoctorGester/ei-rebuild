// The fake unit scene's vertex welding (LinkNodes, called by LoadFakeUnitScene after loading): for every
// mesh, positions closer than the weld distance to an earlier one are merged, the face corners and the
// node's morph key points are renumbered to the merged list, then the texture coordinates the same way.
// Names provisional; see docs/notes/objects.md ("Rewritten").
#include "scene_loader.h"

#include "../lib/crt.h"
#include "lib/vecmath.h"
#include "game_data.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace scene {
namespace {

// The aligned arrays' instantiations used here: points, texture coordinates; the element is passed as its
// bits.

double WeldDistance() { return 0.00001 /*0073B860*/; }

} // namespace

// `unit`: the fake unit (meshes: a CArray); `nodes`: the scene's
// nodes, one per mesh (morph keys). A position or coordinate within the weld distance of
// an earlier kept one (or a NaN distance) maps to it, otherwise it is kept.
void WeldMeshVertices(FakeUnitScene* unit, NodeArray* nodes) {
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

        points.clear();
        pointMap.clear();
        pointMap.resize(std::ssize(positions));
        for (int i = 0; i < std::ssize(positions); ++i) {
            const Vector3& p = positions[i].point;
            int index = 0;
            for (; index < std::ssize(points); ++index) {
                const Vector3& q = points[index].point;
                float dx = q.x - p.x, dy = q.y - p.y, dz = q.z - p.z;
                double length = std::sqrt(LengthSquaredUnrounded<Sum3::XYZ, double>(dx, dy, dz));
                EI_NAN_AUDIT(length);
                if (length < weld) break;
            }
            if (index == std::ssize(points)) {
                points.push_back(std::bit_cast<PointBits>(p));
                index = std::ssize(points) - 1;
            }
            pointMap[i] = static_cast<std::uint32_t>(index);
        }
        positions.resize(std::ssize(points));
        std::memcpy(positions.data(), points.data(), static_cast<std::size_t>(std::ssize(points)) * sizeof(Vector3));

        // The node's morph keys (0x14 each, an aligned point array at +4) move to the merged list.
        SceneNode* node = &nodes->data[m];
        auto& morph = node->morphKeys;
        for (int k = 0; k < morph.size; ++k) {
            std::vector<Vector3>& keyPoints = morph.data[k].points;
            int count = std::ssize(points);
            std::vector<Vector3> moved(static_cast<std::size_t>(count));
            for (int i = 0; i < std::ssize(keyPoints); ++i) moved[pointMap[i]] = keyPoints[i];
            keyPoints = std::move(moved);
        }

        UvBuffer& texCoords = mesh->texCoords;
        coords.clear();
        coordMap.clear();
        coordMap.resize(std::ssize(texCoords));
        for (int i = 0; i < std::ssize(texCoords); ++i) {
            const float* uv = texCoords[i].uv;
            int index = 0;
            for (; index < std::ssize(coords); ++index) {
                const float* known = coords[index].uv;
                float du = known[0] - uv[0], dv = known[1] - uv[1];
                const double distance = libcrt::Hypot(du, dv);
                EI_NAN_AUDIT(distance);
                if (distance < weld) break;
            }
            if (index == std::ssize(coords)) {
                coords.push_back(UvBits{std::bit_cast<std::uint32_t>(uv[0]), std::bit_cast<std::uint32_t>(uv[1])});
                index = std::ssize(coords) - 1;
            }
            coordMap[i] = static_cast<std::uint32_t>(index);
        }
        texCoords.resize(std::ssize(coords));
        std::memcpy(texCoords.data(), coords.data(), static_cast<std::size_t>(std::ssize(coords)) * 8);

        FaceBuffer& faces = mesh->faces;
        for (int f = 0; f < std::ssize(faces); ++f) {
            MeshFace& face = faces[f];
            for (auto& v : face.vertex) v = pointMap[v];
            for (auto& v : face.vertex2) v = coordMap[v];
        }
    }
    lib::FreeStorage(points);
    lib::FreeStorage(coords);
    lib::FreeStorage(coordMap);
    lib::FreeStorage(pointMap);
}

} // namespace scene
