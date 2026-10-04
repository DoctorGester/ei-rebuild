// Checks for CRegionMap::ReachesRegion (src/game/ai_region_reach.cpp). Differential: the marks
// over the 3x3 blocks around the cell's block (inside the region map) are kept, the original
// runs, its marks are kept and the first ones put back, then ours runs; the results and the marks
// are compared. Both leave the local search reset and rebuild the static block list.
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstdint>
#include <vector>

namespace {
using ReachesFn = bool(__fastcall*)(CRegionMap*, void*, int, int, int, int, void*);
ReachesFn s_original;

struct Window {
    int x0, y0, x1, y1;
};

Window WindowOf(CRegionMap* map, int x, int y) {
    auto* base = reinterpret_cast<std::uint8_t*>(map);
    int blocksWide = *reinterpret_cast<int*>(base + 0x124), blocksHigh = *reinterpret_cast<int*>(base + 0x128);
    int bx = x / 8, by = y / 8;
    if (bx > blocksWide - 1) bx = blocksWide - 1;
    if (bx < 0) bx = 0;
    if (by > blocksHigh - 1) by = blocksHigh - 1;
    if (by < 0) by = 0;
    Window w{bx * 8 - 8, by * 8 - 8, bx * 8 + 16, by * 8 + 16};
    if (w.x0 < 0) w.x0 = 0;
    if (w.y0 < 0) w.y0 = 0;
    if (w.x1 > blocksWide * 8) w.x1 = blocksWide * 8;
    if (w.y1 > blocksHigh * 8) w.y1 = blocksHigh * 8;
    return w;
}

std::uint8_t** Rows(void* marks) { return *reinterpret_cast<std::uint8_t***>(static_cast<std::uint8_t*>(marks) + 4); }

void Save(void* marks, const Window& w, std::vector<std::uint8_t>& out) {
    out.clear();
    for (int y = w.y0; y < w.y1; ++y)
        for (int x = w.x0; x < w.x1; ++x) out.push_back(Rows(marks)[y][x]);
}
void Restore(void* marks, const Window& w, const std::vector<std::uint8_t>& in) {
    std::size_t i = 0;
    for (int y = w.y0; y < w.y1; ++y)
        for (int x = w.x0; x < w.x1; ++x) Rows(marks)[y][x] = in[i++];
}
} // namespace

// 005B07C0.
static bool __fastcall ReachesVerify(CRegionMap* self, void*, int sizeClass, int x, int y, int region, void* marks) {
    Window w = WindowOf(self, x, y);
    std::vector<std::uint8_t> before, expected, actual;
    Save(marks, w, before);
    bool original = s_original(self, nullptr, sizeClass, x, y, region, marks);
    Save(marks, w, expected);
    Restore(marks, w, before);
    bool result = self->ReachesRegion(sizeClass, x, y, region, marks);
    Save(marks, w, actual);
    bool ok = result == original && actual == expected;
    ei::verify::Check("CRegionMap::ReachesRegion", true, ok, "class %d (%d,%d) region %d: %d (original %d)", sizeClass, x,
                      y, region, result, original);
    return result;
}
EI_VERIFY(0x005B07C0, ReachesVerify, 7, s_original);

namespace {
using LabelFn = void(__fastcall*)(CRegionMap*, void*);
LabelFn s_originalLabel;

void SaveGrids(CRegionMap* map, std::vector<std::uint16_t>& out) {
    out.clear();
    auto* base = reinterpret_cast<std::uint8_t*>(map);
    for (int c = 0; c < 8; ++c) {
        auto* data = *reinterpret_cast<std::uint16_t**>(base + 0x21C + c * 16);
        int width = *reinterpret_cast<int*>(base + 0x224 + c * 16), height = *reinterpret_cast<int*>(base + 0x228 + c * 16);
        out.insert(out.end(), data, data + width * height);
    }
}
} // namespace

// 005B0050: recomputes every grid from scratch; the original, then ours, the grids compared.
static void __fastcall LabelRegionsVerify(CRegionMap* self, void*) {
    s_originalLabel(self, nullptr);
    std::vector<std::uint16_t> expected, actual;
    SaveGrids(self, expected);
    self->LabelRegions();
    SaveGrids(self, actual);
    int differences = 0;
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != expected[i]) ++differences;
    ei::verify::Check("CRegionMap::LabelRegions", 0, differences, "%d blocks", static_cast<int>(actual.size()));
}
EI_VERIFY(0x005B0050, LabelRegionsVerify, 8, s_originalLabel);
