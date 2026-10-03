#pragma once

// Unit setup helpers around creation and loading: the unit prototype table's name index and the
// server controller's statistics hand-over. Names provisional. See docs/notes/units.md
// ("Fourth round").

#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CUnit;
struct UnitStats;
struct UnitVitals;

// The unit prototype table (global 007AF358): an STLport vector of 0x120-byte records (item_db.h's
// RecordTable reads its begin / end) followed by a hash_map<string, record*> from prototype names
// to records at +0x0C.
struct UnitPrototypeTable {
    using NameIndex = stlp::hash_map<stlp::string, char*, stlp::StringHash>;

    // 00514930 (and its copy 005144D0). The record named `name`: operator[] on the name index,
    // so an unknown name is inserted with a null record (and null is returned).
    char* FindByName(const char* name);

    char* begin;                // +0x00
    char* end;                  // +0x04
    char* endOfStorage;         // +0x08
    NameIndex byName;           // +0x0C
};
static_assert(offsetof(UnitPrototypeTable, byName) == 0x0C);

// The NPC prototype table (global 007AF378; records of statistics for named creatures), laid
// out the same way.
struct NpcPrototypeTable {
    using NameIndex = stlp::hash_map<stlp::string, char*, stlp::StringHash>;

    // 005145D0. As UnitPrototypeTable::FindByName (its own instantiation, 00514A20).
    char* FindByName(const char* name);

    char* begin;
    char* end;
    char* endOfStorage;
    NameIndex byName;           // +0x0C
};
static_assert(offsetof(NpcPrototypeTable, byName) == 0x0C);

// The builders of a creature's starting statistics from its prototype (unit_create_stats.cpp).
// They are methods of an object (the map object manager, or the unit's owner) that they never
// use but pass on to each other.
struct UnitStatsBuilder {
    // 005134F0. Defaults for an object without a prototype: with `stats`, the statistics (stats
    // 50, rates, values and body slots at 100, no layers); without, the vitals (1000 hit points
    // and mana, regeneration 10, protection 100). Returns 1.
    int SetDefaults(UnitVitals* vitals, UnitStats* stats);

    // 005136D0. The vitals and statistics of a creature of prototype `name` (the NPC record of
    // the same name supplies stats, attributes, experience and skills when there is one).
    // Without a prototype: SetDefaults. Returns 0 when `vitals` or `stats` is null (after the
    // lookups), else 1.
    int BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, const char* name);

    // 00513B60. Gives map object `object` the prototype `name`: units get their vitals and
    // statistics (BuildFromPrototype, slots 36 and 35), their height offset, scale (00517700, the
    // base scale and slot 30) and second prototype; every object its prototype index (+0x18).
    // Without a prototype: defaults, slot 33 (0), index 0 and a debug message. Returns 1.
    int ApplyPrototype(CEngineObject* object, const char* name);
};

// 00513DA0 (the original's ECX is not read). The statistics of an NPC of prototype `name` (a
// fatal error without one; a debug message without its NPC record), then recomputed (00524CD0)
// around a refill of the slots. Returns 1.
int __stdcall BuildNpcStats(UnitStats* stats, const char* name);

// The map object manager ("map_objects", vtable 0073DCD8). Only its object factory.
struct CMapObjectManager {
    // 00512730. Virtual slot 14: a new object of class id `classId` (0x32..0x34 units, 0x35..0x3C
    // the other map objects, 0x300D, 0x3010), constructed; null for other ids.
    CEngineObject* CreateObject(int classId);
};

namespace unitdb {
inline UnitPrototypeTable& PrototypeTable() { return *reinterpret_cast<UnitPrototypeTable*>(0x007AF358); }
inline NpcPrototypeTable& NpcTable() { return *reinterpret_cast<NpcPrototypeTable*>(0x007AF378); }
} // namespace unitdb
