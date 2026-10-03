// Checks for the minimap marker mesh (ui_hud_map.cpp).
//
// CHudMap::BuildMarkers runs side by side: the original first (through the trampoline), then a
// copy of what it wrote (the quad positions, vertex records, indices and the renderer fields
// Commit sets), then ours over the same mesh, and the two compared byte for byte. Both size the
// arrays to the same counts, so the second run reuses the storage the first one left. Entries the
// code hides keep their old positions in both runs.
#include "verify.h"

#include "../../src/game/ui.h"

#include <cstring>
#include <vector>

namespace {
using BuildFn = void(__fastcall*)(CHudMap*, void*);
BuildFn s_buildMarkers;

struct MeshCopy {
    std::vector<std::uint8_t> quads, vertices, indices, render;
    int quadCount = 0, vertexCount = 0, indexCount = 0;
};

MeshCopy Copy(const CUiMesh* mesh) {
    MeshCopy c;
    c.quadCount = mesh->quadCount_;
    c.vertexCount = mesh->vertexCount_;
    c.indexCount = mesh->indexCount_;
    auto take = [](std::vector<std::uint8_t>& v, const void* p, std::size_t n) {
        v.resize(n);
        if (n) std::memcpy(v.data(), p, n);
    };
    take(c.quads, mesh->quads_, c.quadCount * sizeof(UiQuadBlock));
    take(c.vertices, mesh->vertices_, c.vertexCount * sizeof(UiMeshVertex));
    take(c.indices, mesh->indices_, c.indexCount * sizeof(std::uint16_t));
    take(c.render, reinterpret_cast<const std::uint8_t*>(mesh) + 0x9C, 0xCC - 0x9C);   // Commit
    return c;
}

// Offset of the first differing byte (-1: none).
int FirstDifference(const std::vector<std::uint8_t>& a, const std::vector<std::uint8_t>& b) {
    if (a.size() != b.size()) return 0;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (a[i] != b[i]) return static_cast<int>(i);
    return -1;
}
} // namespace

static void __fastcall BuildMarkersVerify(CHudMap* self, void*) {
    auto* mesh = reinterpret_cast<CUiMesh*>(self->sceneObjects_.data_[2]);
    s_buildMarkers(self, nullptr);
    MeshCopy expected = Copy(mesh);
    self->CHudMap::BuildMarkers();
    MeshCopy actual = Copy(mesh);

    const char* part = "";
    int offset = -1;
    if (expected.quadCount != actual.quadCount || expected.vertexCount != actual.vertexCount ||
        expected.indexCount != actual.indexCount) {
        part = "counts";
        offset = 0;
    } else if ((offset = FirstDifference(expected.quads, actual.quads)) >= 0) {
        part = "quads";
    } else if ((offset = FirstDifference(expected.vertices, actual.vertices)) >= 0) {
        part = "vertices";
    } else if ((offset = FirstDifference(expected.indices, actual.indices)) >= 0) {
        part = "indices";
    } else if ((offset = FirstDifference(expected.render, actual.render)) >= 0) {
        part = "render fields";
    }
    ei::verify::Check("CHudMap::BuildMarkers", -1, offset, "%s differ at +0x%X (counts %d/%d %d/%d %d/%d)", part,
                      offset, expected.quadCount, actual.quadCount, expected.vertexCount, actual.vertexCount,
                      expected.indexCount, actual.indexCount);
}
// PUSH EBP; MOV EBP,ESP; SUB ESP,0xD8: 9 bytes.
EI_VERIFY(0x00643DC0, BuildMarkersVerify, 9, s_buildMarkers);
