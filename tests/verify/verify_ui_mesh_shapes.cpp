// Checks for the interface sprites' shapes (A-I22, src/game/ui_mesh_shapes.cpp): the rectangle
// 005E70E0, the grid 005E7680, the frame 005E7D60 and the triangle 005E99D0. Rebirth's WideScreen
// patches them (x moved by the panel offset); ours reproduce it behind the flag (W2), so in a
// Rebirth launch these compare with addon's patched code (ui_widescreen: the battle screen's panels,
// built while addon's panel builders set the offset).
//
// All four side by side. Each writes all of what it sizes (the point packets, one packet of
// normals, the corners, four texture coordinates, the indices), the position and the three
// scales, so the original runs, its result is copied, the written parts are overwritten with other
// values and ours runs on the same sprite; the copies are compared (floats within 4 ULPs, the rest
// by bytes). The colour arrays are only sized (their contents are not written and not compared).
// Scenarios: rectangles and frames on every screen (ui_main_menu, ui_camp, ui_hud). The grid's
// callers are 005F2AC9 .. 005F2B81, 006208B8 .. 00622BA2 (the menus' screens) and 00645ABF (the
// HUD), the triangle's 006485CE, 006493C2 / 006494C8 and 0064D0AA (the HUD's panels): ui_screens,
// ui_main_menu, ui_hud and ui_objectives are the scenarios that open those; which of them reach
// the two has not been run (the worker cannot run the game).
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/ui.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace {

template <typename T>
void Copy(std::vector<std::uint8_t>& to, const T* from, int count) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(from);
    to.assign(bytes, bytes + (from && count > 0 ? static_cast<std::size_t>(count) * sizeof(T) : 0));
}

// What a shape leaves in a sprite.
struct Shape {
    Vector3 position;
    Vector3 scale;
    int counts[7];
    int handed[6];
    std::vector<std::uint8_t> points, normals, corners, indices, texels;

    void Read(CUiMesh* sprite) {
        CFigureMesh* mesh = sprite;
        position = sprite->localPosition_;
        scale = Vector3{sprite->ScaleX(), sprite->ScaleY(), sprite->ScaleZ()};
        const int sizes[7] = {sprite->vertices_.size,   sprite->corners_.size, sprite->indices_.size, sprite->texCoords_.size,
                              mesh->normals_.size, mesh->colorsA_.size,  mesh->colorsB_.size};
        std::memcpy(counts, sizes, sizeof(counts));
        const MeshBuffers& b = mesh->buffers_;
        const int given[6] = {b.vertexCount, b.normalCount, b.texCoordCount, b.cornerCount, b.wordCount, b.indexCount};
        std::memcpy(handed, given, sizeof(handed));
        Copy(points, sprite->Quads().data, sprite->vertices_.size);
        Copy(normals, mesh->normals_.data, mesh->normals_.size);
        Copy(corners, sprite->Vertices().data, sprite->corners_.size);
        Copy(indices, sprite->indices_.data, sprite->indices_.size);
        Copy(texels, sprite->Texels().data, sprite->texCoords_.size);
    }
};

// The parts a shape writes, given other values (the sizes stay).
void Scramble(CUiMesh* sprite, const Vector3& position, const Vector3& scale) {
    CFigureMesh* mesh = sprite;
    sprite->localPosition_ = position;
    sprite->ScaleX() = scale.x;
    sprite->ScaleY() = scale.y;
    sprite->ScaleZ() = scale.z;
    auto fill = [](void* data, std::size_t bytes) {
        if (data && bytes) std::memset(data, 0x5A, bytes);
    };
    fill(sprite->Quads().data, static_cast<std::size_t>(sprite->vertices_.size) * sizeof(UiQuadBlock));
    fill(mesh->normals_.data, static_cast<std::size_t>(mesh->normals_.size) * sizeof(NormalPacket));
    fill(sprite->Vertices().data, static_cast<std::size_t>(sprite->corners_.size) * sizeof(UiMeshVertex));
    fill(sprite->Texels().data, static_cast<std::size_t>(sprite->texCoords_.size) * sizeof(UiTexel));
}

// Float arrays within 4 ULPs of each other (NaNs and infinities, a grid without cells, by bits).
bool NearFloats(const std::vector<std::uint8_t>& expected, const std::vector<std::uint8_t>& actual) {
    if (expected.size() != actual.size()) return false;
    for (std::size_t i = 0; i + sizeof(float) <= expected.size(); i += sizeof(float)) {
        if (std::memcmp(&expected[i], &actual[i], sizeof(float)) == 0) continue;
        float a, b;
        std::memcpy(&a, &expected[i], sizeof(float));
        std::memcpy(&b, &actual[i], sizeof(float));
        if (!ei::verify::NearlyEqual(a, b, 4, 1e-6f)) return false;
    }
    return true;
}
bool NearVector(const Vector3& a, const Vector3& b) {
    return ei::verify::NearlyEqual(a.x, b.x, 4, 1e-6f) && ei::verify::NearlyEqual(a.y, b.y, 4, 1e-6f) &&
           ei::verify::NearlyEqual(a.z, b.z, 4, 1e-6f);
}

