// A unit's path in the save stream (thiscall on the AIPath, RET 4): 005B2430 loads, 005B2570
// saves. The stream's slot 1 reads and slot 2 writes (buffer, size). Layout: the cell count and
// each remaining cell (8 bytes), the order (005C8230 / 005C8370), the target (8 bytes), the goal
// (12), the four flag bytes one by one, the excluded regions' count and each {x, y} as two
// dwords, and the smoothing value. Loading appends the cells to the list (created as {0, 0} by
// 005BD2A0, then read over). See ai_map.h (AIPath).
#include "ai_map.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>
#include "originals_library.h"

namespace {

// 005BD2A0 (RET 4): appends a copy of a cell. 005BD340 (RET 4): GetNext (the data, the position
// advanced).
__declspec(naked) void __fastcall AppendCell(PathCellList*, void*, const PathCell*) EI_ORIGINAL(0x005BD2A0)
__declspec(naked) PathCell* __fastcall NextCell(PathCellList*, void*, PathCellList::Node**) EI_ORIGINAL(0x005BD340)
// The order's load / save (RET 4).
__declspec(naked) void __fastcall OrderLoad(AIPath*, void*, void*) EI_ORIGINAL(0x005C8230)
__declspec(naked) void __fastcall OrderSave(AIPath*, void*, void*) EI_ORIGINAL(0x005C8370)
// 005B16D0 (RET 8): the excluded regions' SetSize.
inline void __fastcall ExcludedSetSize(void* a, void*, int n, int g) { PointArraySetSizeOriginal(a, nullptr, n, g); }

void Read(void* stream, void* buffer, int size) { ei::VCall<void>(stream, 1, buffer, size); }
void Write(void* stream, const void* buffer, int size) { ei::VCall<void>(stream, 2, buffer, size); }

struct Excluded {
    void* vtable;
    int* data;   // {x, y} pairs
    int size;
};

} // namespace

void __fastcall LoadPath(AIPath* path, void*, void* stream) {
    int count;
    Read(stream, &count, 4);
    for (int i = 0; i < count; ++i) {
        // The original's cell has an uninitialised second dword; it is read over below.
        PathCell cell{0, 0, 0, 0};
        AppendCell(&path->cells, nullptr, &cell);
    }
    for (PathCellList::Node* position = path->cells.head; position;)
        Read(stream, NextCell(&path->cells, nullptr, &position), 8);
    OrderLoad(path, nullptr, stream);
    auto* base = reinterpret_cast<std::uint8_t*>(path);
    Read(stream, base + 0x58, 8);
    Read(stream, base + 0x60, 0xC);
    Read(stream, base + 0x6C, 1);
    Read(stream, base + 0x6D, 1);
    Read(stream, base + 0x6E, 1);
    Read(stream, base + 0x6F, 1);
    auto* excluded = reinterpret_cast<Excluded*>(base + 0x70);
    Read(stream, &count, 4);
    ExcludedSetSize(excluded, nullptr, count, -1);
    for (int i = 0; i < excluded->size; ++i) {
        Read(stream, &excluded->data[i * 2], 4);
        Read(stream, &excluded->data[i * 2 + 1], 4);
    }
    Read(stream, base + 0x84, 4);
}

void __fastcall SavePath(AIPath* path, void*, void* stream) {
    int count = path->cells.count;
    Write(stream, &count, 4);
    for (PathCellList::Node* position = path->cells.head; position;)
        Write(stream, NextCell(&path->cells, nullptr, &position), 8);
    OrderSave(path, nullptr, stream);
    auto* base = reinterpret_cast<std::uint8_t*>(path);
    Write(stream, base + 0x58, 8);
    Write(stream, base + 0x60, 0xC);
    Write(stream, base + 0x6C, 1);
    Write(stream, base + 0x6D, 1);
    Write(stream, base + 0x6E, 1);
    Write(stream, base + 0x6F, 1);
    auto* excluded = reinterpret_cast<Excluded*>(base + 0x70);
    count = excluded->size;
    Write(stream, &count, 4);
    for (int i = 0; i < excluded->size; ++i) {
        int x = excluded->data[i * 2];
        Write(stream, &x, 4);
        int y = excluded->data[i * 2 + 1];
        Write(stream, &y, 4);
    }
    int smoothing = path->smoothing;
    Write(stream, &smoothing, 4);
}

EI_REPLACE(0x005B2430, LoadPath);
EI_REPLACE(0x005B2570, SavePath);
