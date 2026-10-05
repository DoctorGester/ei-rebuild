// CRegionMap::Reaches (005B07C0): whether cell (x, y) can reach a region, for a size class. The
// region map keeps per size class a u16 region per 8x8 block (+0x220 + 16 * class, rows) and the
// block's representative cell (+0x1A0 + 16 * class, rows of bytes: low nibble x, high nibble y in
// the block). When none of the 3x3 blocks around the cell's block is in the region, the cell's
// block is marked unreachable (2) in `marks`; otherwise the local search (+0x98) floods the 3x3
// blocks from the cell, the region is reached when a representative cell of one of them that is
// in the region was reached, and every reached cell of the window gets the answer (1 yes, 2 no).
//
// CRegionMap::LabelRegions (005B0050): numbers the connected areas of blocks per size class: each
// class's region grid is cleared, and every block still 0 starts a new region (1, 2, ...) flooded
// by 005AFF10 from a queue of blocks. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_ai.h"
#include "originals_library.h"
#include "originals_units.h"

#include <cstdint>
#include <cstring>
#include <initializer_list>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

struct BlockPoint {
    int x, y;
};

// The function-local static CArray<{int x, y}> at 007C2EE0 (data +4, size +8, capacity +0xC),
// guard bit 0 of 007C2EF4, constructed by 00557230, destroyed at exit by 005B0AE0.
struct PointArray {
    void* vtable;
    BlockPoint* data;
    int size;
    int capacity;
    int growBy;
};
PointArray& Blocks() { return *reinterpret_cast<PointArray*>(0x007C2EE0); }
std::uint8_t& BlocksGuard() { return *reinterpret_cast<std::uint8_t*>(0x007C2EF4); }

__declspec(naked) void __fastcall PointArrayConstruct(PointArray*, void*) EI_ORIGINAL(0x00557230)
__declspec(naked) void __fastcall PointArraySetSize(PointArray*, void*, int, int) EI_ORIGINAL(0x005B16D0)
__declspec(naked) int __cdecl GameAtExit(void (*)()) EI_ORIGINAL(0x006E727B)

// The local search at +0x98: set up over a window (size class, x0, y0, x1, y1 exclusive, start
// x, y), flood (RET 4), finish, settle, the cost of a cell (0x7FFFFFFF unreached), reset.
__declspec(naked) void __fastcall SearchSetup(void*, void*, int, int, int, int, int, int, int) EI_ORIGINAL(0x005C2CF0)
__declspec(naked) void __fastcall SearchFlood(void*, void*, int) EI_ORIGINAL(0x005C33F0)
__declspec(naked) void __fastcall SearchSettle(void*, void*) EI_ORIGINAL(0x005C3530)
__declspec(naked) void __fastcall SearchReset(void*, void*) EI_ORIGINAL(0x005C35F0)

// LabelRegions' function-local statics: the flood queue at 007C2F10 (guard bit 0 of 007C2F24,
// destroyed by 005B0360) and its second list at 007C2EF8 (bit 1, 005B0350).
PointArray& FloodQueue() { return *reinterpret_cast<PointArray*>(0x007C2F10); }
PointArray& FloodSecond() { return *reinterpret_cast<PointArray*>(0x007C2EF8); }
std::uint8_t& FloodGuard() { return *reinterpret_cast<std::uint8_t*>(0x007C2F24); }

// 005AFF10 (RET 0x10). Floods `label` from the queued blocks for a size class.
__declspec(naked) void __fastcall FloodRegion(CRegionMap*, void*, int, PointArray*, PointArray*, int) EI_ORIGINAL(0x005AFF10)

// The per size class region grid (+0x21C + 16 * class): u16 per block.
struct RegionGrid {
    std::uint16_t* data;
    std::uint16_t** rows;
    int width, height;
};

constexpr int kUnreached = 0x7FFFFFFF;
constexpr int kBlocksWide = 0x124;
constexpr int kBlocksHigh = 0x128;

std::uint8_t** MarkRows(void* marks) { return At<std::uint8_t**>(marks, 4); }

// 005B1C70 (RET 8): ConstructElements of the {x, y, cost} records (zero-fills `count`).
__declspec(naked) void __stdcall ConstructBlockCosts(void*, int) EI_ORIGINAL(0x005B1C70)

// The list's inlined AddTail (MFC 4.2 NewNode with the plex carved into the free list).
BlockCostList::Node* AppendBlockCost(BlockCostList* list) {
    using Node = BlockCostList::Node;
    if (!list->free) {
        mfc::CPlex*& blocks = reinterpret_cast<mfc::CPlex*&>(list->blocks);
        mfc::CPlex* block = mfc::PlexCreate(blocks, list->blockSize, sizeof(Node));
        Node* node = reinterpret_cast<Node*>(block + 1) + (list->blockSize - 1);
        for (int i = list->blockSize - 1; i >= 0; --i, --node) {
            node->next = list->free;
            list->free = node;
        }
    }
    Node* node = list->free;
    list->free = node->next;
    node->prev = list->tail;
    node->next = nullptr;
    ++list->count;
    ConstructBlockCosts(&node->x, 1);
    return node;
}
// The region map's constructor helpers: the local search's two member constructors (their counts
// preset to 1), the CRT's `eh vector constructor iterator` (array, size, count, ctor, dtor).
__declspec(naked) void __fastcall ConstructSearchPart1(void*) EI_ORIGINAL(0x005B1470)
__declspec(naked) void __fastcall ConstructSearchPart2(void*) EI_ORIGINAL(0x005B14C0)
__declspec(naked) void __stdcall VectorConstruct(void*, unsigned, int, void*, void*) EI_ORIGINAL(0x006E8693)

