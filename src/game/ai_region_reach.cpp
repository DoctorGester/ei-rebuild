// CRegionMap::Reaches (005B07C0): whether cell (x, y) can reach a region, for a size class. The
// region map keeps per size class a u16 region per 8x8 block (+0x220 + 16 * class, rows) and the
// block's representative cell (+0x1A0 + 16 * class, rows of bytes: low nibble x, high nibble y in
// the block). When none of the 3x3 blocks around the cell's block is in the region, the cell's
// block is marked unreachable (2) in `marks`; otherwise the local search (+0x98) floods the 3x3
// blocks from the cell, the region is reached when a representative cell of one of them that is
// in the region was reached, and every reached cell of the window gets the answer (1 yes, 2 no).
// See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"

#include <cstdint>

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

constexpr int kUnreached = 0x7FFFFFFF;
constexpr int kBlocksWide = 0x124;
constexpr int kBlocksHigh = 0x128;

std::uint8_t** MarkRows(void* marks) { return At<std::uint8_t**>(marks, 4); }

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

EI_REPLACE_METHOD(0x005B07C0, CRegionMap::ReachesRegion);
