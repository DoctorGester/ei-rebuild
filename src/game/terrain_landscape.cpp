// The landscape object's own members (Terrain, terrain.h; 006C5430-006CCF20) and the aligned-array
// instantiations next to them (006CD5C0-006CE890). Sector loading and building are in
// terrain_sector.cpp, height queries in terrain.cpp.
#include "terrain.h"
#include "terrain_sector.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"

#include <cstdint>

using terrain::SectorData;

// 006BF8A0: the sector's destructor body (not rewritten).
__declspec(naked) void SectorData::Destruct() EI_ORIGINAL(0x006BF8A0)

namespace {

// A sector bound: FISTP of the value stored as a float (the original stores it, then loads it back),
// divided by 32 towards zero, clamped to 0..count - 1 in this order (a map without sectors gives -1).
int SectorBound(float value, int count) {
    int sector = x87::fistp(value) / 32;
    if (sector < 0) return 0;
    if (sector > count - 1) return count - 1;
    return sector;
}

// How many frames an unused sector stays cached before a load may reuse or free it.
constexpr int kSectorAge = 240;

void DeleteSector(SectorData* sector) {
    sector->Destruct();
    gamecrt::operator_delete(sector);
}

} // namespace

TerrainSector* Terrain::SectorAt(int sx, int sy, std::uint32_t wanted, int create) {
    if (sx < 0 || sy < 0 || sx >= sectorsX || sy >= sectorsY) return nullptr;
    auto* fields = reinterpret_cast<terrain::TerrainFields*>(this);
    SectorCache::Node* node = sectorGrid[sx][sy];
    if (node) {
        SectorData* sector = node->data;
        if (sector->lastUsed < frame) {
            sector->lastUsed = frame;
            sectorCache.RemoveAt(node);
            sectorCache.AddHead(sector);
            sectorGrid[sx][sy] = sectorCache.head;
        }
        if (create) sector->Ensure(wanted, fields);
        return (sector->built & wanted) ? reinterpret_cast<TerrainSector*>(sector) : nullptr;
    }
    if (!create) return nullptr;

    // A new sector, or the least recently used one when it is old enough; then every further
    // sector at the cache's end that is old enough is freed.
    SectorData* sector;
    SectorData* last = sectorCache.count != 0 ? sectorCache.tail->data : nullptr;
    if (!last || frame - kSectorAge <= last->lastUsed) {
        auto* memory = static_cast<SectorData*>(gamecrt::operator_new(sizeof(SectorData)));
        sector = memory ? memory->Construct() : nullptr;
    } else {
        sector = last;
        sectorGrid[sector->sx][sector->sy] = nullptr;
        sectorCache.RemoveTail();
        while (sectorCache.count != 0 && sectorCache.tail->data->lastUsed < frame - kSectorAge) {
            SectorData* old = sectorCache.RemoveTail();
            sectorGrid[old->sx][old->sy] = nullptr;
            if (old) DeleteSector(old);
        }
    }
    if (sector->Load(part15C, sx, sy, pages, perPage, textF8, fields)) {
        sector->lastUsed = frame;
        sectorCache.AddHead(sector);
        sectorGrid[sx][sy] = sectorCache.head;
        sector->Ensure(wanted, fields);
        return (sector->built & wanted) ? reinterpret_cast<TerrainSector*>(sector) : nullptr;
    }
    if (sector) DeleteSector(sector);
    return nullptr;
}
EI_REPLACE_METHOD(0x006C8140, Terrain::SectorAt);

void Terrain::SetVisibleSectors(float x, float y, float radius) {
    int columnFirst = SectorBound(x - radius - 0.5f, sectorsX);
    int columnLast = SectorBound(x + radius - 0.5f, sectorsX);
    int rowFirst = SectorBound(y - radius - 0.5f, sectorsY);
    int rowLast = SectorBound(y + radius - 0.5f, sectorsY);
    int rows = rowLast - rowFirst + 1;
    if (rows < 0) rows = 0;
    visibleColumnFirst.SetSize(rows);
    visibleColumnLast.SetSize(rows);
    for (int row = 0; row < rows; ++row) {
        visibleColumnFirst[row] = columnFirst;
        visibleColumnLast[row] = columnLast;
    }
    visibleRowFirst = rowFirst;
}
EI_REPLACE_METHOD(0x006CA260, Terrain::SetVisibleSectors);

// ---- Aligned arrays of 4-byte elements (lib::AlignedArray) ----------------------------------------
// SetSize(count, growBy) (RET 8): 006CD5C0 (the client setup's array, from 006C7750), 006CE890 (the
// light countdown's sorted sectors, 006C8ED0). SetAtGrow(index, value) (RET 8): 006CDD50 /
// 006CDE90 (the map loader's tables, 006C5AA0).
EI_REPLACE_METHOD(0x006CD5C0, lib::AlignedArray<std::uint32_t>::SetSize);
EI_REPLACE_METHOD(0x006CE890, lib::AlignedArray<std::uint32_t>::SetSize);
EI_REPLACE_METHOD(0x006CDD50, lib::AlignedArray<std::uint32_t>::SetAtGrow);
EI_REPLACE_METHOD(0x006CDE90, lib::AlignedArray<std::uint32_t>::SetAtGrow);
