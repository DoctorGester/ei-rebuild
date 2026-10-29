// The per-cell occluder words (AIMap::occluders, ai_map.h's namespace occluder): adding and removing one footprint
// cell's occluder. A cell with more than one chains 8-byte records (SectorOccluder, sector_map.h) of its 32x32
// sector. See docs/notes/ai.md.
#include "ai_map.h"
#include "sector_map.h"

#include "lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

Sector* SectorOf(AIMap* map, int x, int y) {
    return &map->sectorMap.sectors[(y / 32) * map->sectorMap.width + x / 32];
}

} // namespace

bool AIMap::AddOccluderRecord(FootprintCell* cell) {
    int x = cell->x, y = cell->y;
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    if (cell->low >= cell->high) return true;
    std::uint32_t& word = occluders[y][x];
    if (word & occluder::kNone) {
        std::uint32_t w = word & ~occluder::kNone;
        w = occluder::WithTop(w, cell->high);
        w = occluder::WithBottom(w, cell->low);
        w = occluder::WithKind(w, cell->kind);
        w = occluder::WithTransparency(w, cell->transparency);
        word = w;
        return true;
    }
    Sector* sector = SectorOf(this, x, y);
    if (!(word & occluder::kChained)) {
        int index = sector->NewOccluder();
        std::uint32_t w = word;
        SectorOccluder& first = sector->occluders[index];
        first.top = static_cast<std::int16_t>(occluder::Top(w));
        first.bottom = static_cast<std::int16_t>(occluder::Bottom(w));
        first.kind = occluder::Kind(w);
        first.transparency = static_cast<std::int8_t>(occluder::Transparency(w));
        first.next = -1;
        word = occluder::WithHead(w, index) | occluder::kChained;
    }
    int index = sector->NewOccluder();
    SectorOccluder& record = sector->occluders[index];
    record.top = static_cast<std::int16_t>(cell->high);
    record.bottom = static_cast<std::int16_t>(cell->low);
    record.kind = cell->kind;
    record.transparency = static_cast<std::int8_t>(cell->transparency);
    record.next = static_cast<std::int16_t>(word >> 9);   // the head index (bits 9-28) as a short
    word = occluder::WithHead(word, index);
    return true;
}

bool AIMap::ClearOccluderRecord(FootprintCell* cell) {
    int x = cell->x, y = cell->y;
    if (x < 0 || y < 0 || x >= cells.width || y >= cells.height) return false;
    if (cell->low >= cell->high) return true;
    std::uint32_t& word = occluders[y][x];
    if (word & occluder::kNone) return false;
    if (!(word & occluder::kChained)) {
        word |= occluder::kNone;
        return true;
    }
    Sector* sector = SectorOf(this, x, y);
    int head = occluder::Head(word);
    int index = sector->RemoveOccluder(head, cell->low, cell->high, cell->kind, cell->transparency);
    word = occluder::WithHead(word, index);
    index = occluder::Head(word);
    SectorOccluder& last = sector->occluders[index];
    if (last.next != -1) return true;
    // One record left: back inline (kChained cleared), the record freed.
    std::uint32_t w = word;
    w = occluder::WithBottom(w, last.bottom);
    w = occluder::WithTop(w, last.top);
    w = occluder::WithKind(w, last.kind);
    w = occluder::WithTransparency(w & ~occluder::kChained, last.transparency);
    word = w;
    sector->FreeOccluder(index);
    return true;
}
