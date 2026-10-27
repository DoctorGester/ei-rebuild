// CUnit::TakePrototype: a unit's packed vitals and parameters as its prototype gives them (the zone loader,
// before the object is placed). See unit.h.
#include "unit.h"

#include "map_object.h"
#include "unit_create.h"

#include "ei/hook.h"

void CUnit::TakePrototype(const char* prototype) {
    // The stack copies start uninitialised; the resets and the builder (its defaults too, for an unknown
    // prototype) write every field the packers read.
    UnitVitals vitals{};
    UnitStats stats{};
    vitals.Reset();
    stats.Reset();
    MapObjectManagerOf(this)->BuildFromPrototype(&vitals, &stats, prototype);
    vitals.Pack(&packedVitals);
    stats.Pack(nullptr, Params());
}
EI_REWRITTEN(0x0052EB70, CUnit::TakePrototype);
