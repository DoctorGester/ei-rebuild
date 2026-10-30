// The region map's block searches (BlockSearch, ai_map.h): Dijkstra over the 8x8-cell blocks of one size
// class along the edge words, open records in eight buckets by x & 7 sorted by cost. And the region map's
// block update, which re-measures the blocks after the AI map changed. See docs/notes/ai.md.
#include "ai_map.h"

#include "ai_unit_layer.h"
#include "unit_ai.h"
#include "world_data.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace {
constexpr int kUnreached = 0x7FFFFFFF;

// Progress steps of UpdateBlocks: the representative cells take the first eighth, the edges the rest.
constexpr float kFirstPassShare = 0.125f;
constexpr float kSecondPassShare = 0.875f;
} // namespace

void BlockSearch::Attach(RegionMap* regionMap, bool fromTargetSide) {
    int width = regionMap->edges[0].width;
    int height = regionMap->edges[0].height;
    map = regionMap;
    fromTarget = fromTargetSide;
    records.Resize(width, height);
    for (int y = 0; y < records.height; ++y)
        for (int x = 0; x < records.width; ++x) {
            BlockRecord& r = records[y][x];
            r.settled = false;
            r.cost = kUnreached;
            r.x = static_cast<std::int16_t>(x);
            r.y = static_cast<std::int16_t>(y);
            r.next = nullptr;
            r.prev = nullptr;
            r.backDirection = 0;
        }
    touched = nullptr;
    active = false;
}

void BlockSearch::Requeue(BlockRecord* record) {
    BlockRecord*& bucket = open[record->x & 7];
    BlockRecord* head = bucket;
    if (record == head) return;
    if (record->next) record->next->prev = record->prev;
    if (record->prev) record->prev->next = record->next;
    record->next = nullptr;
    record->prev = nullptr;
    if (!head) {
        bucket = record;
        record->next = nullptr;
        return;
    }
    if (head->cost >= record->cost) {
        bucket = record;
        record->next = head;
        head->prev = record;
        return;
    }
    BlockRecord* after = head;
    while (after->next && after->next->cost < record->cost) after = after->next;
    record->next = after->next;
    if (after->next) after->next->prev = record;
    after->next = record;
    record->prev = after;
}

void BlockSearch::Relax(int direction, int back) {
    int bx = best->x, by = best->y;
    int edge = (*edges)[by][bx * 8 + direction - 1];
    if (edge < 0) return;
    BlockRecord* record = &records[by + kDirectionDy[direction]][bx + kDirectionDx[direction]];
    if (bestCost >= record->cost) return;
    int cost = bestCost + edge;
    if (cost >= record->cost) return;
    record->cost = cost;
    record->backDirection = back;
    Requeue(record);
}

void BlockSearch::PickBest() {
    best = nullptr;
    int cost = kUnreached;
    for (BlockRecord* head : open)
        if (head && head->cost < cost) {
            best = head;
            cost = head->cost;
        }
}

void BlockSearch::Expand() {
    BlockRecord* record = best;
    record->settled = true;
    BlockRecord* next = record->next;
    open[record->x & 7] = next;
    if (next) next->prev = nullptr;
    record->next = touched;
    touched = record;
    bestCost = record->cost;
    for (int direction = 1; direction <= 8; ++direction) Relax(direction, (direction + 3) % 8 + 1);
    PickBest();
}

bool RegionMap::SeedSearch(int sizeClass, bool flag, BlockSearch* search, int x, int y) {
    bool seeded = false;
    for (const BlockCost& node : NearbyBlocks(sizeClass, flag, x, y)) {
        int cost = node.cost / 128;
        BlockRecord* record = &search->records[node.y][node.x];
        if (cost > record->cost) continue;
        record->cost = cost;
        search->Requeue(record);
        seeded = true;
    }
    search->PickBest();
    return seeded;
}

void RegionMap::UpdateBlocks(AIMap* aiMap, int x0, int x1, int y0, int y1) {
    if (y0 < 0) y0 = 0;
    if (edges[0].height <= y1) y1 = edges[0].height;
    if (x0 < 0) x0 = 0;
    if (edges[0].width <= x1) x1 = edges[0].width;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) PickRepresentatives(aiMap, x, y);
        if (progress) progress(static_cast<float>(y) / static_cast<float>(edges[0].height) * kFirstPassShare);
    }
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) MeasureBlockEdges(x, y);
        if (progress)
            progress(static_cast<float>(y) / static_cast<float>(edges[0].height) * kSecondPassShare + kFirstPassShare);
    }
    ClampEdgeCosts();
    AttachSearches();
    LabelRegions();
    progress = nullptr;
}

void RegionMap::UpdateBlocksFor(AIMap* aiMap, int x0, int x1, int y0, int y1) {
    UpdateBlocks(aiMap, x0, x1, y0, y1);
}

void RegionMap::AttachSearches() {
    forward.Attach(this, false);
    backward.Attach(this, true);
}

// --- Ai grids ----------------------------------------------------------------------------------------

// The small constructors of the AI map's members: the local and block searches, the event table, the dirty
// rectangle's reset; and the terrain types' open-cell marks. See ai_map.h and
// docs/notes/ai.md.

extern "C" int _purecall();

// ---- Constructors ---------------------------------------------------------------------------------

LocalSearch::LocalSearch() {
    nodes.Resize(1, 1);
    levels.Resize(1, 1);
}

BlockSearch::BlockSearch() { records.Resize(1, 1); }

void AIDirtyRect::Reset() {
    minX = 10000;
    maxX = 0;
    minY = 10000;
    maxY = 0;
}

// ---- The event table ------------------------------------------------------------------------------

AIEvent* g_freeEvents = /*007C53F0*/ {nullptr};

namespace {

// The mark table's rows are 16 wide; column 10 is the open-cell mark of a cost band.
int OpenMark(int band) { return static_cast<int>(worlddata::kMarkTable[band * 16 + 10]); }

// The cost bands: 0 or 1, 2, 3..9, 10..34, 35..70 (above: no mark).
int CostBand(int cost) {
    if (cost >= 10) return cost >= 35 ? 4 : 3;
    if (cost >= 3) return 2;
    return cost >= 2 ? 1 : 0;
}

} // namespace

AIEventTable::AIEventTable() { Clear(); }

AIEventTable::~AIEventTable() {
    for (auto& slot : slots) {
        for (AIEvent* head : slot) {
            for (AIEvent* event = head; event; event = event->nextInSlot) {
                event->next = g_freeEvents;
                g_freeEvents = event;
            }
        }
    }
}

void AIEventTable::Clear() {
    tick = -1;
    std::memset(slots, 0, sizeof(slots));
    width = 1;
    height = 1;
    cells.assign(1, AIEventCell{});
}

// ---- Terrain types --------------------------------------------------------------------------------

void AIMap::BuildOpenMarks() {
    for (int i = 0; i < std::ssize(materials); ++i) {
        std::uint32_t packed = 0;
        for (int sizeClass = 0; sizeClass < 8; ++sizeClass) {
            // The smallest size class is marked as if every type cost 1. (The original also
            // scales a per-class height here and drops the result.)
            int cost = sizeClass == 0 ? 1 : materials[i].cost;
            int mark = cost > 70 ? 0 : OpenMark(CostBand(cost));
            packed += static_cast<std::uint32_t>(mark) << (sizeClass * 4);
        }
        materials[i].openMarks = packed;
    }
}
