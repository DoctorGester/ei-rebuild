// Checks for the block searches and the region map's block update (src/game/ai_block_search.cpp),
// CAIMap::ClearUnmarkedArea (ai_refresh.cpp) and VisibilityGrid::Grow (ai_movement.cpp).
#include "verify.h"

#include "../../src/game/ai_map.h"

#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace {
void* s_unused;

// Every bucket is sorted by cost, linked both ways, and holds only records of its x & 7.
bool BucketsConsistent(const BlockSearch* search) {
    for (int b = 0; b < 8; ++b) {
        const BlockRecord* prev = nullptr;
        for (const BlockRecord* r = search->open[b]; r; prev = r, r = r->next) {
            if ((r->x & 7) != b || r->prev != prev) return false;
            if (prev && prev->cost > r->cost) return false;
        }
    }
    return true;
}
bool InBucket(const BlockSearch* search, const BlockRecord* record) {
    for (const BlockRecord* r = search->open[record->x & 7]; r; r = r->next)
        if (r == record) return true;
    return false;
}
} // namespace

// 005AED60: allocates, so ours only. Afterwards one unreached record per block, nothing touched.
static void __fastcall AttachVerify(BlockSearch* self, void*, CRegionMap* map, bool fromTarget) {
    self->Attach(map, fromTarget);
    bool ok = self->records.width == map->edges[0].width && self->records.height == map->edges[0].height &&
              self->map == map && !self->touched && !self->active;
    for (int y = 0; ok && y < self->records.height; ++y)
        for (int x = 0; x < self->records.width && ok; ++x) {
            const BlockRecord& r = self->records.rows[y][x];
            ok = r.cost == 0x7FFFFFFF && r.x == x && r.y == y && !r.next && !r.prev && !r.settled;
        }
    ei::verify::Check("BlockSearch::Attach", true, ok, "%dx%d", self->records.width, self->records.height);
}
EI_VERIFY(0x005AED60, AttachVerify, 0, s_unused);

// 005AEE30: relinks records, so ours only: the record is in its bucket and the buckets stay sorted.
static void __fastcall RequeueVerify(BlockSearch* self, void*, BlockRecord* record) {
    self->Requeue(record);
    ei::verify::Check("BlockSearch::Requeue", true, InBucket(self, record) && BucketsConsistent(self),
                      "block (%d,%d) cost %d", record->x, record->y, record->cost);
}
EI_VERIFY(0x005AEE30, RequeueVerify, 0, s_unused);

// 005AEEC0: ours only against the expected neighbour cost computed first.
static void __fastcall RelaxVerify(BlockSearch* self, void*, int direction, int back) {
    int bx = self->best->x, by = self->best->y;
    int edge = self->edges->rows[by][bx * 8 + direction - 1];
    BlockRecord* record = edge < 0 ? nullptr
                                   : &self->records.rows[by + DirectionDy()[direction]][bx + DirectionDx()[direction]];
    int expected = record ? record->cost : 0;
    if (record && self->bestCost < expected && self->bestCost + edge < expected) expected = self->bestCost + edge;
    self->Relax(direction, back);
    int actual = record ? record->cost : 0;
    ei::verify::Check("BlockSearch::Relax", expected, actual, "(%d,%d) direction %d", bx, by, direction);
}
EI_VERIFY(0x005AEEC0, RelaxVerify, 0, s_unused);

namespace {
using PickFn = void(__fastcall*)(BlockSearch*);
PickFn s_originalPick;
}

// 005AEFF0: writes only `best`: the original, then ours; compared.
static void __fastcall PickBestVerify(BlockSearch* self) {
    s_originalPick(self);
    BlockRecord* expected = self->best;
    self->PickBest();
    ei::verify::Check("BlockSearch::PickBest", expected, self->best, "");
}
EI_VERIFY(0x005AEFF0, PickBestVerify, 6, s_originalPick);

// 005AF0C0: ours only: the expanded record is settled and heads the touched chain.
static void __fastcall ExpandVerify(BlockSearch* self) {
    BlockRecord* record = self->best;
    self->Expand();
    bool ok = record->settled && self->touched == record && self->bestCost == record->cost && BucketsConsistent(self);
    ei::verify::Check("BlockSearch::Expand", true, ok, "block (%d,%d) cost %d", record->x, record->y, record->cost);
}
EI_VERIFY(0x005AF0C0, ExpandVerify, 0, s_unused);

// 005B0AF0: ours only: `best` is the cheapest bucket head and the buckets stay sorted.
static bool __fastcall SeedSearchVerify(CRegionMap* self, void*, int sizeClass, bool flag, BlockSearch* search, int x,
                                        int y) {
    bool result = self->SeedSearch(sizeClass, flag, search, x, y);
    const BlockRecord* cheapest = nullptr;
    for (const BlockRecord* head : search->open)
        if (head && head->cost < 0x7FFFFFFF && (!cheapest || head->cost < cheapest->cost)) cheapest = head;
    bool ok = BucketsConsistent(search) && (!cheapest || search->best->cost == cheapest->cost);
    ei::verify::Check("CRegionMap::SeedSearch", true, ok, "class %d (%d,%d) -> %d", sizeClass, x, y, result);
    return result;
}
EI_VERIFY(0x005B0AF0, SeedSearchVerify, 0, s_unused);

