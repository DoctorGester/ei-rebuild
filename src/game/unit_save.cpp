// CUnit's Load and Save slots. See unit.h and docs/notes/units.md ("Creation, databases, save").
#include "base_manager.h"
#include "unit.h"

#include "ai_motion.h"
#include "core_streams.h"
#include "player_messages.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include <bit>
#include <cstring>
#include <memory>

namespace {
constexpr std::uint32_t kFlag800000 = 0x800000;     // flags_, cleared by Load
constexpr int kWorldServerClassId = 8;              // the world's GetClassId on a server

constexpr int kVersionFightAtWill = 0x49;
constexpr int kVersionPrototypeNames = 0x4D;
constexpr int kVersionSecondName = 0x6A;


// The index of prototype record `record` (one of the table's), or -1 without one.
int PrototypeIndexOf(const UnitPrototypeRecord* record) {
    return record ? static_cast<int>(record - g_unitDatabase->prototypes.records.data()) : -1;
}

template <typename T>
void WriteItemIds(CStream* stream, T* const* items, int count) {
    for (int i = 0; i < count; ++i) stream->WriteValue<int>(items[i] ? items[i]->id_ : -1);
}

// The prototype record named `name` through the name map (operator[]: an unknown name is added to it).
UnitPrototypeRecord* PrototypeByName(std::string_view name) {
    std::string key(name);
    return g_unitDatabase->prototypes.byName[key];
}

// The world the unit belongs to (its manager's context).
CWorld* ContextWorld(CUnit* unit) { return unit->WorldContext(); }

// Reads an item id and resolves it in the world's item manager (asked before each read), with a
// counted reference.
CItem* ReadItem(CUnit* unit, CStream* stream) {
    CBaseManager* manager = ContextWorld(unit)->ItemFactory();
    int id;
    stream->ReadValue(id);
    if (id == -1) return nullptr;
    CItem* item = FindItem(manager, id);
    if (item) ++item->refCount_;
    return item;
}

void ReadItems(CUnit* unit, CStream* stream, CItem** slots, int count) {
    for (int i = 0; i < count; ++i) slots[i] = ReadItem(unit, stream);
}
// The worn armour: its ids name armour pieces.
void ReadItems(CUnit* unit, CStream* stream, CItemArmor** slots, int count) {
    for (int i = 0; i < count; ++i) slots[i] = AsArmor(ReadItem(unit, stream));
}




// The unit's new owner: on a server world, player `id` (null when there is none).
CPlayer* PlayerById(CUnit* unit, int id) {
    CPlayer* player = nullptr;
    if (ContextWorld(unit)->GetClassId() == kWorldServerClassId)
        player = unit->ServerWorld()->GetPlayer(id);
    return player;
}
} // namespace

void CUnit::LoadUnit(CStream* stream) {
    LoadMapObject(stream);
    controller->Load(stream);
    flags_ &= ~kFlag800000;
    stream->ReadInto(&scriptName);
    stream->ReadValue(relationsA);
    int playerId;
    stream->ReadValue(playerId);
    CPlayer* newPlayer = PlayerById(this, playerId);

    stream->ReadValue(side);
    stream->ReadValue(animationEntry);
    stream->ReadValue(secondEntry);
    stream->ReadValue(animationStart);
    stream->ReadValue(motionMode);
    stream->ReadValue(pathTimeShift);
    stream->ReadValue(moveMode);
    stream->ReadValue(moveMode2);
    stream->ReadValue(activeWeapon);
    stream->ReadValue(pendingWeapon);
    stream->ReadValue(dead);
    stream->ReadValue(obstacle);
    motion.LoadState(stream);
    stream->ReadValue(heightOffset);
    stream->ReadValue(sizeClass);
    stream->ReadValue(unitRadius);
    params.Reset();
    stream->ReadValue(activeWeapon);             // saved twice; the second copy wins

    ReadItems(this, stream, weapons, 4);
    ReadItems(this, stream, quickItems, 4);
    ReadItems(this, stream, spells, 8);
    ReadItems(this, stream, armor, 7);
    stream->Read(exposure, sizeof(exposure));
    StatsChanged();
    SetPlayer(newPlayer);   // inlined

    stream->ReadValue(relationsB);
    int count;
    stream->ReadValue(count);
    carried.resize(static_cast<std::size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) carried[i] = ReadItem(this, stream);
    stream->ReadValue(standingHeight);

    if (stream->version_ < kVersionFightAtWill) return;
    stream->ReadValue(fightAtWill);
    if (stream->version_ < kVersionPrototypeNames) return;

    std::string prototypeName;
    stream->ReadInto(&prototypeName);
    prototypeId_ = static_cast<std::uint32_t>(PrototypeIndexOf(PrototypeByName(prototypeName)));
    UnitPrototypeRecord* second;
    if (stream->version_ < kVersionSecondName) {
        second = g_unitDatabase->prototypes.FindByName(prototypeName);
    } else {
        stream->ReadInto(&prototypeName);
        second = PrototypeByName(prototypeName);
    }
    secondPrototype = PrototypeIndexOf(second);
}

void CUnit::SaveUnit(CStream* stream) {
    SaveMapObject(stream);
    controller->Save(stream);
    stream->WriteString(scriptName);
    stream->WriteValue(relationsA);
    stream->WriteValue<int>(player ? player->playerId_ : -1);
    stream->WriteValue(side);
    stream->WriteValue(animationEntry);
    stream->WriteValue(secondEntry);
    stream->WriteValue(animationStart);
    stream->WriteValue(motionMode);
    stream->WriteValue(pathTimeShift);
    stream->WriteValue(moveMode);
    stream->WriteValue(moveMode2);
    stream->WriteValue(activeWeapon);
    stream->WriteValue(pendingWeapon);
    stream->WriteValue(dead);
    stream->WriteValue(obstacle);
    motion.SaveState(stream);
    stream->WriteValue(heightOffset);
    stream->WriteValue(sizeClass);
    stream->WriteValue(std::bit_cast<std::uint32_t>(unitRadius));
    stream->WriteValue(activeWeapon);

    WriteItemIds(stream, weapons, 4);
    WriteItemIds(stream, quickItems, 4);
    WriteItemIds(stream, spells, 8);
    WriteItemIds(stream, armor, 7);
    stream->Write(exposure, sizeof(exposure));
    stream->WriteValue(relationsB);
    stream->WriteValue(static_cast<int>(std::ssize(carried)));
    WriteItemIds(stream, carried.data(), static_cast<int>(std::ssize(carried)));
    stream->WriteValue(standingHeight);
    stream->WriteValue(fightAtWill);

    // The prototype's name (the record's first field is a string; a record out of range is copied from
    // address 0), then the second prototype's or "".
    std::string prototypeName = unitdb::PrototypeRecord(prototypeId_)->name;
    stream->WriteString(prototypeName);
    const UnitPrototypeRecord* second = unitdb::PrototypeRecord(static_cast<std::uint32_t>(secondPrototype));
    if (second) prototypeName = second->name;
    else prototypeName = "";
    stream->WriteString(prototypeName);
}
