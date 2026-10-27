// CUnit's Load and Save slots (vtable 0073EA8C slots 9 and 10). See unit.h and
// docs/notes/units.md ("Creation, databases, save").
#include "base_manager.h"
#include "unit.h"

#include "ai_motion.h"
#include "core_streams.h"
#include "player_messages.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <bit>
#include <cstring>
#include <string>

namespace {
constexpr std::uint32_t kFlag800000 = 0x800000;     // flags_, cleared by Load
constexpr int kWorldServerClassId = 8;              // the world's GetClassId on a server

constexpr int kVersionField438 = 0x49;
constexpr int kVersionPrototypeNames = 0x4D;
constexpr int kVersionSecondName = 0x6A;

constexpr int kCarriedGrowMin = 4;
constexpr int kCarriedGrowMax = 0x400;

// The index of prototype record `record` (one of the table's), or -1 without one.
int PrototypeIndexOf(const UnitPrototypeRecord* record) {
    return record ? static_cast<int>(record - unitdb::PrototypeTable().begin) : -1;
}

// Writes `value` from a stack copy, as the original does for most scalars.
template <typename T>
void WriteCopy(CStream* stream, T value) {
    stream->Write(&value, sizeof(T));
}

void WriteString(CStream* stream, const char* text) {
    stream->Write(text, static_cast<unsigned>(std::strlen(text) + 1));
}

template <typename T>
void WriteItemIds(CStream* stream, T* const* items, int count) {
    for (int i = 0; i < count; ++i) WriteCopy<int>(stream, items[i] ? items[i]->id_ : -1);
}

// Reads a string and assigns it to `target` (the original's temporary is destroyed at once).
void ReadStringInto(CStream* stream, mfc::CString& target) {
    alignas(4) unsigned char storage[sizeof(mfc::CString)];
    mfc::CString* read = stream->ReadString(storage);
    target = *read;
    read->~CString();
}

// The prototype record named `name` through the name map (an unknown name is added to it;
// 005335E0, hash_map<string, record*>::operator[], inlined by the original).
UnitPrototypeRecord* PrototypeByName(const char* name) {
    stlp::string key(name);
    return unitdb::PrototypeTable().byName.Subscript(key);
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

void ZeroSlots(CItem** slots, int count) {
    for (int i = 0; i < count; ++i) slots[i] = nullptr;
}

// CArray::SetSize of the carried items as the original inlines it.
void SetCarriedSize(CUnit* unit, int size) {
    if (size == 0) {
        if (unit->carried) {
            gamecrt::operator_delete(unit->carried);
            unit->carried = nullptr;
        }
        unit->carriedCapacity = 0;
        unit->carriedCount = 0;
        return;
    }
    if (!unit->carried) {
        unit->carried = gamecrt::operator_new_array<CItem*>(static_cast<std::size_t>(size));
        ZeroSlots(unit->carried, size);
        unit->carriedCapacity = size;
        unit->carriedCount = size;
        return;
    }
    if (size <= unit->carriedCapacity) {
        if (size > unit->carriedCount) ZeroSlots(unit->carried + unit->carriedCount, size - unit->carriedCount);
        unit->carriedCount = size;
        return;
    }
    int grow = unit->carriedGrowBy;
    if (grow == 0) {
        grow = unit->carriedCount / 8;
        if (grow < kCarriedGrowMin) grow = kCarriedGrowMin;
        else if (grow > kCarriedGrowMax) grow = kCarriedGrowMax;
    }
    int capacity = grow + unit->carriedCapacity;
    if (size >= capacity) capacity = size;
    auto* items = gamecrt::operator_new_array<CItem*>(static_cast<std::size_t>(capacity));
    std::memcpy(items, unit->carried, static_cast<std::size_t>(unit->carriedCount) << 2);
    ZeroSlots(items + unit->carriedCount, size - unit->carriedCount);
    gamecrt::operator_delete(unit->carried);
    unit->carried = items;
    unit->carriedCount = size;
    unit->carriedCapacity = capacity;
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
    ReadStringInto(stream, scriptName);
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
    SetPlayer(newPlayer);   // inlined by the original

    stream->Read(&relationsB, 4);
    int count;
    stream->Read(&count, 4);
    SetCarriedSize(this, count);
    for (int i = 0; i < count; ++i) carried[i] = ReadItem(this, stream);
    stream->Read(&standingHeight, 4);

    if (stream->version_ < kVersionField438) return;
    stream->Read(&fightAtWill, 4);
    if (stream->version_ < kVersionPrototypeNames) return;

    mfc::CString prototypeName;
    ReadStringInto(stream, prototypeName);
    prototypeId_ = static_cast<std::uint32_t>(PrototypeIndexOf(PrototypeByName(prototypeName)));
    UnitPrototypeRecord* second;
    if (stream->version_ < kVersionSecondName) {
        second = unitdb::PrototypeTable().FindByName(prototypeName);
    } else {
        ReadStringInto(stream, prototypeName);
        second = PrototypeByName(prototypeName);
    }
    prototype23C = PrototypeIndexOf(second);
}
EI_REWRITTEN(0x00531A80, CUnit::LoadUnit);

void CUnit::SaveUnit(CStream* stream) {
    SaveMapObject(stream);
    controller->Save(stream);
    WriteString(stream, scriptName);
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
    WriteCopy(stream, carriedCount);
    WriteItemIds(stream, carried, carriedCount);
    WriteCopy(stream, standingHeight);
    stream->Write(&fightAtWill, 4);

    // The prototype's name (the record's first field is a CString; a record out of range is
    // copied from address 0, as the original does), then the second prototype's or "".
    std::string prototypeName = unitdb::PrototypeRecord(prototypeId_)->name.GetString();
    WriteString(stream, prototypeName.c_str());
    const UnitPrototypeRecord* second = unitdb::PrototypeRecord(static_cast<std::uint32_t>(prototype23C));
    if (second) prototypeName = second->name;
    else prototypeName = "";
    WriteString(stream, prototypeName.c_str());
}
EI_REWRITTEN(0x005321C0, CUnit::SaveUnit);