// Runs the original, then ours over a scrambled copy of its result, and compares.
template <typename Original, typename Ours>
void SideBySide(const char* name, CUiMesh* sprite, Original original, Ours ours) {
    static Shape expected, actual;
    Vector3 position = sprite->localPosition_;
    Vector3 scale{sprite->ScaleX(), sprite->ScaleY(), sprite->ScaleZ()};
    original();
    expected.Read(sprite);
    Scramble(sprite, position, scale);
    ours();
    actual.Read(sprite);
    bool sizes = std::memcmp(expected.counts, actual.counts, sizeof(expected.counts)) == 0 &&
                 std::memcmp(expected.handed, actual.handed, sizeof(expected.handed)) == 0;
    bool placed = NearVector(expected.position, actual.position) && NearVector(expected.scale, actual.scale);
    bool points = NearFloats(expected.points, actual.points);
    bool texels = NearFloats(expected.texels, actual.texels);
    bool rest = expected.normals == actual.normals && expected.corners == actual.corners && expected.indices == actual.indices;
    ei::verify::Check(name, true, sizes && placed && points && texels && rest,
                      "sizes %d placed %d points %d texels %d corners %d (packets %d corners %d indices %d)", sizes, placed,
                      points, texels, rest, actual.counts[0], actual.counts[1], actual.counts[2]);
}

} // namespace

// SUB ESP,0x10; PUSH EBX; MOV EBX,ECX (3 + 1 + 2 bytes).
static void(__fastcall* s_setRectangle)(CUiMesh*, void*, float, float, float, float, float, float, int, int, int, int);
static void __fastcall SetRectangleVerify(CUiMesh* self, void*, float depth, float scale, float left, float top,
                                          float right, float bottom, int u0, int v0, int u1, int v1) {
    SideBySide(
        "CUiMesh::SetRectangleVanilla", self,
        [&] { s_setRectangle(self, nullptr, depth, scale, left, top, right, bottom, u0, v0, u1, v1); },
        [&] { self->SetRectangleVanilla(depth, scale, left, top, right, bottom, u0, v0, u1, v1); });
}
EI_VERIFY(0x005E70E0, SetRectangleVerify, 6, s_setRectangle);

// SUB ESP,0x18; PUSH EBX; MOV EBX,ECX (3 + 1 + 2 bytes).
static void(__fastcall* s_setGrid)(CUiMesh*, void*, float, float, float, float, float, float, int, int, int, int, int, int);
static void __fastcall SetGridVerify(CUiMesh* self, void*, float depth, float scale, float left, float top, float right,
                                     float bottom, int u0, int v0, int u1, int v1, int columns, int rows) {
    SideBySide(
        "CUiMesh::SetGridVanilla", self,
        [&] { s_setGrid(self, nullptr, depth, scale, left, top, right, bottom, u0, v0, u1, v1, columns, rows); },
        [&] { self->SetGridVanilla(depth, scale, left, top, right, bottom, u0, v0, u1, v1, columns, rows); });
}
EI_VERIFY(0x005E7680, SetGridVerify, 6, s_setGrid);

// SUB ESP,0x10; PUSH EBX; MOV EBX,ECX (3 + 1 + 2 bytes).
static void(__fastcall* s_setFrame)(CUiMesh*, void*, float, float, float, float, float, float, float, int, int, int, int);
static void __fastcall SetFrameVerify(CUiMesh* self, void*, float depth, float scale, float left, float top, float right,
                                      float bottom, float border, int u0, int v0, int u1, int v1) {
    SideBySide(
        "CUiMesh::SetFrameVanilla", self,
        [&] { s_setFrame(self, nullptr, depth, scale, left, top, right, bottom, border, u0, v0, u1, v1); },
        [&] { self->SetFrameVanilla(depth, scale, left, top, right, bottom, border, u0, v0, u1, v1); });
}
EI_VERIFY(0x005E7D60, SetFrameVerify, 6, s_setFrame);

// SUB ESP,0x10; PUSH EBX; MOV EBX,ECX (3 + 1 + 2 bytes).
static void(__fastcall* s_setTriangle)(CUiMesh*, void*, float, float, float, float, float, float, float, float, float, float,
                                       float, float, float, float);
static void __fastcall SetTriangleVerify(CUiMesh* self, void*, float depth, float scale, float x0, float y0, float x1,
                                         float y1, float x2, float y2, float u0, float v0, float u1, float v1, float u2,
                                         float v2) {
    SideBySide(
        "CUiMesh::SetTriangleVanilla", self,
        [&] { s_setTriangle(self, nullptr, depth, scale, x0, y0, x1, y1, x2, y2, u0, v0, u1, v1, u2, v2); },
        [&] { self->SetTriangleVanilla(depth, scale, x0, y0, x1, y1, x2, y2, u0, v0, u1, v1, u2, v2); });
}
EI_VERIFY(0x005E99D0, SetTriangleVerify, 6, s_setTriangle);
