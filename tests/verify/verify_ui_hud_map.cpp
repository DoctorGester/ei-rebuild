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

// CHudMap::UpdateView, side by side. The original runs first; what it wrote is copied (the map's
// centre and facing, the compass needle's rotation, the clipped polygon, the visible-area mesh
// and the world-bounds quad), the centre and facing are put back (without a camera the original
// normalises the facing it finds, and normalising twice can move the last bit), then ours runs.
namespace {
using UpdateFn = void(__fastcall*)(CHudMap*, void*);
UpdateFn s_updateView;

struct ViewCopy {
    float centre[4];
    std::uint8_t needle[16];
    std::vector<std::uint8_t> polygon;
    int polygonSize = 0;
    MeshCopy area;
    std::vector<std::uint8_t> uvs;
    int uvCount = 0;
    int boundsVisible = 0;
    std::uint8_t boundsQuad[0x20];
    std::uint8_t boundsUv[0x20];
};

ViewCopy CopyView(CHudMap* map) {
    ViewCopy c;
    std::memcpy(c.centre, &map->centreX_, sizeof(c.centre));
    std::memcpy(c.needle, reinterpret_cast<std::uint8_t*>(map->sceneObjects_.data_[10]) + 0x34, 16);
    c.polygonSize = map->visibleArea_.size_;
    c.polygon.resize(c.polygonSize * sizeof(UiPointF));
    if (c.polygonSize) std::memcpy(c.polygon.data(), map->visibleArea_.data_, c.polygon.size());
    auto* area = reinterpret_cast<CUiMesh*>(map->sceneObjects_.data_[1]);
    c.area = Copy(area);
    c.uvCount = area->uvCount_;
    c.uvs.resize(c.uvCount * 8);
    if (c.uvCount) std::memcpy(c.uvs.data(), area->uvs_, c.uvs.size());
    auto* bounds = reinterpret_cast<CUiMesh*>(map->sceneObjects_.data_[0]);
    c.boundsVisible = bounds->visible_;
    std::memset(c.boundsQuad, 0, sizeof(c.boundsQuad));
    std::memset(c.boundsUv, 0, sizeof(c.boundsUv));
    if (bounds->quads_) std::memcpy(c.boundsQuad, bounds->quads_, sizeof(c.boundsQuad));
    if (bounds->uvs_) std::memcpy(c.boundsUv, bounds->uvs_, sizeof(c.boundsUv));
    return c;
}
} // namespace

static void __fastcall UpdateViewVerify(CHudMap* self, void*) {
    float before[4];
    std::memcpy(before, &self->centreX_, sizeof(before));
    s_updateView(self, nullptr);
    ViewCopy expected = CopyView(self);
    std::memcpy(&self->centreX_, before, sizeof(before));
    self->CHudMap::UpdateView();
    ViewCopy actual = CopyView(self);

    const char* part = "";
    int offset = -1;
    if (std::memcmp(expected.centre, actual.centre, sizeof(expected.centre)) != 0) {
        part = "centre/facing";
        offset = 0;
    } else if (std::memcmp(expected.needle, actual.needle, sizeof(expected.needle)) != 0) {
        part = "needle";
        offset = 0;
    } else if (expected.polygonSize != actual.polygonSize) {
        part = "polygon size";
        offset = 0;
    } else if ((offset = FirstDifference(expected.polygon, actual.polygon)) >= 0) {
        part = "polygon";
    } else if (expected.area.quadCount != actual.area.quadCount ||
               expected.area.vertexCount != actual.area.vertexCount ||
               expected.area.indexCount != actual.area.indexCount || expected.uvCount != actual.uvCount) {
        part = "area counts";
        offset = 0;
    } else if ((offset = FirstDifference(expected.area.quads, actual.area.quads)) >= 0) {
        part = "area quads";
    } else if ((offset = FirstDifference(expected.area.vertices, actual.area.vertices)) >= 0) {
        part = "area vertices";
    } else if ((offset = FirstDifference(expected.area.indices, actual.area.indices)) >= 0) {
        part = "area indices";
    } else if ((offset = FirstDifference(expected.uvs, actual.uvs)) >= 0) {
        part = "area uvs";
    } else if ((offset = FirstDifference(expected.area.render, actual.area.render)) >= 0) {
        part = "area render fields";
    } else if (expected.boundsVisible != actual.boundsVisible) {
        part = "bounds visible";
        offset = 0;
    } else if (std::memcmp(expected.boundsQuad, actual.boundsQuad, sizeof(expected.boundsQuad)) != 0) {
        part = "bounds quad";
        offset = 0;
    } else if (std::memcmp(expected.boundsUv, actual.boundsUv, sizeof(expected.boundsUv)) != 0) {
        part = "bounds uv";
        offset = 0;
    }
    ei::verify::Check("CHudMap::UpdateView", -1, offset, "%s differ at +0x%X (polygon %d/%d)", part, offset,
                      expected.polygonSize, actual.polygonSize);
}
// MOV EAX,[0x007AF7C8]: 5 bytes.
EI_VERIFY(0x00642E80, UpdateViewVerify, 5, s_updateView);

// CHudMap::OnWheel (input slot 12), side by side: the zoom and the handled flag are put back
// after the original ran, then ours runs; zoom, flag and result are compared.
namespace {
using WheelFn = BOOL(__fastcall*)(void*, void*, int, int, int, int);
WheelFn s_mapWheel;

struct WheelState {
    float zoom;
    int handled;
    BOOL result;
};
} // namespace

static BOOL __fastcall MapWheelVerify(void* input, void*, int buttons, int delta, int x, int y) {
    auto* map = reinterpret_cast<CHudMap*>(static_cast<std::uint8_t*>(input) - 0x94);
    float zoom;
    std::memcpy(&zoom, &map->zoom_, 4);
    bool handled = map->inputHandled_;
    WheelState expected;
    expected.result = s_mapWheel(input, nullptr, buttons, delta, x, y);
    std::memcpy(&expected.zoom, &map->zoom_, 4);
    expected.handled = map->inputHandled_;
    std::memcpy(&map->zoom_, &zoom, 4);
    map->inputHandled_ = handled;
    WheelState actual;
    actual.result = map->CHudMap::OnWheel(buttons, delta, x, y);
    std::memcpy(&actual.zoom, &map->zoom_, 4);
    actual.handled = map->inputHandled_;
    ei::verify::Check("CHudMap::OnWheel", expected, actual, "(%d,%d) delta %d handled %d/%d", x, y, delta,
                      expected.handled, actual.handled);
    return expected.result;
}
// MOV EAX,[ESP+0x10]; PUSH ESI: 5 bytes.
EI_VERIFY(0x00645640, MapWheelVerify, 5, s_mapWheel);
