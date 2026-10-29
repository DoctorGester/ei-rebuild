// The unit set's builder (UnitSet::Add; world.h), called for each unit by the server's SendZone
// (world_sync.cpp): the figured groups (by prototype), each group a hash_multimap of figured records by
// their scale key, and the dressed records, a hash_multimap by prototype.
#include "world.h"

#include "item_db.h"
#include "unit.h"

#include <cstdint>
#include <string>

namespace {

constexpr int kNoModelNumber = -1;   // UnitLookRecord::modelNumber of a record shared by several units
constexpr float kScaleSteps = 127.0f;

// The figure's scale as a key: each component times 127, truncated (_ftol), 10 bits each, x highest.
std::uint32_t ScaleKey(const Vector3& scale) {
    auto steps = [](float v) { return static_cast<std::uint32_t>(static_cast<int>(v * kScaleSteps)) & 0x3FF; };
    return (steps(scale.x) << 10 | steps(scale.y)) << 10 | steps(scale.z);
}

// An item's id (its prototype index in the high word, its material in the low word) when the item
// is there and both indices are inside their tables (`known` for the prototype), else kNoItemId.
template <typename Known>
std::uint32_t ItemId(const CEngineObject* item, Known known) {
    if (!item) return kNoItemId;
    std::uint32_t id = item->prototypeId_;
    if (!known(id >> 16) || !itemdb::MaterialAt(id & 0xFFFF)) return kNoItemId;
    return id;
}
bool KnownArmour(std::uint32_t index) { return itemdb::ArmorAt(index) != nullptr; }
bool KnownWeapon(std::uint32_t index) { return itemdb::WeaponAt(index) != nullptr; }

// Two records with the same worn and extra weapon ids.
bool SameIds(const DressedUnitRecord& a, const DressedUnitRecord& b) {
    return a.items == b.items && a.extraWeapons == b.extraWeapons;
}

} // namespace

void UnitSet::Add(CUnit* unit) {
    FiguredUnitRecord record;   // its vectors empty
    std::uint32_t prototype = unit->prototypeId_ & 0xFFFF;
    const Vector3 scale = unit->scale;
    record.prototype = static_cast<std::uint16_t>(unit->prototypeId_);
    std::uint32_t scaleKey = ScaleKey(scale);
    record.morphS = scale.x;
    record.morphT = scale.y;
    record.morphU = scale.z;

    if (unit->MapType() == MapObjectType::Hero) {
        for (int slot = 0; slot < 7; ++slot)
            record.items.push_back(ItemId(unit->WornItem(slot), KnownArmour));
        for (std::uint32_t slot = 0; slot < 4; ++slot)
            record.extraWeapons.push_back(ItemId(unit->GetWeapon(slot), KnownWeapon));
        record.extraWeapon = static_cast<std::int8_t>(unit->activeWeapon);
    } else {
        record.extraWeapon = -1;
    }

    // The figured records: one of the same scale key and ids in the prototype's group is marked
    // (modelNumber -1); else the record goes into the group.
    bool marked = false;
    for (auto [it, last] = figured[prototype].equal_range(scaleKey); it != last; ++it) {
        if (SameIds(it->second, record)) {
            it->second.modelNumber = kNoModelNumber;
            marked = true;
            break;
        }
    }
    if (!marked) {
        if (unit->MapType() != MapObjectType::Hero) {
            // The texts the server companion holds (CMapObjectServer's string array).
            const CMapObjectServer* server = unit->ServerCompanion();
            for (const std::string& name : server->strings) record.names.push_back(name);
        }
        record.modelNumber = static_cast<int>(unit->id_);
        // The group reached again as the game does: the groups grow for one more entry each time (their bucket
        // count is the walk order Write sends).
        figured[prototype].insert({scaleKey, record});
    }

    // The dressed records: one of the same prototype and ids is marked, else a copy of the
    // record's head is added.
    for (auto [it, last] = dressed.equal_range(prototype); it != last; ++it) {
        if (SameIds(it->second, record)) {
            it->second.modelNumber = kNoModelNumber;
            return;
        }
    }
    record.modelNumber = static_cast<int>(unit->id_);
    dressed.insert({prototype, record});
}
