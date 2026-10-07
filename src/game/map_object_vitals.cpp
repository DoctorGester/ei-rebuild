// CMapObject: the server companion's vitals (A-U5). See map_object.h and docs/notes/map_objects.md.
#include "map_object.h"

#include "unit.h"

#include "ei/hook.h"

namespace {
constexpr int kClassMapObjectServer = 0x39;
} // namespace

void CMapObject::ClearProtectionBases() {
    if (companion->GetClassId() != kClassMapObjectServer) return;
    // The original reloads the companion pointer for each store (two stores per pair, both +0).
    for (UnitStatPair& pair : static_cast<CMapObjectServer*>(companion)->vitals.protection) pair.base = 0.0f;
}
EI_REPLACE_METHOD(0x0050F080, CMapObject::ClearProtectionBases);
