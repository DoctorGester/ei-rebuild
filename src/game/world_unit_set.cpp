// The unit set's writer, used by the server's SendZone (world_sync.cpp), and the client's reader of the unit
// looks. See docs/notes/world.md ("Client maps", the unit set). The set's three hash maps walk in the game's
// order (lib::HashMap).
#include "core_streams.h"

#include "item_db.h"
#include "unit.h"
#include "world.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace {

void Put(ByteStream* stream, const void* data, std::uint32_t size) { stream->Write(data, size); }

// vector<int>::resize(n, value), vector<string>::resize, the stream's string reader.

void Get(ByteStream* stream, void* data, std::uint32_t size) { stream->Read(data, size); }

// A counted dword array: the count, the vector resized (new entries 0), the data.
void GetIds(ByteStream* stream, std::vector<std::uint32_t>& list) {
    std::uint32_t n;
    stream->ReadValue(n);
    std::uint32_t zero = 0;
    list.resize(n, zero);
    Get(stream, list.data(), static_cast<std::uint32_t>(list.size()) << 2);
}

} // namespace

void DressedUnitRecord::Write(ByteStream* stream) {
    std::int16_t word = static_cast<std::int16_t>(prototype);
    stream->WriteValue(word);
    stream->WriteValue(static_cast<std::uint32_t>(items.size()));
    Put(stream, items.data(), static_cast<std::uint32_t>(items.size() * sizeof(int)));
    stream->WriteValue(static_cast<std::uint32_t>(extraWeapons.size()));
    Put(stream, extraWeapons.data(), static_cast<std::uint32_t>(extraWeapons.size() * sizeof(int)));
    std::uint8_t byte = static_cast<std::uint8_t>(extraWeapon);
    stream->WriteValue(byte);
    stream->WriteValue(static_cast<std::uint32_t>(modelNumber));
}

void UnitSet::Write(ByteStream* stream) {
    // The number of figured records over every group.
    std::uint32_t count = 0;
    for (const auto& [prototype, group] : figured) count += static_cast<std::uint32_t>(group.size());
    stream->WriteValue(count);

    for (auto& [prototype, group] : figured) {
        for (auto& [scaleKey, record] : group) {
            record.Write(stream);
            stream->WriteValue(record.morphS);
            stream->WriteValue(record.morphT);
            stream->WriteValue(record.morphU);
            auto names = [&record](std::uint32_t i) -> const char* { return record.names[i].c_str(); };
            auto nameCount = [&record] { return static_cast<std::uint32_t>(record.names.size()); };
            stream->WriteValue(nameCount());
            for (std::uint32_t i = 0; i < nameCount(); ++i) {
                const char* text = names(i);
                Put(stream, text, static_cast<std::uint32_t>(std::strlen(text) + 1));
            }
        }
    }
    stream->WriteValue(static_cast<std::uint32_t>(dressed.size()));

    for (auto& [prototype, looks] : dressed) {
        DressedUnitRecord* record = &looks;
        std::uint32_t key = record->prototype;   // two bytes written
        Put(stream, &key, 2);
        auto counted = [stream](const std::vector<std::uint32_t>& list) {
            std::uint32_t n = static_cast<std::uint32_t>(list.size());
            stream->WriteValue(n);
            n = static_cast<std::uint32_t>(list.size());
            Put(stream, list.data(), n << 2);
        };
        counted(record->items);
        counted(record->extraWeapons);
        std::uint32_t flag = static_cast<std::uint8_t>(record->extraWeapon);
        Put(stream, &flag, 1);
        stream->WriteValue(static_cast<std::uint32_t>(record->modelNumber));
    }
}

// ---- The unit look lists' reader ------------------------------------------------------

UnitLooks ReadUnitLooks(ByteStream* stream) {
    UnitLooks result;
    std::uint32_t count;
    stream->ReadValue(count);
    result.figured.resize(count);
    for (FiguredUnitRecord& looks : result.figured) {
        FiguredUnitRecord* record = &looks;
        stream->ReadValue(record->prototype);
        GetIds(stream, record->items);
        GetIds(stream, record->extraWeapons);
        stream->ReadValue(record->extraWeapon);
        stream->ReadValue(record->modelNumber);
        stream->ReadValue(record->morphS);
        stream->ReadValue(record->morphT);
        stream->ReadValue(record->morphU);
        std::uint32_t names;
        stream->ReadValue(names);
        record->names.resize(names);
        for (std::uint32_t i = 0; i < static_cast<std::uint32_t>(record->names.size()); ++i)
            record->names[i] = stream->ReadString();
    }
    stream->ReadValue(count);
    result.dressed.resize(count);
    for (DressedUnitRecord& looks : result.dressed) {
        DressedUnitRecord* record = &looks;
        stream->ReadValue(record->prototype);
        GetIds(stream, record->items);
        GetIds(stream, record->extraWeapons);
        stream->ReadValue(record->extraWeapon);
        stream->ReadValue(record->modelNumber);
    }
    return result;
}

// --- Add ---------------------------------------------------------------------------------------------

// The unit set's builder (UnitSet::Add; world.h), called for each unit by the server's SendZone
// (world_sync.cpp): the figured groups (by prototype), each group a hash_multimap of figured records by
// their scale key, and the dressed records, a hash_multimap by prototype.

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
std::uint32_t ItemId(const EngineObject* item, Known known) {
    if (!item) return kNoItemId;
    std::uint32_t id = item->prototypeId;
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

void UnitSet::Add(Unit* unit) {
    FiguredUnitRecord record;   // its vectors empty
    std::uint32_t prototype = unit->prototypeId & 0xFFFF;
    const Vector3 scale = unit->scale;
    record.prototype = static_cast<std::uint16_t>(unit->prototypeId);
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
            // The texts the server companion holds (MapObjectServer's string array).
            const MapObjectServer* server = unit->ServerCompanion();
            for (const std::string& name : server->strings) record.names.push_back(name);
        }
        record.modelNumber = static_cast<int>(unit->id);
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
    record.modelNumber = static_cast<int>(unit->id);
    dressed.insert({prototype, record});
}
