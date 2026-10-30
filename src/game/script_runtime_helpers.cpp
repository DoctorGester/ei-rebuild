// The AI map's sector map wrappers the script commands reach.
#include "ai_map.h"
#include "sector_map.h"

#include <list>

// ---- The AI map's sector map ------------------------------------------------------------------------

namespace {
SectorMap* Sectors(AIMap* map) { return &map->sectorMap; }
} // namespace

std::uint8_t AIMap::SetRefreshUnits(int on) {
    std::uint8_t& flag = refreshUnits;
    std::uint8_t old = flag;
    flag = static_cast<std::uint8_t>(on);
    return old;
}

bool AIMap::AddToSectors(MapObject* object) { return Sectors(this)->Add(object); }

bool AIMap::RemoveFromSectors(MapObject* object) { return Sectors(this)->Remove(object); }

bool AIMap::MoveInSectors(MapObject* object, const Vector3& position) {
    return Sectors(this)->Move(object, position);
}

std::vector<Unit*> AIMap::FindUnitsInRadius(const Vector3& position, float radius) {
    return Sectors(this)->FindUnitsInRadius(position, radius);
}

