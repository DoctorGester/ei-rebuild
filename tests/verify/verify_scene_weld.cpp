// Checks for scene_weld.cpp: ours only (it rewrites the meshes and morph keys in place). Before the
// call every face corner's position and texture coordinate is recorded; afterwards each corner must
// index inside the merged lists and land within the weld distance of what it had (the merged entry
// is the first one within that distance). Distances use plain float maths (rule 7): a point on the
// weld distance itself may merge differently from the original.
#include "verify.h"

#include "../../src/game/scene_loader.h"

#include <cmath>
#include <cstring>
#include <vector>

namespace scene {
void __fastcall WeldMeshVertices(void* unit, VtArray* nodes);
}

namespace {
void* s_unused;
using namespace scene;

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

struct Corner {
    float p[3];
    float uv[2];
};

void __fastcall WeldVerify(void* unit, VtArray* nodes) {
    VtArray& meshes = At<VtArray>(unit, 0x544);
    std::vector<std::vector<Corner>> before(meshes.size > 0 ? meshes.size : 0);
    for (int m = 0; m < meshes.size; ++m) {
        char* mesh = reinterpret_cast<char*>(meshes.data) + m * mesh::kSize;
        auto& positions = At<lib::AlignedArray<std::uint8_t>>(mesh, mesh::kPositions);
        auto& coords = At<lib::AlignedArray<std::uint8_t>>(mesh, mesh::kTexCoords);
        auto& faces = At<lib::AlignedArray<std::uint8_t>>(mesh, mesh::kFaces);
        for (int f = 0; f < faces.size; ++f) {
            const MeshFace& face = reinterpret_cast<MeshFace*>(faces.data)[f];
            for (int c = 0; c < 3; ++c) {
                Corner corner{};
                if (static_cast<int>(face.vertex[c]) < positions.size)
                    std::memcpy(corner.p, reinterpret_cast<char*>(positions.data) + face.vertex[c] * 12, 12);
                if (static_cast<int>(face.vertex2[c]) < coords.size)
                    std::memcpy(corner.uv, reinterpret_cast<char*>(coords.data) + face.vertex2[c] * 8, 8);
                before[m].push_back(corner);
            }
        }
    }
    WeldMeshVertices(unit, nodes);
    double weld;
    std::memcpy(&weld, reinterpret_cast<const void*>(0x0073B860), sizeof(weld));
    bool ok = true;
    for (int m = 0; ok && m < meshes.size; ++m) {
        char* mesh = reinterpret_cast<char*>(meshes.data) + m * mesh::kSize;
        auto& positions = At<lib::AlignedArray<std::uint8_t>>(mesh, mesh::kPositions);
        auto& coords = At<lib::AlignedArray<std::uint8_t>>(mesh, mesh::kTexCoords);
        auto& faces = At<lib::AlignedArray<std::uint8_t>>(mesh, mesh::kFaces);
        for (int f = 0; ok && f < faces.size; ++f) {
            const MeshFace& face = reinterpret_cast<MeshFace*>(faces.data)[f];
            for (int c = 0; ok && c < 3; ++c) {
                const Corner& old = before[m][static_cast<std::size_t>(f * 3 + c)];
                if (static_cast<int>(face.vertex[c]) >= positions.size || static_cast<int>(face.vertex2[c]) >= coords.size) {
                    ok = false;
                    break;
                }
                const float* p = reinterpret_cast<float*>(reinterpret_cast<char*>(positions.data) + face.vertex[c] * 12);
                const float* uv = reinterpret_cast<float*>(reinterpret_cast<char*>(coords.data) + face.vertex2[c] * 8);
                double dp = std::sqrt(double(p[0] - old.p[0]) * (p[0] - old.p[0]) + double(p[1] - old.p[1]) * (p[1] - old.p[1]) +
                                      double(p[2] - old.p[2]) * (p[2] - old.p[2]));
                double duv = std::hypot(double(uv[0] - old.uv[0]), double(uv[1] - old.uv[1]));
                if (dp > weld * 1.0001 || duv > weld * 1.0001) ok = false;
            }
        }
    }
    ei::verify::Check("WeldMeshVertices", true, ok, "%d meshes", meshes.size);
}
} // namespace

EI_VERIFY(0x0044C4A0, WeldVerify, 0, s_unused);
