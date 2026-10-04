// Checks for CRegionMap::ReachesRegion (src/game/ai_region_reach.cpp). Differential: the marks
// over the 3x3 blocks around the cell's block (inside the region map) are kept, the original
// runs, its marks are kept and the first ones put back, then ours runs; the results and the marks
// are compared. Both leave the local search reset and rebuild the static block list.
#include "verify.h"

#include "../../src/game/ai_map.h"
#include "../../src/lib/mfc.h"

#include <cstdint>
#include <cstring>
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

namespace {
LabelFn s_originalClamp;

// The edge words of every size class (+0x120 + 16 * class: rows, width, height), saved or put back.
void EdgeWords(CRegionMap* map, std::vector<std::int16_t>& words, bool restore) {
    auto* base = reinterpret_cast<std::uint8_t*>(map);
    if (!restore) words.clear();
    std::size_t i = 0;
    for (int c = 0; c < 8; ++c) {
        auto** rows = *reinterpret_cast<std::int16_t***>(base + 0x120 + c * 16);
        int width = *reinterpret_cast<int*>(base + 0x124 + c * 16), height = *reinterpret_cast<int*>(base + 0x128 + c * 16);
        for (int y = 0; y < height; ++y)
            for (int x = 0; x < width * 8; ++x) {
                if (restore) rows[y][x] = words[i++];
                else words.push_back(rows[y][x]);
            }
    }
}
} // namespace

// 005B0F70: snapshot, the original, its words kept, the snapshot put back, ours; compared.
static void __fastcall ClampEdgeCostsVerify(CRegionMap* self, void*) {
    std::vector<std::int16_t> before, expected, actual;
    EdgeWords(self, before, false);
    s_originalClamp(self, nullptr);
    EdgeWords(self, expected, false);
    EdgeWords(self, before, true);
    self->ClampEdgeCosts();
    EdgeWords(self, actual, false);
    int differences = 0;
    for (std::size_t i = 0; i < actual.size(); ++i)
        if (actual[i] != expected[i]) ++differences;
    ei::verify::Check("CRegionMap::ClampEdgeCosts", 0, differences, "%d words", static_cast<int>(actual.size()));
}
EI_VERIFY(0x005B0F70, ClampEdgeCostsVerify, 5, s_originalClamp);

namespace {
void* s_unusedLoad;
}

// 005B1180: reads a stream, so ours only. After a load every grid of every class has the size
// of the first (the map's block size, 1..256).
static bool __fastcall LoadBlocksVerify(CRegionMap* self, void*, void* stream) {
    bool result = self->LoadBlocks(stream);
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    int width = *reinterpret_cast<int*>(base + 0x124), height = *reinterpret_cast<int*>(base + 0x128);
    bool ok = true;
    if (width > 0 && width <= 0x100 && height > 0 && height <= 0x100) {
        for (int grid = 0x11C; grid < 0x29C && ok; grid += 16)
            ok = *reinterpret_cast<int*>(base + grid + 8) == width && *reinterpret_cast<int*>(base + grid + 12) == height;
    }
    ei::verify::Check("CRegionMap::LoadBlocks", true, ok, "%dx%d -> %d", width, height, result);
    return result;
}
EI_VERIFY(0x005B1180, LoadBlocksVerify, 0, s_unusedLoad);

namespace {
using PickFn = void(__fastcall*)(CRegionMap*, void*, CAIMap*, int, int);
PickFn s_originalPick;

std::uint8_t& RepresentativeOf(CRegionMap* map, int c, int bx, int by) {
    auto* base = reinterpret_cast<std::uint8_t*>(map);
    return (*reinterpret_cast<std::uint8_t***>(base + 0x1A0 + c * 16))[by][bx];
}
} // namespace

// 005AF900: the block's 8 representative bytes: snapshot, the original, kept, put back, ours.
static void __fastcall PickVerify(CRegionMap* self, void*, CAIMap* map, int bx, int by) {
    std::uint8_t before[8], expected[8], actual[8];
    for (int c = 0; c < 8; ++c) before[c] = RepresentativeOf(self, c, bx, by);
    s_originalPick(self, nullptr, map, bx, by);
    for (int c = 0; c < 8; ++c) {
        expected[c] = RepresentativeOf(self, c, bx, by);
        RepresentativeOf(self, c, bx, by) = before[c];
    }
    self->PickRepresentatives(map, bx, by);
    for (int c = 0; c < 8; ++c) actual[c] = RepresentativeOf(self, c, bx, by);
    ei::verify::Check("CRegionMap::PickRepresentatives", true, std::memcmp(actual, expected, 8) == 0, "block (%d,%d)", bx,
                      by);
}
EI_VERIFY(0x005AF900, PickVerify, 5, s_originalPick);

namespace {
using NearbyFn = void(__fastcall*)(CRegionMap*, void*, int, bool, int, int, BlockCostList*);
NearbyFn s_originalNearby;

std::vector<int> Records(const BlockCostList* list) {
    std::vector<int> out;
    for (const BlockCostList::Node* n = list->head; n; n = n->next) {
        out.push_back(n->x);
        out.push_back(n->y);
        out.push_back(n->cost);
    }
    return out;
}
} // namespace

// 005B0370: the original fills a scratch list of the same shape, ours the caller's; the records
// (in order) and the counts compared. The scratch list's blocks are freed afterwards.
static void __fastcall NearbyBlocksVerify(CRegionMap* self, void*, int sizeClass, bool flag, int x, int y,
                                          BlockCostList* out) {
    BlockCostList scratch{out->vtable, nullptr, nullptr, 0, nullptr, nullptr, out->blockSize};
    s_originalNearby(self, nullptr, sizeClass, flag, x, y, &scratch);
    std::vector<int> expected = Records(&scratch);
    int expectedCount = scratch.count;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(scratch.blocks));
    self->NearbyBlocks(sizeClass, flag, x, y, out);
    std::vector<int> actual = Records(out);
    bool ok = actual == expected && out->count == expectedCount;
    ei::verify::Check("CRegionMap::NearbyBlocks", true, ok, "class %d (%d,%d): %d blocks (original %d)", sizeClass, x, y,
                      out->count, expectedCount);
}
EI_VERIFY(0x005B0370, NearbyBlocksVerify, 7, s_originalNearby);