// 005C1EA0 (RET 8): the local search's set-up for a map (map, 0).
__declspec(naked) void __fastcall SearchAttach(void*, void*, CAIMap*, int) EI_ORIGINAL(0x005C1EA0)

// The region map's per size class grids are allocated for their size by 005B1550 (edge words,
// +0x11C), 005B15F0 (representative cells, +0x19C) and 005B1680 (regions, +0x21C). 005AED60
// (RET 8) rebuilds a derived table from the map (+0 and +0x4C, the second with 1).
__declspec(naked) void __fastcall AllocateEdges(void*) EI_ORIGINAL(0x005B1550)
__declspec(naked) void __fastcall AllocateCells(void*) EI_ORIGINAL(0x005B15F0)
__declspec(naked) void __fastcall AllocateRegions(void*) EI_ORIGINAL(0x005B1680)
__declspec(naked) void __fastcall RebuildTable(void*, void*, CRegionMap*, int) EI_ORIGINAL(0x005AED60)

// A grid {data, rows, width, height}; resized (freed and reallocated) when the size changes.
struct BlockGrid {
    void* data;
    void** rows;
    int width;
    int height;
};
void Fit(BlockGrid& grid, int width, int height, void (__fastcall* allocate)(void*)) {
    if (grid.width == width && grid.height == height) return;
    GameDelete(grid.data);
    GameDelete(grid.rows);
    grid.width = width;
    grid.height = height;
    allocate(&grid);
}
// 005AF7B0 (RET 0x14, AL): whether cell (x, y) of block (bx, by) suits the block for a size class
// (CRegionMap::CellSuitsBlock, called through its original address).
__declspec(naked) bool __fastcall CellSuitsBlock(CRegionMap*, void*, int, int, int, int, int) EI_ORIGINAL(0x005AF7B0)
// 005C2CF0 with its AL result: false when the search could not be set up.
__declspec(naked) bool __fastcall SearchSetupOk(void*, void*, int, int, int, int, int, int, int) EI_ORIGINAL(0x005C2CF0)
// The region map's two block searches (+0 from the start, +0x4C from the target): a search over
// the blocks of one size class. Its block records (24 bytes, rows at +4) keep a cost
// (0x7FFFFFFF unvisited) and a link; +0x18 chains the touched records, +0x1C eight open
// buckets, +0x3C the best open record, +0x49 the active flag; +0x10 points back at the map and +0x14 at the
// class's edge grid.
struct BlockRecord {
    int cost;
    BlockRecord* next;
    int unknown_08;
    int unknown_0C;
    int unknown_10;
    std::uint8_t unknown_14;
    std::uint8_t pad[3];
};
static_assert(sizeof(BlockRecord) == 24);
struct BlockSearch {
    std::uint32_t unknown_00;
    BlockRecord** rows;
    std::uint8_t unknown_08[8];
    std::uint8_t* map;
    void* edges;
    BlockRecord* touched;
    BlockRecord* open[8];
    BlockRecord* best;         // +0x3C the best open record (x, y as shorts at +0xC / +0xE)
    std::uint8_t unknown_40[9];
    bool active;
    std::uint8_t unknown_4A[2];
};
static_assert(sizeof(BlockSearch) == 0x4C);

// 005B0AF0 (RET 0x14, AL): seeds a search (size class, which side, the search, cell x, y).
// 005AF5F0 (RET 4, AL): joins the two searches into the region chain list.
__declspec(naked) bool __fastcall SeedBlockSearch(CRegionMap*, void*, int, int, BlockSearch*, int, int) EI_ORIGINAL(0x005B0AF0)
__declspec(naked) bool __fastcall JoinBlockSearchesAt(CRegionMap*, void*, void*) EI_ORIGINAL(0x005AF5F0)

// The block search (thiscall): expand one round; the path to block (x, y) into a list (RET 0xC).
__declspec(naked) void __fastcall ExpandBlockSearch(BlockSearch*) EI_ORIGINAL(0x005AF0C0)
__declspec(naked) void __fastcall BlockPathTo(BlockSearch*, void*, int, int, PathCellList*) EI_ORIGINAL(0x005AF1C0)
// The path list: a reversed copy into `out` (RET 4, returns out), append (RET 4), fix-up.
__declspec(naked) void __fastcall ChainFixUp(PathCellList*) EI_ORIGINAL(0x005C6000)

// 00539980: ConstructElements<PathCell> (zero-fills `count`).

void DestroyPathList(PathCellList& list) {
    list.vtable = reinterpret_cast<void*>(0x0073EDB0);
    list.count = 0;
    list.free = nullptr;
    list.tail = nullptr;
    list.head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(list.blocks));
}

