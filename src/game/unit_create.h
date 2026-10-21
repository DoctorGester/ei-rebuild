#pragma once

// Unit setup helpers around creation and loading: the unit prototype table's name index and the
// server controller's statistics hand-over. Names provisional. See docs/notes/units.md ("Creation,
// databases, save").

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
struct FigureAnimation;   // map_object_figures.h
class CUnit;
struct UnitStats;
struct UnitVitals;
struct UnitPrototypeRecord;   // unit_db.h

// The unit prototype table (global 007AF358): an STLport vector of 0x120-byte records (item_db.h's
// RecordTable reads its begin / end) followed by a hash_map<string, record*> from prototype names
// to records at +0x0C.
struct UnitPrototypeTable {
    // 0053AD30 (unit_stat_small.cpp, RET 4). Prototype `index`, or null past the end.
    UnitPrototypeRecord* At(std::uint32_t index);
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

// 00513240. The object class (+0x04 of its model record) of prototype `name`; 0x33 without a
// prototype or model.
int __stdcall PrototypeObjectClass(const char* name);

// The map object manager ("map_objects", vtable 0073DCD8). Only its object factory and the
// starting equipment; +0xAC is its map object context (the world).
struct CMapObjectManager {
    // 00512730. Virtual slot 14: a new object of class id `classId` (0x32..0x34 units, 0x35..0x3C
    // the other map objects, 0x300D, 0x3010), constructed; null for other ids.
    CEngineObject* CreateObject(int classId);
    // 005141F0 (RET 8; unit_equipment.cpp). Gives `unit` the starting equipment of prototype
    // `name` (unit_db.h), made by the world's item manager (world slot 18; spells also take what
    // slot 22 returns, asked first): a unit of type 0x32 equips each armour piece and gets the
    // weapon (unless named "none" or empty; the slot it lands in becomes +0x270) and the second
    // weapon; every unit gets each spell. A piece that cannot be made gets a FatalError message
    // (a weapon is added even then: AddWeapon(null) returns 0, so +0x270 becomes 0). Without the
    // prototype a FatalError message and 0; else 1.
    int GiveStartingEquipment(CUnit* unit, const char* name);

    // map_object_manager.cpp (A-U12).
    // 005125E0. The constructor: the resource manager's (00674D90), then the vtable 0073DCD8.
    // Returns this.
    CMapObjectManager* Construct();
    // 00512650. The destructor body: the vtable, then the resource manager's (00674DF0).
    void Destruct();
    // 00512630 (slot 0, RET 4). Destruct, then operator delete with flags bit 0. Returns this.
    CMapObjectManager* DeletingDestruct(unsigned flags);
    // 00512660 (slot 2). The base manager's Create (00403020; 0 when it fails), then the archive
    // "<text folder>database.res" added (slot 16). Returns 1.
    int CreateManager();
    // 00512A30 (slot 23, RET 8). The base manager's CreateObject (00403730); a new object gets an
    // animation state (the figure manager's class 0x300D) and its side halves from the world's
    // specific manager: classes 0x46 and 0x47 (through the object's slot 19), then 0x48 for the
    // three unit classes (0x32-0x34) and 0x4B for classes 0x36-0x39. Returns the object or null.
    CEngineObject* CreateMapObject(int classId, int id);
    // 00512B20 (RET 8). The entry of the animation database "<figure>.adb" (slot 19) matching
    // `key`: the same bits above 0x7FFF and, of the low bits, none of the key's missing (a key
    // without low bits takes only an entry without). Null (with a debug message when there is no
    // database).
    FigureAnimation* FindAnimation(const mfc::CString* figure, std::uint32_t key);
    // 00513630 (RET 4). The defaults of an object without a prototype (SetDefaults): a unit
    // (class 0x50) gets the statistics (its slot 35), any other object the vitals (slot 36); then
    // slot 33 with 0 and no prototype index (+0x18). Returns 1.
    int GiveDefaults(CEngineObject* object);
    // 005143E0 (RET 0x14). The starting equipment's names of prototype `name`: the armour pieces,
    // the two weapons and the spells. 1, or FatalError's result without the prototype.
    int PrototypeEquipment(const char* name, mfc::CStringArray* armour, mfc::CString* weapon,
                           mfc::CString* secondWeapon, mfc::CStringArray* spells);
};

namespace unitdb {
inline UnitPrototypeTable& PrototypeTable() { return ei::GameData<UnitPrototypeTable>(0x007AF358); }
inline NpcPrototypeTable& NpcTable() { return ei::GameData<NpcPrototypeTable>(0x007AF378); }
} // namespace unitdb
