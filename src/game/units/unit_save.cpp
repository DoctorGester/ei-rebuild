// Unit's Load and Save slots. See unit.h and docs/notes/units.md ("Creation, databases, save").
#include "engine/core/base_manager.h"
#include "game/items/item_manager.h"
#include "game/units/unit.h"

#include "game/ai/ai_motion.h"
#include "engine/core/core_streams.h"
#include "game/players/player.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"

#include "lib/crt.h"
#include "game/saves/save_versions.h"
#include <cstddef>
#include <cstdint>
#include <string>

#include <bit>
#include <cstring>
#include <memory>

namespace {



// The index of prototype record `record` (one of the table's), or -1 without one.
int PrototypeIndexOf(const UnitPrototypeRecord* record) {
    return record ? static_cast<int>(record - g_unitDatabase.prototypes.records.data()) : -1;
}



// Reads an item id and resolves it in the world's item manager (asked before each read), with a
// counted reference.
Item* ReadItem(Unit* unit, ByteStream* stream) {
    BaseManager* manager = unit->GetManagerContext()->ItemFactory();
    const int id = stream->ReadId();
    if (id == kNoObjectId) return nullptr;
    Item* item = manager->FindObjectAs<Item>(id);
    if (item) item->AddRef();
    return item;
}

void ReadItems(Unit* unit, ByteStream* stream, Item** slots, int count) {
    for (int i = 0; i < count; ++i) slots[i] = ReadItem(unit, stream);
}
// The worn armour: its ids name armour pieces.
void ReadItems(Unit* unit, ByteStream* stream, ItemArmor** slots, int count) {
    for (int i = 0; i < count; ++i) {
        Item* item = ReadItem(unit, stream);
        slots[i] = item ? item->AsArmor() : nullptr;
    }
}




// The unit's new owner: on a server world, player `id` (null when there is none).
Player* PlayerById(Unit* unit, int id) {
    Player* player = nullptr;
    if (unit->OwningWorld()->GetClassId() == ClassId::WorldServer)
        player = unit->ServerWorld()->GetPlayer(id);
    return player;
}
} // namespace

void Unit::LoadUnit(ByteStream* stream) {
    MapObject::Load(stream);
    controller->Load(stream);
    flags &= ~MapObjectFlag::Blocked;
    scriptName = stream->ReadString();
    stream->ReadValue(relationsA);
    int playerId;
    stream->ReadValue(playerId);
    Player* newPlayer = PlayerById(this, playerId);

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
    stream->Read(exposure.data(), sizeof(exposure));
    StatsChanged();
    SetPlayer(newPlayer);   // inlined

    stream->ReadValue(relationsB);
    const int count = stream->ReadCount();
    carried.resize(count);
    for (int i = 0; i < count; ++i) carried[i] = ReadItem(this, stream);
    stream->ReadValue(standingHeight);

    if (stream->version < kVersionFightAtWill) return;
    stream->ReadValue(fightAtWill);
    if (stream->version < kVersionPrototypeNames) return;

    std::string prototypeName;
    prototypeName = stream->ReadString();
    prototypeId = static_cast<std::uint32_t>(PrototypeIndexOf(g_unitDatabase.prototypes.FindByName(prototypeName)));
    UnitPrototypeRecord* second;
    if (stream->version < kVersionSecondName) {
        second = g_unitDatabase.prototypes.FindByName(prototypeName);
    } else {
        prototypeName = stream->ReadString();
        second = g_unitDatabase.prototypes.FindByName(prototypeName);
    }
    secondPrototype = PrototypeIndexOf(second);
}

void Unit::SaveUnit(ByteStream* stream) {
    MapObject::Save(stream);
    controller->Save(stream);
    stream->WriteString(scriptName);
    stream->WriteValue(relationsA);
    stream->WriteValue<int>(player ? player->playerId : -1);
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
    stream->WriteValue(unitRadius);
    stream->WriteValue(activeWeapon);

    for (const Item* item : weapons) stream->WriteId(item);
    for (const Item* item : quickItems) stream->WriteId(item);
    for (const Item* item : spells) stream->WriteId(item);
    for (const ItemArmor* item : armor) stream->WriteId(item);
    stream->Write(exposure.data(), sizeof(exposure));
    stream->WriteValue(relationsB);
    stream->WriteCount(carried.size());
    for (const Item* item : carried) stream->WriteId(item);
    stream->WriteValue(standingHeight);
    stream->WriteValue(fightAtWill);

    // The prototype's name, then the second prototype's; "" for one out of range (the original read the first
    // through null).
    const UnitPrototypeRecord* first = unitdb::PrototypeRecord(prototypeId);
    std::string prototypeName = first ? first->name : std::string();
    stream->WriteString(prototypeName);
    const UnitPrototypeRecord* second = unitdb::PrototypeRecord(static_cast<std::uint32_t>(secondPrototype));
    if (second) prototypeName = second->name;
    else prototypeName = "";
    stream->WriteString(prototypeName);
}
