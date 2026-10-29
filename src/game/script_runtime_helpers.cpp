// The AI map's sector map wrappers the script commands reach.
#include "ai_map.h"
#include "sector_map.h"

#include <list>

// ---- The AI map's sector map ------------------------------------------------------------------------

namespace {
CSectorMap* Sectors(CAIMap* map) { return &map->sectorMap; }
} // namespace

std::uint8_t CAIMap::SetRefreshUnits(int on) {
    std::uint8_t& flag = refreshUnits;
    std::uint8_t old = flag;
    flag = static_cast<std::uint8_t>(on);
    return old;
}

bool CAIMap::AddToSectors(CMapObject* object) { return Sectors(this)->Add(object); }

bool CAIMap::RemoveFromSectors(CMapObject* object) { return Sectors(this)->Remove(object); }

bool CAIMap::MoveInSectors(CMapObject* object, const Vector3& position) {
    return Sectors(this)->Move(object, position);
}

std::list<CUnit*> CAIMap::FindUnitsInRadius(const Vector3& position, float radius) {
    return Sectors(this)->FindUnitsInRadius(position, radius);
}