void ResetChain(BlockRecord* record) {
    while (record) {
        BlockRecord* next = record->next;
        record->cost = 0x7FFFFFFF;
        record->next = nullptr;
        record->unknown_08 = 0;
        record->unknown_10 = 0;
        record->unknown_14 = 0;
        record = next;
    }
}
void PrepareSearch(BlockSearch& search, int sizeClass) {
    search.edges = search.map + sizeClass * 16 + 0x11C;
    for (BlockRecord*& bucket : search.open) bucket = nullptr;
    search.active = true;
    search.best = nullptr;
}
void Exclude(BlockSearch& search, int x, int y) {
    BlockRecord* record = &search.rows[y][x];
    if (record->cost < 0x7FFFFFFF) return;
    record->cost = -1;
    record->next = search.touched;
    search.touched = record;
}
void FinishSearch(BlockSearch& search) {
    ResetChain(search.touched);
    for (BlockRecord* bucket : search.open) ResetChain(bucket);
    search.touched = nullptr;
    search.active = false;
}
} // namespace

bool CRegionMap::ReachesRegion(int sizeClass, int x, int y, int region, void* marks) {
    int bx = x / 8, by = y / 8;
    if (At<int>(this, kBlocksWide) - 1 < bx) bx = At<int>(this, kBlocksWide) - 1;
    if (bx < 0) bx = 0;
    if (At<int>(this, kBlocksHigh) - 1 < by) by = At<int>(this, kBlocksHigh) - 1;
    if (by < 0) by = 0;

    PointArray& blocks = Blocks();
    if (!(BlocksGuard() & 1)) {
        BlocksGuard() |= 1;
        PointArrayConstruct(&blocks, nullptr);
        GameAtExit(reinterpret_cast<void (*)()>(0x005B0AE0));
    }
    if (blocks.data) {
        GameDelete(blocks.data);
        blocks.data = nullptr;
    }
    blocks.capacity = 0;
    blocks.size = 0;
    for (int yy = by - 1; yy < by + 2; ++yy) {
        if (yy < 0 || yy >= At<int>(this, kBlocksHigh)) continue;
        for (int xx = bx - 1; xx < bx + 2; ++xx) {
            if (xx < 0 || xx >= At<int>(this, kBlocksWide)) continue;
            int n = blocks.size;
            PointArraySetSize(&blocks, nullptr, n + 1, -1);
            blocks.data[n].x = xx;
            blocks.data[n].y = yy;
        }
    }

    std::uint16_t** regionRows = At<std::uint16_t**>(this, (sizeClass + 0x22) * 16);
    auto inRegion = [&](int xx, int yy) {
        return regionRows[yy][xx] == static_cast<std::uint16_t>(region);
    };
    bool any = false;
    for (int k = 0; k < blocks.size; ++k)
        if (inRegion(blocks.data[k].x, blocks.data[k].y)) {
            any = true;
            break;
        }
    if (!any) {
        for (int row = by * 8; row < by * 8 + 8; ++row)
            for (int col = bx * 8; col < bx * 8 + 8; ++col) MarkRows(marks)[row][col] = 2;
        return false;
    }

    int x0 = bx * 8 - 8, y0 = by * 8 - 8, x1 = bx * 8 + 16, y1 = by * 8 + 16;
    At<std::uint8_t>(this, 0x114) = 1;
    void* search = reinterpret_cast<std::uint8_t*>(this) + 0x98;
    SearchSetup(search, nullptr, sizeClass, x0, y0, x1, y1, x, y);
    SearchFlood(search, nullptr, 1);
    SearchFinish(search, nullptr);
    SearchSettle(search, nullptr);

    std::uint8_t** cellRows = At<std::uint8_t**>(this, (sizeClass + 0x1A) * 16);
    bool found = false;
    for (int k = 0; k < blocks.size; ++k) {
        int xx = blocks.data[k].x, yy = blocks.data[k].y;
        std::uint8_t cell = cellRows[yy][xx];
        if (SearchCost(search, nullptr, (cell & 0xF) + xx * 8, (cell >> 4) + yy * 8) < kUnreached && inRegion(xx, yy)) {
            found = true;
            break;
        }
    }
    std::uint8_t mark = found ? 1 : 2;
    for (int row = y0; row < y1; ++row)
        for (int col = x0; col < x1; ++col)
            if (SearchCost(search, nullptr, col, row) < kUnreached) MarkRows(marks)[row][col] = mark;
    SearchReset(search, nullptr);
    return found;
}

