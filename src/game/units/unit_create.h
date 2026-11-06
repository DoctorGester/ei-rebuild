#pragma once

// Unit setup helpers around creation and loading: the unit prototype table's name index and the server controller's
// statistics hand-over. See docs/notes/units.md ("Creation, databases, save").

#include "lib/hash_map.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

struct UnitStats;
struct UnitPrototypeRecord;
struct UnitNpcRecord;
enum class MapObjectType : int;   // objects/map_object_manager.h

// The unit prototype table: the records and an index by prototype name.
struct UnitPrototypeTable {
    UnitPrototypeRecord* At(std::uint32_t index);
    // In STLport's bucket order (EI_Plugin's fallback prototype is the first bucket head that fits).
    using NameIndex = lib::HashMap<std::string, UnitPrototypeRecord*, lib::TextHash>;

    // The record named `name`, or null.
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

// The statistics of an NPC of prototype `name` (the game ends without one; a debug message without its NPC
// record), then recomputed around a refill of the slots.
void BuildNpcStats(UnitStats* stats, const std::string& name);

// The object class of prototype `name`'s model record; Creature without a prototype or model.
MapObjectType PrototypeObjectClass(std::string_view name);

namespace unitdb {
// Record `index` of a table's records, or null past the end.
template <typename T>
T* RecordAt(std::vector<T>& records, std::uint32_t index) {
    return index < records.size() ? &records[index] : nullptr;
}
} // namespace unitdb
