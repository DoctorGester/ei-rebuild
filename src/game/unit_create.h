#pragma once

// Unit setup helpers around creation and loading: the unit prototype table's name index and the server controller's
// statistics hand-over. See docs/notes/units.md ("Creation, databases, save").

#include "resource_manager.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include "lib/hash_map.h"

#include <vector>
#include <unordered_map>

#undef GetClassName

struct FigureAnimation;
class CUnit;
class CMapObject;
struct UnitStats;
struct UnitVitals;
struct UnitPrototypeRecord;
struct UnitNpcRecord;

// The unit prototype table: the records and an index by prototype name.
struct UnitPrototypeTable {
    UnitPrototypeRecord* At(std::uint32_t index);
    // In STLport's bucket order (EI_Plugin's fallback prototype is the first bucket head that fits).
    using NameIndex = lib::HashMap<std::string, UnitPrototypeRecord*, lib::TextHash>;

    // The record named `name` (an unknown name gets a null entry), or null.
    UnitPrototypeRecord* FindByName(std::string_view name);

    std::vector<UnitPrototypeRecord> records;
    NameIndex byName;
};

// The NPC prototype table (records of statistics for named creatures), laid out the same way.
struct NpcPrototypeTable {

    UnitNpcRecord* FindByName(std::string_view name);

    std::vector<UnitNpcRecord> records;
    std::unordered_map<std::string, UnitNpcRecord*> byName;
};

// The statistics of an NPC of prototype `name` (a fatal error without one; a debug message without its NPC
// record), then recomputed around a refill of the slots. Returns 1.
int BuildNpcStats(UnitStats* stats, const char* name);

// The object class of prototype `name`'s model record; 0x33 without a prototype or model.
int PrototypeObjectClass(std::string_view name);

// The map object manager ("map objects", class id 0x2D).
struct CMapObjectManager : CResourceManager {
    CMapObjectManager();
    ~CMapObjectManager() override;
    int Create() override;
    const char* GetClassName() override;
    int GetClassId() override;
    CEngineObject* NewObject(int classId) override;
    CEngineObject* CreateObject(int classId, int id) override;
    const char* GetFolderName() override;                   // "map objects"

    // A new object of class id `classId` (0x32..0x34 units, 0x35..0x3C the other map objects, 0x300D, 0x3010), constructed; null
    // for other ids. The starting equipment of prototype `name`: see docs/notes/units.md, "Creation helpers".
    int GiveStartingEquipment(CUnit* unit, const char* name);

    int CreateManager();
    // The base manager's CreateObject; a new object gets an animation state (the figure manager's class
    // 0x300D) and its side halves from the world's specific manager: classes 0x46 and 0x47, then 0x48 for the
    // three unit classes (0x32-0x34) and 0x4B for classes 0x36-0x39. Returns the object or null.
    CEngineObject* CreateMapObject(int classId, int id);
    // The object registered under `id` (null for none); the manager holds only map objects.
    CMapObject* FindMapObject(unsigned id) const;
    // The entry of the animation database "<figure>.adb" matching `key`: the same bits above 0x7FFF and, of
    // the low bits, none of the key's missing (a key without low bits takes only an entry without). Null
    // (with a debug message when there is no database).
    FigureAnimation* FindAnimation(const std::string* figure, std::uint32_t key);
    // The defaults of an object without a prototype (SetDefaults): a unit (class 0x50) gets the statistics
    // (SetStats), any other object the vitals (SetServerData); then ScaleEffectChanged(0) and no prototype
    // index. Returns 1.
    int GiveDefaults(CMapObject* object);

    // The builders of a creature's starting statistics from its prototype; the map objects call them on their
    // owner.
    // Defaults for an object without a prototype: with `stats`, the statistics (stats
    // 50, rates, values and body slots at 100, no layers); without, the vitals (1000 hit points
    // and mana, regeneration 10, protection 100). Returns 1.
    int SetDefaults(UnitVitals* vitals, UnitStats* stats);
    // The vitals and statistics of a creature of prototype `name` (the NPC record of
    // the same name supplies stats, attributes, experience and skills when there is one).
    // Without a prototype: SetDefaults. Returns 0 when `vitals` or `stats` is null (after the
    // lookups), else 1.
    int BuildFromPrototype(UnitVitals* vitals, UnitStats* stats, std::string_view name);
    // Gives map object `object` the prototype `name`: units get their vitals and statistics
    // (BuildFromPrototype, SetServerData and SetStats), their height offset, scale (the base scale and
    // ApplyBaseScale) and second prototype; every object its prototype index. Without a prototype: defaults,
    // ScaleEffectChanged(0), index 0 and a debug message. Returns 1.
    int ApplyPrototype(CMapObject* object, const char* name);
};

inline CMapObjectManager* MapObjectManagerOf(const CEngineObject* object) {
    return static_cast<CMapObjectManager*>(object->owner_);
}

namespace unitdb {
// Record `index` of a table's records, or null past the end.
template <typename T>
T* RecordAt(std::vector<T>& records, std::uint32_t index) {
    return index < records.size() ? &records[index] : nullptr;
}
} // namespace unitdb
