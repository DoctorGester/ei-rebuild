// The per-cell occluder words (CAIMap + 0x26AC): adding (005B61B0) and removing (005B6380) one
// footprint cell's occluder. A word holds one occluder inline: bit 8 set = none, bits 5-6 the
// transparency, 9-11 the kind, 12-21 the top and 22-31 the bottom height. With bit 7 set the
// occluders are a chain of 8-byte records (SectorOccluder, sector_map.h) of the cell's 32x32
// sector (0x54-byte sectors at +0x138) whose head index sits in bits 9-28. See ai_map.h and
// docs/notes/ai.md.
#include "ai_map.h"
#include "sector_map.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

constexpr std::uint32_t kNone = 0x100;
constexpr std::uint32_t kChained = 0x80;
constexpr std::uint32_t kKeepAroundIndex = 0xE00001FF;

Sector* SectorOf(CAIMap* map, int x, int y) {
    return &map->sectorMap.sectors.data[(y / 32) * map->sectorMap.width + x / 32];
}
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
    Sector* sector = SectorOf(this, x, y);
    if (!(word & kChained)) {
        // The inline occluder moves into the first record of a chain.
        int index = sector->NewOccluder();
        std::uint32_t w = word;
        SectorOccluder& first = sector->occluders[index];
        first.top = static_cast<std::int16_t>((w >> 12) & 0x3FF);
        first.bottom = static_cast<std::int16_t>(w >> 22);
        first.kind = static_cast<std::int8_t>((w >> 9) & 7);
        first.transparency = static_cast<std::int8_t>((w >> 5) & 3);
        first.next = -1;
        word = WithIndex(w, index) | kChained;
    }
    int index = sector->NewOccluder();
    SectorOccluder& record = sector->occluders[index];
    record.top = static_cast<std::int16_t>(cell->high);
    record.bottom = static_cast<std::int16_t>(cell->low);
    record.kind = static_cast<std::int8_t>(cell->kind);
    record.transparency = static_cast<std::int8_t>(cell->transparency);
    record.next = static_cast<std::int16_t>(word >> 9);
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
    Sector* sector = SectorOf(this, x, y);
    int head = static_cast<int>((word >> 9) & 0xFFFFF);
    int index = sector->RemoveOccluder(head, cell->low, cell->high, cell->kind, cell->transparency);
    word = WithIndex(word, index);
    index = static_cast<int>((word >> 9) & 0xFFFFF);
    SectorOccluder& last = sector->occluders[index];
    if (last.next != -1) return true;
    // One record left: back inline (bit 7 cleared with the transparency), the record freed.
    std::uint32_t w = word;
    w = (w & 0x003FFFFF) | (static_cast<std::uint32_t>(last.bottom & 0x3FF) << 22);
    w = (w & 0xFFC00FFF) | (static_cast<std::uint32_t>(last.top & 0x3FF) << 12);
    w = (w & ~0xE00u) | (static_cast<std::uint32_t>(last.kind & 7) << 9);
    w = (w & ~0xE0u) | (static_cast<std::uint32_t>(last.transparency & 3) << 5);
    word = w;
    sector->FreeOccluder(index);
    return true;
}

EI_REPLACE_METHOD(0x005B61B0, CAIMap::AddOccluderRecord);
EI_REPLACE_METHOD(0x005B6380, CAIMap::ClearOccluderRecord);