// 005AFC70: ours only (its steps are verified on their own): the searches are attached and the
// progress report cleared.
static void __fastcall UpdateBlocksVerify(CRegionMap* self, void*, CAIMap* map, int x0, int x1, int y0, int y1) {
    self->UpdateBlocks(map, x0, x1, y0, y1);
    bool ok = !self->progress && self->forward.map == self && self->backward.map == self && self->backward.fromTarget &&
              !self->forward.fromTarget;
    ei::verify::Check("CRegionMap::UpdateBlocks", true, ok, "[%d,%d)x[%d,%d)", x0, x1, y0, y1);
}
EI_VERIFY(0x005AFC70, UpdateBlocksVerify, 0, s_unused);

// 005AFEE0 / 005AFEC0: thunks, ours only (their callees are checked).
static void __fastcall UpdateBlocksForVerify(CRegionMap* self, void*, CAIMap* map, int x0, int x1, int y0, int y1) {
    self->UpdateBlocksFor(map, x0, x1, y0, y1);
    ei::verify::Check("CRegionMap::UpdateBlocksFor", true, !self->progress, "");
}
EI_VERIFY(0x005AFEE0, UpdateBlocksForVerify, 0, s_unused);

static void __fastcall AttachSearchesVerify(CRegionMap* self) {
    self->AttachSearches();
    ei::verify::Check("CRegionMap::AttachSearches", true, self->forward.map == self && self->backward.fromTarget, "");
}
EI_VERIFY(0x005AFEC0, AttachSearchesVerify, 0, s_unused);

namespace {
using ClearFn = void(__fastcall*)(CAIMap*, void*, int, int, int, int, int, int, int);
ClearFn s_originalClear;
}

// 005B89F0: writes only cell words in the rectangle: kept, the original, its words kept and the
// first ones put back, ours; compared.
static void __fastcall ClearUnmarkedAreaVerify(CAIMap* self, void*, int level, int dx, int dy, int x0, int x1, int y0,
                                               int y1) {
    int cx0 = x0 < 0 ? 0 : x0, cy0 = y0 < 0 ? 0 : y0;
    int cx1 = x1 > self->width - 1 ? self->width - 1 : x1, cy1 = y1 > self->height - 1 ? self->height - 1 : y1;
    auto snapshot = [&](std::vector<std::uint32_t>& out) {
        out.clear();
        for (int y = cy0; y <= cy1; ++y)
            for (int x = cx0; x <= cx1; ++x) out.push_back(self->cells[y][x].passable);
    };
    std::vector<std::uint32_t> before, expected, actual;
    snapshot(before);
    s_originalClear(self, nullptr, level, dx, dy, x0, x1, y0, y1);
    snapshot(expected);
    std::size_t i = 0;
    for (int y = cy0; y <= cy1; ++y)
        for (int x = cx0; x <= cx1; ++x) self->cells[y][x].passable = before[i++];
    self->ClearUnmarkedArea(level, dx, dy, x0, x1, y0, y1);
    snapshot(actual);
    int differences = 0;
    for (std::size_t k = 0; k < actual.size(); ++k)
        if (actual[k] != expected[k]) ++differences;
    ei::verify::Check("CAIMap::ClearUnmarkedArea", 0, differences, "level %d {%d,%d} [%d,%d]x[%d,%d]", level, dx, dy, x0,
                      x1, y0, y1);
}
EI_VERIFY(0x005B89F0, ClearUnmarkedAreaVerify, 6, s_originalClear);

// 005C8D40: reallocates, so ours only: afterwards the grid covers the asked square and holds
// exactly the marks it held before (at the same map cells).
static void __fastcall GrowVerify(VisibilityGrid* self, void*, int x, int y, int n) {
    std::vector<std::pair<int, int>> marked;
    bool first = self->originX == 0x7FFFFFFF;
    if (!first)
        for (int row = 0; row < self->height; ++row)
            for (int col = 0; col < self->width; ++col)
                if (self->rows[row][col]) marked.emplace_back(self->originX + col, self->originY + row);
    self->Grow(x, y, n);
    bool ok = x - n >= self->originX && y - n >= self->originY && x + n <= self->originX + self->width - 1 &&
              y + n <= self->originY + self->height - 1;
    std::size_t count = 0;
    for (int row = 0; row < self->height; ++row)
        for (int col = 0; col < self->width; ++col)
            if (self->rows[row][col]) ++count;
    ok = ok && count == marked.size();
    for (const auto& m : marked)
        if (!self->rows[m.second - self->originY][m.first - self->originX]) ok = false;
    ei::verify::Check("VisibilityGrid::Grow", true, ok, "(%d,%d) n %d: %d marks", x, y, n, static_cast<int>(marked.size()));
}
EI_VERIFY(0x005C8D40, GrowVerify, 0, s_unused);
