// Checks for scene_normals.cpp: the original runs first, its results are recorded (each mesh's
// normals and normal blocks), then ours recomputes them from the same inputs over its results
// (both start by emptying the normals; the face normals and the blocks are rebuilt). Normals are
// compared within a few ULPs (rule 7: plain float sums and divisions); a sum within rounding of
// the weld distance may pick the zero vector on one side only.
#include "verify.h"

#include "../../src/game/scene_loader.h"

#include <cstring>
#include <vector>

namespace scene {
void __fastcall ComputeMeshNormals(VtArray* meshes);
void __fastcall PackMeshPositions(void* unit);
void __fastcall ComputeMeshBounds(char* mesh);
}

namespace {
using namespace scene;

template <typename T>
T& At(const void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(const_cast<void*>(base)) + offset);
}

struct Recorded {
    std::vector<float> normals;
    std::vector<float> blocks;
};

// 0044DCF0: PUSH -1 (2), PUSH imm32 (5).
void(__fastcall* s_normals)(VtArray*);
void __fastcall NormalsVerify(VtArray* meshes) {
    s_normals(meshes);
    std::vector<Recorded> expected(meshes->body.size > 0 ? meshes->body.size : 0);
    for (int m = 0; m < meshes->body.size; ++m) {
        char* mesh = static_cast<char*>(meshes->body.data) + m * mesh::kSize;
        auto& normals = At<ArrayBody>(mesh, 0xF8);
        auto& blocks = At<ArrayBody>(mesh, 0xB8);
        auto* n = static_cast<float*>(normals.data);
        auto* b = static_cast<float*>(blocks.data);
        expected[m].normals.assign(n, n + normals.size * 3);
        expected[m].blocks.assign(b, b + blocks.size * 16);
    }
    ComputeMeshNormals(meshes);
    bool ok = true;
    int bad = -1;
    for (int m = 0; ok && m < meshes->body.size; ++m) {
        char* mesh = static_cast<char*>(meshes->body.data) + m * mesh::kSize;
        auto& normals = At<ArrayBody>(mesh, 0xF8);
        auto& blocks = At<ArrayBody>(mesh, 0xB8);
        if (static_cast<std::size_t>(normals.size) * 3 != expected[m].normals.size() ||
            static_cast<std::size_t>(blocks.size) * 16 != expected[m].blocks.size()) {
            ok = false;
            bad = m;
            break;
        }
        const float* n = static_cast<float*>(normals.data);
        for (std::size_t i = 0; ok && i < expected[m].normals.size(); ++i)
            if (!ei::verify::NearlyEqual(expected[m].normals[i], n[i], 4, 1e-6f)) ok = false, bad = m;
        const float* b = static_cast<float*>(blocks.data);
        for (std::size_t i = 0; ok && i < expected[m].blocks.size(); ++i)
            if (!ei::verify::NearlyEqual(expected[m].blocks[i], b[i], 4, 1e-6f)) ok = false, bad = m;
    }
    ei::verify::Check("ComputeMeshNormals", true, ok, "mesh %d of %d", bad, meshes->body.size);
}

// 0044E3B0: SUB ESP,0x14 (3), PUSH EBX (1), MOV EBX,ECX (2). The original first, then ours over
// its blocks (both resize and refill them from the positions); compared bitwise.
void(__fastcall* s_pack)(void*);
void __fastcall PackVerify(void* unit) {
    s_pack(unit);
    VtArray& meshes = At<VtArray>(unit, 0x544);
    std::vector<std::vector<std::uint32_t>> expected(meshes.body.size > 0 ? meshes.body.size : 0);
    for (int m = 0; m < meshes.body.size; ++m) {
        auto& blocks = At<ArrayBody>(static_cast<char*>(meshes.body.data) + m * mesh::kSize, 0xA8);
        auto* b = static_cast<std::uint32_t*>(blocks.data);
        expected[m].assign(b, b + blocks.size * 16);
    }
    PackMeshPositions(unit);
    bool ok = true;
    for (int m = 0; ok && m < meshes.body.size; ++m) {
        auto& blocks = At<ArrayBody>(static_cast<char*>(meshes.body.data) + m * mesh::kSize, 0xA8);
        ok = static_cast<std::size_t>(blocks.size) * 16 == expected[m].size() &&
             (expected[m].empty() || std::memcmp(blocks.data, expected[m].data(), expected[m].size() * 4) == 0);
    }
    ei::verify::Check("PackMeshPositions", true, ok, "");
}

// 0044E8A0: PUSH EBP, MOV EBP,ESP, SUB ESP,0x64 (6). Side by side: the original on a copy of the
// mesh's 0x18C bytes (it only reads the positions and writes +0x15C..+0x183), ours on the mesh;
// the ten floats compared within a few ULPs (rule 7).
void(__fastcall* s_bounds)(char*);
void __fastcall BoundsVerify(char* mesh) {
    std::vector<char> copy(mesh, mesh + mesh::kSize);
    s_bounds(copy.data());
    ComputeMeshBounds(mesh);
    bool ok = true;
    for (std::size_t o = 0x15C; o < 0x184; o += 4)
        ok = ok && ei::verify::NearlyEqual(At<float>(copy.data(), o), At<float>(mesh, o), 4, 1e-6f);
    ei::verify::Check("ComputeMeshBounds", true, ok, "");
}
} // namespace

EI_VERIFY(0x0044DCF0, NormalsVerify, 7, s_normals);
EI_VERIFY(0x0044E3B0, PackVerify, 6, s_pack);
EI_VERIFY(0x0044E8A0, BoundsVerify, 6, s_bounds);
