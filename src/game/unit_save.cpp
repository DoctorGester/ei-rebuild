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
#include "lib/mfc.h"

#include <bit>
#include <cstring>
#include <memory>
#include <string>

namespace {
constexpr std::uint32_t kFlag800000 = 0x800000;     // flags_, cleared by Load
constexpr int kWorldServerClassId = 8;              // the world's GetClassId on a server

constexpr int kVersionField438 = 0x49;
constexpr int kVersionPrototypeNames = 0x4D;
constexpr int kVersionSecondName = 0x6A;


// The index of prototype record `record` (one of the table's), or -1 without one.
int PrototypeIndexOf(const UnitPrototypeRecord* record) {
    return record ? static_cast<int>(record - unitdb::PrototypeTable().records.data()) : -1;
}

// Writes `value` from a stack copy, as for most scalars.
template <typename T>
void WriteCopy(CStream* stream, T value) {
    stream->Write(&value, sizeof(T));
}

template <typename T>
void WriteItemIds(CStream* stream, T* const* items, int count) {
    for (int i = 0; i < count; ++i) WriteCopy<int>(stream, items[i] ? items[i]->id_ : -1);
}

// The prototype record named `name` through the name map (operator[]: an unknown name is added to it).
UnitPrototypeRecord* PrototypeByName(const char* name) {
    std::string key(name);
    return unitdb::PrototypeTable().byName[key];
}

// The world the unit belongs to (its manager's context).
CWorld* ContextWorld(CUnit* unit) { return unit->WorldContext(); }

// Reads an item id and resolves it in the world's item manager (asked before each read), with a
// counted reference.
CItem* ReadItem(CUnit* unit, CStream* stream) {
    CBaseManager* manager = ContextWorld(unit)->ItemFactory();
    int id;
    stream->Read(&id, 4);
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
    stream->Read(&relationsA, 4);
    int playerId;
    stream->Read(&playerId, 4);
    CPlayer* newPlayer = PlayerById(this, playerId);

    stream->Read(&side, 4);
    stream->Read(&animationEntry, 1);
    stream->Read(&secondEntry, 1);
    stream->Read(&animationStart, 4);
    stream->Read(&motionMode, 4);
    stream->Read(&pathTimeShift, 4);
    stream->Read(&moveMode, 4);
    stream->Read(&moveMode2, 4);
    stream->Read(&activeWeapon, 4);
    stream->Read(&pendingWeapon, 4);
    stream->Read(&dead, 4);
    stream->Read(&obstacle, 4);
    Motion()->LoadState(stream);
    stream->Read(&heightOffset, 4);
    stream->Read(&sizeClass, 4);
    stream->Read(&unitRadius, 4);
    Params()->Reset();
    stream->Read(&activeWeapon, 4);             // saved twice; the second copy wins

    ReadItems(this, stream, weapons, 4);
    ReadItems(this, stream, quickItems, 4);
    ReadItems(this, stream, spells, 8);
    ReadItems(this, stream, armor, 7);
    stream->Read(levels4C8, sizeof(levels4C8));
    StatsChanged();
    SetPlayer(newPlayer);   // inlined

    stream->Read(&relationsB, 4);
    int count;
    stream->Read(&count, 4);
    carried.resize(static_cast<std::size_t>(count > 0 ? count : 0));
    for (int i = 0; i < count; ++i) carried[i] = ReadItem(this, stream);
    stream->Read(&standingHeight, 4);

    if (stream->version_ < kVersionField438) return;
    stream->Read(&fightAtWill, 4);
    if (stream->version_ < kVersionPrototypeNames) return;

    std::string prototypeName;
    stream->ReadInto(&prototypeName);
    prototypeId_ = static_cast<std::uint32_t>(PrototypeIndexOf(PrototypeByName(prototypeName.c_str())));
    UnitPrototypeRecord* second;
    if (stream->version_ < kVersionSecondName) {
        second = unitdb::PrototypeTable().FindByName(prototypeName.c_str());
    } else {
        stream->ReadInto(&prototypeName);
        second = PrototypeByName(prototypeName.c_str());
    }
    prototype23C = PrototypeIndexOf(second);
}

void CUnit::SaveUnit(CStream* stream) {
    SaveMapObject(stream);
    controller->Save(stream);
    stream->WriteString(scriptName);
    WriteCopy(stream, relationsA);
    WriteCopy<int>(stream, player ? player->playerId_ : -1);
    WriteCopy(stream, side);
    WriteCopy(stream, animationEntry);
    WriteCopy(stream, secondEntry);
    WriteCopy(stream, animationStart);
    stream->Write(&motionMode, 4);
    stream->Write(&pathTimeShift, 4);
    stream->Write(&moveMode, 4);
    stream->Write(&moveMode2, 4);
    WriteCopy(stream, activeWeapon);
    WriteCopy(stream, pendingWeapon);
    WriteCopy(stream, dead);
    WriteCopy(stream, obstacle);
    Motion()->SaveState(stream);
    WriteCopy(stream, heightOffset);
    WriteCopy(stream, sizeClass);
    WriteCopy(stream, std::bit_cast<std::uint32_t>(unitRadius));
    WriteCopy(stream, activeWeapon);

    WriteItemIds(stream, weapons, 4);
    WriteItemIds(stream, quickItems, 4);
    WriteItemIds(stream, spells, 8);
    WriteItemIds(stream, armor, 7);
    stream->Write(levels4C8, sizeof(levels4C8));
    WriteCopy(stream, relationsB);
    WriteCopy(stream, static_cast<int>(std::ssize(carried)));
    WriteItemIds(stream, carried.data(), static_cast<int>(std::ssize(carried)));
    WriteCopy(stream, standingHeight);
    stream->Write(&fightAtWill, 4);

    // The prototype's name (the record's first field is a CString; a record out of range is copied from
    // address 0), then the second prototype's or "".
    std::string prototypeName = unitdb::PrototypeRecord(prototypeId_)->name;
    stream->WriteString(prototypeName);
    const UnitPrototypeRecord* second = unitdb::PrototypeRecord(static_cast<std::uint32_t>(prototype23C));
    if (second) prototypeName = second->name;
    else prototypeName = "";
    stream->WriteString(prototypeName);
}