void CRegionMap::LabelRegions() {
    PointArray& queue = FloodQueue();
    PointArray& second = FloodSecond();
    if (!(FloodGuard() & 1)) {
        FloodGuard() |= 1;
        PointArrayConstruct(&queue, nullptr);
        GameAtExit(reinterpret_cast<void (*)()>(0x005B0360));
    }
    if (!(FloodGuard() & 2)) {
        FloodGuard() |= 2;
        PointArrayConstruct(&second, nullptr);
        GameAtExit(reinterpret_cast<void (*)()>(0x005B0350));
    }
    PointArraySetSize(&queue, nullptr, 0, -1);
    for (int sizeClass = 0; sizeClass < 8; ++sizeClass) {
        RegionGrid& grid = At<RegionGrid>(this, 0x21C + sizeClass * 16);
        std::memset(grid.data, 0, static_cast<std::size_t>(grid.height * grid.width * 2));
        int label = 1;
        for (int by = 0; by < At<int>(this, kBlocksHigh); ++by)
            for (int bx = 0; bx < At<int>(this, kBlocksWide); ++bx) {
                if (grid.rows[by][bx] != 0) continue;
                grid.rows[by][bx] = static_cast<std::uint16_t>(label);
                int n = queue.size;
                PointArraySetSize(&queue, nullptr, n + 1, -1);
                queue.data[n].x = bx;
                queue.data[n].y = by;
                FloodRegion(this, nullptr, sizeClass, &queue, &second, label);
                ++label;
            }
    }
}

void CRegionMap::NearbyBlocks(int sizeClass, bool flag, int x, int y, BlockCostList* out) {
    int bx = x / 8, by = y / 8;
    if (At<int>(this, kBlocksWide) - 1 < bx) bx = At<int>(this, kBlocksWide) - 1;
    if (bx < 0) bx = 0;
    if (At<int>(this, kBlocksHigh) - 1 < by) by = At<int>(this, kBlocksHigh) - 1;
    if (by < 0) by = 0;
    // The inlined RemoveAll.
    out->count = 0;
    out->free = nullptr;
    out->tail = nullptr;
    out->head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(out->blocks));
    out->blocks = nullptr;

    At<bool>(this, 0x114) = flag;
    void* search = reinterpret_cast<std::uint8_t*>(this) + 0x98;
    SearchSetup(search, nullptr, sizeClass, bx * 8 - 8, by * 8 - 8, bx * 8 + 16, by * 8 + 16, x, y);
    SearchFlood(search, nullptr, 1);
    SearchFinish(search, nullptr);
    SearchSettle(search, nullptr);
    std::uint8_t** cellRows = At<std::uint8_t**>(this, (sizeClass + 0x1A) * 16);
    for (int yy = by - 1; yy < by + 2; ++yy) {
        if (yy < 0 || yy >= At<int>(this, kBlocksHigh)) continue;
        for (int xx = bx - 1; xx < bx + 2; ++xx) {
            if (xx < 0 || xx >= At<int>(this, kBlocksWide)) continue;
            std::uint8_t cell = cellRows[yy][xx];
            int cost = SearchCost(search, nullptr, (cell & 0xF) + xx * 8, (cell >> 4) + yy * 8);
            if (cost >= kUnreached) continue;
            BlockCostList::Node* node = AppendBlockCost(out);
            node->x = xx;
            node->y = yy;
            node->cost = cost;
            if (out->tail) out->tail->next = node;
            else out->head = node;
            out->tail = node;
        }
    }
    SearchReset(search, nullptr);
}

void CRegionMap::ClampEdgeCosts() {
    // Per size class (+0x120 + 16 * class): rows of blocks of 8 signed words, one per direction
    // (offsets in the tables 0079110C / 00791130; direction d and (d - 4) & 7 are opposite).
    struct EdgeGrid {
        std::int16_t** rows;
        int width;
        int height;
        int unknown_0C;
    };
    auto grid = [this](int sizeClass) -> EdgeGrid& { return At<EdgeGrid>(this, 0x120 + sizeClass * 16); };
    const int* dx = reinterpret_cast<const int*>(0x0079110C);
    const int* dy = reinterpret_cast<const int*>(0x00791130);
    // No way out of the map: the directions leaving the first and last column and row are -1.
    for (int by = 0; by < At<int>(this, kBlocksHigh); ++by)
        for (int c = 0; c < 8; ++c) {
            std::int16_t* row = grid(c).rows[by];
            row[1] = row[2] = row[3] = -1;
            std::int16_t* last = row + grid(c).width * 8 - 8;
            last[5] = last[6] = last[7] = -1;
        }
    for (int bx = 0; bx < At<int>(this, kBlocksWide); ++bx)
        for (int c = 0; c < 8; ++c) {
            std::int16_t* first = grid(c).rows[0] + bx * 8;
            first[7] = first[0] = first[1] = -1;
            std::int16_t* last = grid(c).rows[grid(c).height - 1] + bx * 8;
            last[3] = last[4] = last[5] = -1;
        }
    // Each edge between neighbouring blocks takes the smaller of its two directions.
    for (int by = 0; by < At<int>(this, kBlocksHigh); ++by)
        for (int bx = 0; bx < At<int>(this, kBlocksWide); ++bx)
            for (int dir = 0; dir < 8; ++dir) {
                int nx = bx + dx[dir], ny = by + dy[dir];
                if (nx < 0 || ny < 0 || nx >= At<int>(this, kBlocksWide) || ny >= At<int>(this, kBlocksHigh)) continue;
                int opposite = (dir - 4) & 7;
                for (int c = 0; c < 8; ++c) {
                    std::int16_t& a = grid(c).rows[by][bx * 8 + dir];
                    std::int16_t& b = grid(c).rows[ny][nx * 8 + opposite];
                    std::int16_t m = a < b ? a : b;
                    a = m;
                    b = m;
                }
            }
}

