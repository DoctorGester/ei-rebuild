// The per-cell occluder words (CAIMap + 0x26AC): adding (005B61B0) and removing (005B6380) one
// footprint cell's occluder. A word holds one occluder inline: bit 8 set = none, bits 5-6 the
// transparency, 9-11 the kind, 12-21 the top and 22-31 the bottom height. With bit 7 set the
// occluders are a chain of 8-byte records ({u16 top, u16 bottom, u8 kind, u8 transparency, u16
// next}, +0x44 of the cell's 32x32 sector, 0x54-byte sectors at +0x138) whose head index sits in
// bits 9-28. See ai_map.h and docs/notes/ai.md.
#include "ai_map.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

struct OccluderRecord {
    std::uint16_t top;
    std::uint16_t bottom;
    std::uint8_t kind;
    std::uint8_t transparency;
    std::uint16_t next;
};
static_assert(sizeof(OccluderRecord) == 8);

// The sector's record pool (thiscall): a new record's index; remove the record matching
// (bottom, top, kind, transparency) from the chain at `head`, returning the new head (RET 0x14);
// free a record (RET 4).
__declspec(naked) int __fastcall NewRecord(void*) EI_ORIGINAL(0x005BFE70)
__declspec(naked) int __fastcall RemoveRecord(void*, void*, int, int, int, int, int) EI_ORIGINAL(0x005BFFD0)
__declspec(naked) void __fastcall FreeRecord(void*, void*, int) EI_ORIGINAL(0x005C0060)

constexpr std::uint32_t kNone = 0x100;
constexpr std::uint32_t kChained = 0x80;
constexpr std::uint32_t kKeepAroundIndex = 0xE00001FF;

std::uint8_t* SectorOf(CAIMap* map, int x, int y) {
    return map->sectors + ((y / 32) * map->sectorsPerRow + x / 32) * 0x54;
}
OccluderRecord* Records(std::uint8_t* sector) { return *reinterpret_cast<OccluderRecord**>(sector + 0x44); }
std::uint32_t WithIndex(std::uint32_t word, int index) {
    return (static_cast<std::uint32_t>(index & 0xFFFFF) << 9) | (word & kKeepAroundIndex);
}

} // namespace

bool CAIMap::AddOccluderRecord(FootprintCell* cell) {
    int x = cell->x, y = cell->y;
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    if (cell->low >= cell->high) return true;
    std::uint32_t& word = occluders[y][x];
    if (word & kNone) {
        std::uint32_t w = word & ~kNone;
        w = (w & 0xFFC00FFF) | (static_cast<std::uint32_t>(cell->high & 0x3FF) << 12);
        w = (w & 0x003FFFFF) | (static_cast<std::uint32_t>(cell->low) << 22);
        w = (w & ~0xE00u) | (static_cast<std::uint32_t>(cell->kind & 7) << 9);
        w = (w & ~0x60u) | (static_cast<std::uint32_t>(cell->transparency & 3) << 5);
        word = w;
        return true;
    }
    std::uint8_t* sector = SectorOf(this, x, y);
    if (!(word & kChained)) {
        // The inline occluder moves into the first record of a chain.
        int index = NewRecord(sector);
        std::uint32_t w = word;
        OccluderRecord& first = Records(sector)[index];
        first.top = static_cast<std::uint16_t>((w >> 12) & 0x3FF);
        first.bottom = static_cast<std::uint16_t>(w >> 22);
        first.kind = static_cast<std::uint8_t>((w >> 9) & 7);
        first.transparency = static_cast<std::uint8_t>((w >> 5) & 3);
        first.next = 0xFFFF;
        word = WithIndex(w, index) | kChained;
    }
    int index = NewRecord(sector);
    OccluderRecord& record = Records(sector)[index];
    record.top = static_cast<std::uint16_t>(cell->high);
    record.bottom = static_cast<std::uint16_t>(cell->low);
    record.kind = static_cast<std::uint8_t>(cell->kind);
    record.transparency = static_cast<std::uint8_t>(cell->transparency);
    record.next = static_cast<std::uint16_t>(word >> 9);
    word = WithIndex(word, index);
    return true;
}

bool CAIMap::ClearOccluderRecord(FootprintCell* cell) {
    int x = cell->x, y = cell->y;
    if (x < 0 || y < 0 || x >= width || y >= height) return false;
    if (cell->low >= cell->high) return true;
    std::uint32_t& word = occluders[y][x];
    if (word & kNone) return false;
    if (!(word & kChained)) {
        word |= kNone;
        return true;
    }
    std::uint8_t* sector = SectorOf(this, x, y);
    int head = static_cast<int>((word >> 9) & 0xFFFFF);
    int index = RemoveRecord(sector, nullptr, head, cell->low, cell->high, cell->kind, cell->transparency);
    word = WithIndex(word, index);
    index = static_cast<int>((word >> 9) & 0xFFFFF);
    OccluderRecord& last = Records(sector)[index];
    if (last.next != 0xFFFF) return true;
    // One record left: back inline (bit 7 cleared with the transparency), the record freed.
    std::uint32_t w = word;
    w = (w & 0x003FFFFF) | (static_cast<std::uint32_t>(last.bottom & 0x3FF) << 22);
    w = (w & 0xFFC00FFF) | (static_cast<std::uint32_t>(last.top & 0x3FF) << 12);
    w = (w & ~0xE00u) | (static_cast<std::uint32_t>(last.kind & 7) << 9);
    w = (w & ~0xE0u) | (static_cast<std::uint32_t>(last.transparency & 3) << 5);
    word = w;
    FreeRecord(sector, nullptr, index);
    return true;
}

EI_REPLACE_METHOD(0x005B61B0, CAIMap::AddOccluderRecord);
EI_REPLACE_METHOD(0x005B6380, CAIMap::ClearOccluderRecord);
