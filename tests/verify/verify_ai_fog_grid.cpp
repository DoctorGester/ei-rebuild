// Checks for FogGrid (src/game/ai_fog_grid.cpp). Reached by the clairvoyance effect's Pack /
// Unpack / Reset (spells_hero, spells_cast).
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/effects.h"

#include <cstdint>
#include <vector>

namespace {
void* s_unused;

using WriteFn = void(__fastcall*)(FogGrid*, void*, CMemoryStream*);
WriteFn s_originalWrite;

// A stand-in stream whose slot 2 (Write) records the bytes.
std::vector<std::uint8_t>* s_bytes;
void __fastcall RecordWrite(void*, void*, const void* buffer, int size) {
    auto* p = static_cast<const std::uint8_t*>(buffer);
    s_bytes->insert(s_bytes->end(), p, p + size);
}
void* s_vtable[8];
struct StandIn {
    void** vtable;
};

// The rows point into the cells, one width apart.
bool Consistent(const FogGrid* grid) {
    if (!grid->cells || !grid->rows) return false;
    for (int row = 0; row < grid->height; ++row)
        if (grid->rows[row] != grid->cells + grid->width * row) return false;
    return true;
}
} // namespace

// 005C8B60: ours only (it frees and allocates). No origin, 1 x 1, the cell cleared.
static void __fastcall ClearVerify(FogGrid* grid, void*) {
    grid->Clear();
    bool ok = grid->x0 == 0x7FFFFFFF && grid->y0 == 0x7FFFFFFF && grid->width == 1 && grid->height == 1 &&
              Consistent(grid) && grid->cells[0] == 0;
    ei::verify::Check("FogGrid::Clear", true, ok, "%d x %d", grid->width, grid->height);
}
EI_VERIFY(0x005C8B60, ClearVerify, 0, s_unused);

// 005C8BF0: reads the real stream, so ours only: the rows match the size read.
static void __fastcall ReadVerify(FogGrid* grid, void*, CMemoryStream* stream) {
    grid->Read(stream);
    ei::verify::Check("FogGrid::Read", true, Consistent(grid), "%d x %d", grid->width, grid->height);
}
EI_VERIFY(0x005C8BF0, ReadVerify, 0, s_unused);

// 005C8CC0: PUSH ECX; PUSH EBX; PUSH EBP; PUSH ESI; MOV ESI,[ESP+0x14] (8 bytes). Differential: the
// original, then ours, into the stand-in stream; then ours into the real one.
static void __fastcall WriteVerify(FogGrid* grid, void*, CMemoryStream* stream) {
    std::vector<std::uint8_t> expected, actual;
    s_vtable[2] = reinterpret_cast<void*>(&RecordWrite);
    StandIn standIn{s_vtable};
    s_bytes = &expected;
    s_originalWrite(grid, nullptr, reinterpret_cast<CMemoryStream*>(&standIn));
    s_bytes = &actual;
    grid->Write(reinterpret_cast<CMemoryStream*>(&standIn));
    ei::verify::Check("FogGrid::Write", true, actual == expected, "%d x %d: %d bytes (original %d)", grid->width,
                      grid->height, static_cast<int>(actual.size()), static_cast<int>(expected.size()));
    grid->Write(stream);
}
EI_VERIFY(0x005C8CC0, WriteVerify, 8, s_originalWrite);