bool CRegionMap::LoadBlocks(void* stream) {
    using ReadFn = void(__fastcall*)(void*, void*, void*, int);
    auto read = [stream](void* buffer, int size) {
        reinterpret_cast<ReadFn>((*static_cast<void***>(stream))[1])(stream, nullptr, buffer, size);
    };
    int width, height;
    read(&width, 4);
    read(&height, 4);
    if (width <= 0 || height <= 0 || width > 0x100 || height > 0x100) return false;
    for (int c = 0; c < 8; ++c) {
        BlockGrid& edges = At<BlockGrid>(this, 0x11C + c * 16);
        BlockGrid& cells = At<BlockGrid>(this, 0x19C + c * 16);
        BlockGrid& regions = At<BlockGrid>(this, 0x21C + c * 16);
        Fit(edges, width, height, &AllocateEdges);
        Fit(cells, width, height, &AllocateCells);
        Fit(regions, width, height, &AllocateRegions);
        for (int y = 0; y < At<int>(this, kBlocksHigh); ++y) {
            read(edges.rows[y], width * 16);
            read(cells.rows[y], width);
            read(regions.rows[y], width * 2);
        }
    }
    RebuildTable(this, nullptr, this, 0);
    RebuildTable(reinterpret_cast<std::uint8_t*>(this) + 0x4C, nullptr, this, 1);
    return At<bool>(stream, 5);
}

void CRegionMap::PickRepresentatives(CAIMap* map, int bx, int by) {
    for (int c = 0; c < 8; ++c) {
        std::uint8_t& cell = At<std::uint8_t**>(this, (c + 0x1A) * 16)[by][bx];
        cell = static_cast<std::uint8_t>((cell & 0xF3) | 3);
        cell = static_cast<std::uint8_t>((cell & 0x0F) | 0x30);
        int best = 0;
        bool found = false;
        // Square rings from the block's middle: [4, 4), [3, 5), [2, 6), [1, 7), [0, 8).
        for (int k = 8, grow = 0; k >= 0; k -= 2, grow += 2) {
            int lo = k / 2, hi = grow + k / 2;
            for (int yy = lo; yy < hi; ++yy)
                for (int xx = lo; xx < hi; ++xx) {
                    int x = bx * 8 + xx, y = by * 8 + yy;
                    if (x < 0 || y < 0 || x >= map->width || y >= map->height) continue;
                    int nibble = static_cast<int>((map->cells[y][x].passable >> (c * 4)) & 0xF);
                    if (nibble == 0) continue;
                    if (!(nibble > best) && found) continue;
                    bool suits = CellSuitsBlock(this, nullptr, c, bx, by, xx, yy);
                    if (!suits && found) continue;
                    found = suits;
                    best = nibble;
                    cell = static_cast<std::uint8_t>((cell & 0xF0) | (xx & 0xF));
                    cell = static_cast<std::uint8_t>((cell & 0x0F) | (yy << 4));
                }
        }
    }
}

bool CRegionMap::FindRegionChain(int sizeClass, int fromX, int fromY, int toX, int toY, void* chain, void* excluded) {
    BlockSearch& forward = At<BlockSearch>(this, 0);
    BlockSearch& backward = At<BlockSearch>(this, 0x4C);
    PrepareSearch(forward, sizeClass);
    PrepareSearch(backward, sizeClass);
    // The excluded blocks (CArray<{x, y}>: data +4, size +8) are marked visited in both.
    const BlockPoint* points = At<BlockPoint*>(excluded, 4);
    for (int i = 0; i < At<int>(excluded, 8); ++i) {
        Exclude(forward, points[i].x, points[i].y);
        Exclude(backward, points[i].x, points[i].y);
    }
    bool ok = SeedBlockSearch(this, nullptr, sizeClass, 0, &forward, fromX, fromY);
    if (ok) ok = SeedBlockSearch(this, nullptr, sizeClass, 1, &backward, toX, toY);
    if (ok) ok = JoinBlockSearchesAt(this, nullptr, chain);
    FinishSearch(forward);
    FinishSearch(backward);
    return ok;
}

int CRegionMap::DominantRegion(int sizeClass, int x, int y) {
    // The reachable blocks around the cell (through the original address, 005B0370).
    using NearbyFn = void(__fastcall*)(CRegionMap*, void*, int, bool, int, int, BlockCostList*);
    BlockCostList blocks{reinterpret_cast<void*>(0x0073FCFC), nullptr, nullptr, 0, nullptr, nullptr, 10};
    reinterpret_cast<NearbyFn>(0x005B0370)(this, nullptr, sizeClass, false, x, y, &blocks);
    // Count the blocks per region, in first-seen order (the original keeps a CList of {region,
    // count}; at most nine entries).
    std::uint16_t regions[9];
    int counts[9];
    int distinct = 0;
    std::uint16_t** rows = At<std::uint16_t**>(this, (sizeClass + 0x22) * 16);
    for (BlockCostList::Node* n = blocks.head; n; n = n->next) {
        std::uint16_t region = rows[n->y][n->x];
        int i = 0;
        while (i < distinct && regions[i] != region) ++i;
        if (i < distinct) {
            ++counts[i];
        } else if (distinct < 9) {
            regions[distinct] = region;
            counts[distinct++] = 1;
        }
    }
    int best = 0;
    std::uint16_t region = 0;
    for (int i = 0; i < distinct; ++i)
        if (counts[i] > best) {
            best = counts[i];
            region = regions[i];
        }
    // The block list's inlined destructor.
    blocks.vtable = reinterpret_cast<void*>(0x0073FCFC);
    blocks.count = 0;
    blocks.free = nullptr;
    blocks.tail = nullptr;
    blocks.head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(blocks.blocks));
    return region;
}

