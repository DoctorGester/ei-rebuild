// CMapObject: the server companion's vitals. See map_object.h and docs/notes/map_objects.md.
#include "map_object.h"

#include "unit.h"

namespace {
} // namespace

void CMapObject::ClearProtectionBases() {
    if (companion->GetClassId() != kClassMapObjectServer) return;
    // The original reloads the companion pointer for each store (two stores per pair, both +0).
    for (UnitStatPair& pair : ServerCompanion()->vitals.protection) pair.base = 0.0f;
}
