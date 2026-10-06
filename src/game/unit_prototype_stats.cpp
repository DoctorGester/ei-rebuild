// CUnit::PackPrototypeStats (0052EB70, virtual slot 34): a unit's packed vitals and parameters as
// its prototype gives them (the zone loader, 005927C0, before the object is placed). See unit.h.
#include "unit.h"

#include "map_object.h"
#include "unit_create.h"

#include "ei/hook.h"

void CUnit::PackPrototypeStats(const char* prototype) {
    // The original's stack copies start uninitialised; the resets and the builder (its defaults
    // too, for an unknown prototype) write every field the packers read.
    UnitVitals vitals{};
    UnitStats stats{};
    vitals.Reset();
    stats.Reset();
    static_cast<UnitStatsBuilder*>(static_cast<void*>(owner_))->BuildFromPrototype(&vitals, &stats, prototype);
    vitals.Pack(MapObject()->PackedVitals());
    stats.Pack(nullptr, Params());
}

EI_REPLACE_METHOD(0x0052EB70, CUnit::PackPrototypeStats);