bool CRegionMap::JoinBlockSearches(PathCellList* chain) {
    BlockSearch& forward = At<BlockSearch>(this, 0);
    BlockSearch& backward = At<BlockSearch>(this, 0x4C);
    auto meets = [](BlockSearch& other, int x, int y) {
        const BlockRecord& r = other.rows[y][x];
        return r.unknown_14 != 0 && r.cost < 0x7FFFFFFF;
    };
    bool ok = true;
    int x = 0, y = 0;
    for (;;) {
        ExpandBlockSearch(&forward);
        ExpandBlockSearch(&backward);
        BlockRecord* best = forward.best;
        if (!best || best->cost == 0x7FFFFFFF) {
            ok = false;
            break;
        }
        x = *reinterpret_cast<std::int16_t*>(reinterpret_cast<std::uint8_t*>(best) + 0xC);
        y = *reinterpret_cast<std::int16_t*>(reinterpret_cast<std::uint8_t*>(best) + 0xE);
        if (meets(backward, x, y)) break;
        best = backward.best;
        if (!best || best->cost == 0x7FFFFFFF) {
            ok = false;
            break;
        }
        x = *reinterpret_cast<std::int16_t*>(reinterpret_cast<std::uint8_t*>(best) + 0xC);
        y = *reinterpret_cast<std::int16_t*>(reinterpret_cast<std::uint8_t*>(best) + 0xE);
        if (meets(forward, x, y)) break;
    }
    PathCellList scratch{reinterpret_cast<void*>(0x0073EDB0), nullptr, nullptr, 0, nullptr, nullptr, 0x80};
    if (ok) {
        BlockPathTo(&forward, nullptr, x, y, chain);
        BlockPathTo(&backward, nullptr, x, y, &scratch);
        PathCellList reversed;
        ListAppend(chain, nullptr, ListReversed(&scratch, nullptr, &reversed));
        DestroyPathList(reversed);
        ChainFixUp(chain);
    }
    DestroyPathList(scratch);
    return ok;
}

void __fastcall BlockSearchPathTo(void* searchIn, void*, int x, int y, PathCellList* path) {
    auto& search = *static_cast<BlockSearch*>(searchIn);
    // The inlined RemoveAll.
    path->count = 0;
    path->free = nullptr;
    path->tail = nullptr;
    path->head = nullptr;
    mfc::PlexFreeDataChain(static_cast<mfc::CPlex*>(path->blocks));
    path->blocks = nullptr;
    int columns = *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(&search) + 8);
    int rows = *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(&search) + 0xC);
    if (x < 0 || x >= columns || y < 0 || y >= rows) return;
    // The original builds each cell's second dword (flags) from a stack slot it never writes;
    // ours writes 0.
    auto cellAt = [](int cx, int cy) {
        PathCell c{static_cast<std::int16_t>(cx), static_cast<std::int16_t>(cy), 0, 0};
        return c;
    };
    // Walk back along the stored directions (+0x10: 1..8; 0 ends), each cell added at the head.
    while (int dir = search.rows[y][x].unknown_10) {
        PathCellList::Node* node = path->NewNode(nullptr, path->head);
        node->data = cellAt(x, y);
        if (path->head) path->head->prev = node;
        else path->tail = node;
        path->head = node;
        switch (dir) {
        case 1: --y; break;
        case 2: --y; --x; break;
        case 3: --x; break;
        case 4: ++y; --x; break;
        case 5: ++y; break;
        case 6: ++y; ++x; break;
        case 7: ++x; break;
        case 8: --y; ++x; break;
        default: break;
        }
    }
    // The first cell, through the inlined NewNode.
    if (!path->free) {
        mfc::CPlex*& blocks = reinterpret_cast<mfc::CPlex*&>(path->blocks);
        mfc::CPlex* block = mfc::PlexCreate(blocks, path->blockSize, sizeof(PathCellList::Node));
        auto* n = reinterpret_cast<PathCellList::Node*>(block + 1) + (path->blockSize - 1);
        for (int i = path->blockSize - 1; i >= 0; --i, --n) {
            n->next = path->free;
            path->free = n;
        }
    }
    PathCellList::Node* node = path->free;
    path->free = node->next;
    node->prev = nullptr;
    node->next = path->head;
    ++path->count;
    ConstructPathCells(&node->data, 1);
    node->data = cellAt(x, y);
    if (path->head) path->head->prev = node;
    else path->tail = node;
    path->head = node;
}

