// Checks for the figure mesh rasterisation (src/game/figure_rasterise.cpp). Differential through a
// stand-in rasteriser whose slots 8 and 9 record every triangle (0x34 bytes) and the slot: the
// original, then ours, each into the stand-in; the records and results compared; then ours once
// more into the real rasteriser.
#include "verify.h"

#include "../../src/game/figure_mesh.h"
#include "../../src/game/texture.h"

#include <cstdint>
#include <cstring>
#include <vector>

int __fastcall RasteriseFigureMesh(CFigureMesh* mesh, void*, CPixelConverter* rasteriser, const RasterTriangle::Point* points,
                                   RasterTriangle* triangle);

namespace {
using RasteriseFn = int(__fastcall*)(CFigureMesh*, void*, CPixelConverter*, const RasterTriangle::Point*, RasterTriangle*);
RasteriseFn s_original;

std::vector<std::uint8_t>* s_record;

void Record(int slot, const RasterTriangle* triangle) {
    const auto* bytes = reinterpret_cast<const std::uint8_t*>(triangle);
    s_record->push_back(static_cast<std::uint8_t>(slot));
    s_record->insert(s_record->end(), bytes, bytes + sizeof(RasterTriangle));
}
void __fastcall Slot8(void*, void*, const RasterTriangle* triangle) { Record(8, triangle); }
void __fastcall Slot9(void*, void*, const RasterTriangle* triangle) { Record(9, triangle); }

void* s_vtable[10];
struct StandIn {
    void** vtable;
};

int RunInto(RasteriseFn fn, CFigureMesh* mesh, const RasterTriangle::Point* points, RasterTriangle* triangle,
            std::vector<std::uint8_t>& out) {
    s_vtable[8] = reinterpret_cast<void*>(&Slot8);
    s_vtable[9] = reinterpret_cast<void*>(&Slot9);
    StandIn standIn{s_vtable};
    s_record = &out;
    return fn(mesh, nullptr, reinterpret_cast<CPixelConverter*>(&standIn), points, triangle);
}
} // namespace

// 005A0E80.
static int __fastcall RasteriseVerify(CFigureMesh* mesh, void*, CPixelConverter* rasteriser, const RasterTriangle::Point* points,
                                      RasterTriangle* triangle) {
    std::vector<std::uint8_t> expected, actual;
    int original = RunInto(s_original, mesh, points, triangle, expected);
    int result = RunInto(&RasteriseFigureMesh, mesh, points, triangle, actual);
    ei::verify::Check("RasteriseFigureMesh", true, result == original && actual == expected, "%d bytes of triangles",
                      static_cast<int>(actual.size()));
    return RasteriseFigureMesh(mesh, nullptr, rasteriser, points, triangle);
}
EI_VERIFY(0x005A0E80, RasteriseVerify, 6, s_original);
