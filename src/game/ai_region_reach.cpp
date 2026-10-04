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
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>

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
__declspec(naked) void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)

// The local search at +0x98: set up over a window (size class, x0, y0, x1, y1 exclusive, start
// x, y), flood (RET 4), finish, settle, the cost of a cell (0x7FFFFFFF unreached), reset.
__declspec(naked) void __fastcall SearchSetup(void*, void*, int, int, int, int, int, int, int) EI_ORIGINAL(0x005C2CF0)
__declspec(naked) void __fastcall SearchFlood(void*, void*, int) EI_ORIGINAL(0x005C33F0)
__declspec(naked) void __fastcall SearchFinish(void*, void*) EI_ORIGINAL(0x005C30E0)
__declspec(naked) void __fastcall SearchSettle(void*, void*) EI_ORIGINAL(0x005C3530)
__declspec(naked) int __fastcall SearchCost(void*, void*, int, int) EI_ORIGINAL(0x005C3090)
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

EI_REPLACE_METHOD(0x005B07C0, CRegionMap::ReachesRegion);
EI_REPLACE_METHOD(0x005B0F70, CRegionMap::ClampEdgeCosts);
EI_REPLACE_METHOD(0x005B0370, CRegionMap::NearbyBlocks);
EI_REPLACE_METHOD(0x005B0050, CRegionMap::LabelRegions);