void CRegionMap::MeasureBlockEdges(void*, int bx, int by) {
    const int* dx = reinterpret_cast<const int*>(0x0079110C);
    const int* dy = reinterpret_cast<const int*>(0x00791130);
    void* search = reinterpret_cast<std::uint8_t*>(this) + 0x98;
    for (int c = 0; c < 8; ++c) {
        std::uint8_t** cells = At<std::uint8_t**>(this, 0x1A0 + c * 16);
        std::int16_t** edges = At<std::int16_t**>(this, 0x120 + c * 16);
        std::uint8_t own = cells[by][bx];
        At<std::uint8_t>(this, 0x114) = 0;
        SearchSetup(search, nullptr, c, bx * 8 - 8, by * 8 - 8, bx * 8 + 16, by * 8 + 16, (own & 0xF) + bx * 8,
                    (own >> 4) + by * 8);
        SearchFinish(search, nullptr);
        // Each direction: the search cost from the block's representative cell to the
        // neighbour's, in units of 128 (at most 0x7FFF); -1 off the map or unreached.
        for (int dir = 0; dir < 8; ++dir) {
            int nx = bx + dx[dir], ny = by + dy[dir];
            std::int16_t value = -1;
            if (nx >= 0 && ny >= 0 && nx < At<int>(this, kBlocksWide) && ny < At<int>(this, kBlocksHigh)) {
                std::uint8_t other = cells[ny][nx];
                int cost = SearchCost(search, nullptr, (other & 0xF) + nx * 8, (other >> 4) + ny * 8);
                if (cost < kUnreached) {
                    cost /= 128;
                    if (cost > 0x7FFF) cost = 0x7FFF;
                    value = static_cast<std::int16_t>(cost);
                }
            }
            edges[by][bx * 8 + dir] = value;
        }
        SearchReset(search, nullptr);
    }
}

void CRegionMap::Attach(CAIMap* map) {
    SearchAttach(reinterpret_cast<std::uint8_t*>(this) + 0x98, nullptr, map, 0);
    int width = map->width / 8, height = map->height / 8;
    for (int c = 0; c < 8; ++c) {
        Fit(At<BlockGrid>(this, 0x11C + c * 16), width, height, &AllocateEdges);
        Fit(At<BlockGrid>(this, 0x19C + c * 16), width, height, &AllocateCells);
        // The region grid's allocation is inlined here: u16 per block, rows of `width`.
        BlockGrid& regions = At<BlockGrid>(this, 0x21C + c * 16);
        if (regions.width == width && regions.height == height) continue;
        GameDelete(regions.data);
        GameDelete(regions.rows);
        regions.width = width;
        regions.height = height;
        auto* data = static_cast<std::uint8_t*>(gamecrt::operator_new(static_cast<std::size_t>(height * width * 2)));
        regions.data = data;
        regions.rows = static_cast<void**>(gamecrt::operator_new(static_cast<std::size_t>(regions.height * 4)));
        for (int y = 0; y < regions.height; ++y) regions.rows[y] = data + regions.width * y * 2;
    }
    RebuildTable(this, nullptr, this, 0);
    RebuildTable(reinterpret_cast<std::uint8_t*>(this) + 0x4C, nullptr, this, 1);
}

// 005AF3D0 (thiscall, returns this): the region map's constructor.
CRegionMap* __fastcall ConstructRegionMap(CRegionMap* self) {
    auto* base = reinterpret_cast<std::uint8_t*>(self);
    // The two block searches start with a 1x1 record grid.
    for (int offset : {0, 0x4C}) {
        std::uint8_t* search = base + offset;
        At<int>(search, 8) = 1;
        At<int>(search, 0xC) = 1;
        auto* records = static_cast<std::uint8_t*>(gamecrt::operator_new(0x18));
        At<std::uint8_t*>(search, 0) = records;
        auto** rows = static_cast<std::uint8_t**>(gamecrt::operator_new(static_cast<std::size_t>(At<int>(search, 0xC) * 4)));
        At<std::uint8_t**>(search, 4) = rows;
        for (int y = 0; y < At<int>(search, 0xC); ++y) rows[y] = At<std::uint8_t*>(search, 0) + y * At<int>(search, 8) * 24;
        At<std::uint8_t>(search, 0x49) = 0;
        At<std::uint32_t>(search, 0x10) = 0;
    }
    std::uint8_t* local = base + 0x98;
    At<int>(local, 4 + 8) = 1;
    At<int>(local, 4 + 0xC) = 1;
    ConstructSearchPart1(local + 4);
    At<int>(local, 0x14 + 8) = 1;
    At<int>(local, 0x14 + 0xC) = 1;
    ConstructSearchPart2(local + 0x14);
    At<std::uint32_t>(local, 0x28) = 0x0073FA04;
    At<std::uint32_t>(local, 0x2C) = 0;
    At<std::uint32_t>(local, 0x38) = 0;
    At<std::uint32_t>(local, 0x34) = 0;
    At<std::uint32_t>(local, 0x30) = 0;
    At<std::uint32_t>(local, 0x24) = 0;
    VectorConstruct(base + 0x11C, 0x10, 8, reinterpret_cast<void*>(0x005AF580), reinterpret_cast<void*>(0x00598440));
    VectorConstruct(base + 0x19C, 0x10, 8, reinterpret_cast<void*>(0x005AF590), reinterpret_cast<void*>(0x00598460));
    VectorConstruct(base + 0x21C, 0x10, 8, reinterpret_cast<void*>(0x005AF5A0), reinterpret_cast<void*>(0x00598480));
    RebuildTable(self, nullptr, self, 0);
    RebuildTable(base + 0x4C, nullptr, self, 1);
    At<std::uint32_t>(self, 0x118) = 0;
    return self;
}

