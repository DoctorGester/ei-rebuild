#pragma once

// Unit setup helpers around creation and loading: the unit prototype table's name index and the
// server controller's statistics hand-over. Names provisional. See docs/notes/units.md ("Creation,
// databases, save").

#include "resource_manager.h"

#include "lib/mfc.h"
#include "lib/stlp.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

class CEngineObject;
struct FigureAnimation;   // map_object_figures.h
class CUnit;
class CMapObject;   // map_object.h
struct UnitStats;
struct UnitVitals;
struct UnitPrototypeRecord;   // unit_db.h
struct UnitNpcRecord;         // unit_db.h

// The unit prototype table (global): an STLport vector of 0x120-byte records followed by
// a hash_map<string, record*> from prototype names to records at +0x0C.
struct UnitPrototypeTable {
    // Prototype `index`, or null past the end.
    UnitPrototypeRecord* At(std::uint32_t index);
    using NameIndex = stlp::hash_map<stlp::string, UnitPrototypeRecord*, stlp::StringHash>;

    // The record named `name`: operator[] on the name index,
    // so an unknown name is inserted with a null record (and null is returned).
    UnitPrototypeRecord* FindByName(const char* name);

    UnitPrototypeRecord* begin;         // +0x00
    UnitPrototypeRecord* end;           // +0x04
    UnitPrototypeRecord* endOfStorage;  // +0x08
    NameIndex byName;           // +0x0C
};
static_assert(offsetof(UnitPrototypeTable, byName) == 0x0C);

// The NPC prototype table (global; records of statistics for named creatures), laid
// out the same way.
struct NpcPrototypeTable {
    using NameIndex = stlp::hash_map<stlp::string, UnitNpcRecord*, stlp::StringHash>;

    // As UnitPrototypeTable::FindByName (its own instantiation).
    UnitNpcRecord* FindByName(const char* name);

    UnitNpcRecord* begin;
    UnitNpcRecord* end;
    UnitNpcRecord* endOfStorage;
    NameIndex byName;           // +0x0C
};
static_assert(offsetof(NpcPrototypeTable, byName) == 0x0C);


// The statistics of an NPC of prototype `name` (a
// fatal error without one; a debug message without its NPC record), then recomputed
// around a refill of the slots. Returns 1.
int __stdcall BuildNpcStats(UnitStats* stats, const char* name);

// The object class (+0x04 of its model record) of prototype `name`; 0x33 without a
// prototype or model.
int __stdcall PrototypeObjectClass(const char* name);

// The map object manager ("map objects", class id 0x2D, 0xBC bytes; a real virtual class: the
// compiler makes its vtable). +0xAC is its map object context
// (the world).
struct CMapObjectManager : CResourceManager {
    // CResourceManager's constructor.
    CMapObjectManager();
    // CResourceManager's destructor.
    ~CMapObjectManager() override;
    int Create() override;                                  // 2: CreateManager
    const char* GetClassName() override;                    // 11: "CMapObjectManager"
    int GetClassId() override;                              // 12: (0x2D)
    CEngineObject* NewObject(int classId) override;         // 14: NewObjectImpl
    CEngineObject* CreateObject(int classId, int id) override;   // 23: CreateMapObject
    const char* GetFolderName() override;                   // 25: "map objects"

    // Slot 14's body: a new object of class id `classId`
    // (0x32..0x34 units, 0x35..0x3C the other map objects, 0x300D, 0x3010), constructed; null for
    // other ids.
    // Gives `unit` the starting equipment of prototype
    // `name` (unit_db.h), made by the world's item manager (world slot 18; spells also take what
    // slot 22 returns, asked first): a unit of type 0x32 equips each armour piece and gets the
    // weapon (unless named "none" or empty; the slot it lands in becomes +0x270) and the second
    // weapon; every unit gets each spell. A piece that cannot be made gets a FatalError message
    // (a weapon is added even then: AddWeapon(null) returns 0, so +0x270 becomes 0). Without the
    // prototype a FatalError message and 0; else 1.
    int GiveStartingEquipment(CUnit* unit, const char* name);

    // The base manager's Create (0 when it fails), then the archive
    // "<text folder>database.res" added. Returns 1.
    int CreateManager();
    // The base manager's CreateObject; a new object gets an
    // animation state (the figure manager's class 0x300D) and its side halves from the world's
    // specific manager: classes 0x46 and 0x47 (through the object's slot 19), then 0x48 for the
    // three unit classes (0x32-0x34) and 0x4B for classes 0x36-0x39. Returns the object or null.
    CEngineObject* CreateMapObject(int classId, int id);
    // The entry of the animation database "<figure>.adb" matching
    // `key`: the same bits above 0x7FFF and, of the low bits, none of the key's missing (a key
    // without low bits takes only an entry without). Null (with a debug message when there is no
    // database).
    FigureAnimation* FindAnimation(const mfc::CString* figure, std::uint32_t key);
    // The defaults of an object without a prototype (SetDefaults): a unit
    // (class 0x50) gets the statistics (its slot 35), any other object the vitals; then
    // slot 33 with 0 and no prototype index (+0x18). Returns 1.
    int GiveDefaults(CMapObject* object);
    // The starting equipment's names of prototype `name`: the armour pieces,
    // the two weapons and the spells. 1, or FatalError's result without the prototype.
    int PrototypeEquipment(const char* name, mfc::CStringArray* armour, mfc::CString* weapon,
                           mfc::CString* secondWeapon, mfc::CStringArray* spells);

    // The builders of a creature's starting statistics from its prototype (unit_create_stats.cpp).
    // The map objects call them on their owner; they never read the manager.
    // Defaults for an object without a prototype: with `stats`, the statistics (stats
    // 50, rates, values and body slots at 100, no layers); without, the vitals (1000 hit points
    // and mana, regeneration 10, protection 100). Returns 1.
    int SetDefaults(UnitVitals* vitals, UnitStats* stats);
    // The vitals and statistics of a creature of prototype `name` (the NPC record of
    // the same name supplies stats, attributes, experience and skills when there is one).
    // Without a prototype: SetDefaults. Returns 0 when `vitals` or `stats` is null (after the
    // lookups), else 1.
    int BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, const char* name);
    // Gives map object `object` the prototype `name`: units get their vitals and
    // statistics (BuildFromPrototype, slots 36 and 35), their height offset, scale (the
    // base scale and slot 30) and second prototype; every object its prototype index (+0x18).
    // Without a prototype: defaults, slot 33 (0), index 0 and a debug message. Returns 1.
    int ApplyPrototype(CMapObject* object, const char* name);
};
static_assert(sizeof(CMapObjectManager) == 0xBC);

// The manager that owns a map object (CEngineObject::owner_; CreateMapObject made it).
inline CMapObjectManager* MapObjectManagerOf(const CEngineObject* object) {
    return static_cast<CMapObjectManager*>(object->owner_);
}

namespace unitdb {
UnitPrototypeTable& PrototypeTable();   // the unit database's first table (unit_db_destroy.cpp)
NpcPrototypeTable& NpcTable();          // its second
} // namespace unitdb