bool CRegionMap::CellSuitsBlockAt(int sizeClass, int bx, int by, int xx, int yy) {
    void* search = reinterpret_cast<std::uint8_t*>(this) + 0x98;
    int left = bx * 8, top = by * 8, right = bx * 8 + 8, bottom = by * 8 + 8;
    // A search confined to the block and its far edges (9x9 cells) from the candidate cell; when
    // it cannot be set up the cell is taken.
    if (!SearchSetupOk(search, nullptr, sizeClass, left, top, right, bottom, xx + left, yy + top)) return true;
    SearchFinish(search, nullptr);
    // Suitable when it reaches both the left and the right edge, or else both the top and the
    // bottom edge.
    bool first = false, second = false;
    for (int y = top; y < top + 9; ++y) {
        if (SearchCost(search, nullptr, left, y) < kUnreached) first = true;
        if (SearchCost(search, nullptr, right, y) < kUnreached) second = true;
    }
    if (first && second) {
        SearchReset(search, nullptr);
        return true;
    }
    first = false;
    second = false;
    for (int x = left; x < left + 9; ++x) {
        if (SearchCost(search, nullptr, x, top) < kUnreached) first = true;
        if (SearchCost(search, nullptr, x, bottom) < kUnreached) second = true;
    }
    SearchReset(search, nullptr);
    return first && second;
}

void CRegionMap::FloodRegionFrom(int sizeClass, void* frontierIn, void* nextIn, int label) {
    auto* frontier = static_cast<PointArray*>(frontierIn);
    auto* next = static_cast<PointArray*>(nextIn);
    const int* dx = reinterpret_cast<const int*>(0x0079110C);
    const int* dy = reinterpret_cast<const int*>(0x00791130);
    std::int16_t** edges = At<std::int16_t**>(this, 0x120 + sizeClass * 16);
    std::uint16_t** regions = At<std::uint16_t**>(this, 0x220 + sizeClass * 16);
    for (;;) {
        // The inlined RemoveAll of the next frontier.
        if (next->data) {
            GameDelete(next->data);
            next->data = nullptr;
        }
        next->capacity = 0;
        next->size = 0;
        // Every open edge (word >= 0) of a frontier block leads to a neighbour; an unlabelled one
        // takes the label and joins the next frontier.
        for (int i = 0; i < frontier->size; ++i) {
            int bx = frontier->data[i].x, by = frontier->data[i].y;
            for (int dir = 0; dir < 8; ++dir) {
                if (edges[by][bx * 8 + dir] < 0) continue;
                int nx = bx + dx[dir], ny = by + dy[dir];
                std::uint16_t& region = regions[ny][nx];
                if (region != 0) continue;
                region = static_cast<std::uint16_t>(label);
                int n = next->size;
                PointArraySetSize(next, nullptr, n + 1, -1);
                next->data[n].x = nx;
                next->data[n].y = ny;
            }
        }
        if (next->size == 0) return;
        PointArray* swap = frontier;
        frontier = next;
        next = swap;
    }
}

EI_REPLACE_METHOD(0x005B07C0, CRegionMap::ReachesRegion);
EI_REPLACE_METHOD(0x005AFF10, CRegionMap::FloodRegionFrom);
EI_REPLACE_METHOD(0x005AF7B0, CRegionMap::CellSuitsBlockAt);
EI_REPLACE(0x005AF3D0, ConstructRegionMap);
EI_REPLACE_METHOD(0x005B0C00, CRegionMap::Attach);
EI_REPLACE_METHOD(0x005AFAE0, CRegionMap::MeasureBlockEdges);
EI_REPLACE(0x005AF1C0, BlockSearchPathTo);
EI_REPLACE_METHOD(0x005AF5F0, CRegionMap::JoinBlockSearches);
EI_REPLACE_METHOD(0x005B0600, CRegionMap::DominantRegion);
EI_REPLACE_METHOD(0x005B0D90, CRegionMap::FindRegionChain);
EI_REPLACE_METHOD(0x005AF900, CRegionMap::PickRepresentatives);
EI_REPLACE_METHOD(0x005B1180, CRegionMap::LoadBlocks);
EI_REPLACE_METHOD(0x005B0F70, CRegionMap::ClampEdgeCosts);
EI_REPLACE_METHOD(0x005B0370, CRegionMap::NearbyBlocks);
EI_REPLACE_METHOD(0x005B0050, CRegionMap::LabelRegions);
