// A party's unit records: adding one from a prototype (its texts, statistics and starting items),
// removing one, the record's assignment and the copy of another record's items; the record
// lists' copies of AlignedPtrArray::SetAtGrow. See party.h, unit.h and
// docs/notes/items_spells.md ("Party").
#include "party.h"

#include "game_strings.h"
#include "item.h"
#include "object_tables.h"
#include "spell.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "lib/crt.h"

#include "ei/hook.h"
#include "originals_gameplay.h"

#include <cstddef>
#include <cstring>
#include <initializer_list>

namespace {

// The spell manager of an item's manager context (slot 22 of the world or the party).
CSpellManager* SpellManagerOf(CItem* item) { return static_cast<CWorld*>(item->ManagerContext())->SpellManager(); }

// A counted copy of `item` (CItem::Clone with its context's spell manager); `item` is held
// meanwhile and released after.
CItem* CountedClone(CItem* item) {
    if (item) ++item->refCount_;
    CItem* copy = item->Clone(SpellManagerOf(item));
    ++copy->refCount_;
    return copy;
}

void Drop(CItem* item) {
    if (item) item->Release();
}

CItem* MakeItem(CParty* party, const char* name) {
    return static_cast<CItem*>(ItemManagerCreateItemOriginal(party->ItemManager(), nullptr, name));
}

// A weapon name that gives a weapon: not empty and not "none" (_mbsicmp).
bool Named(const mfc::CString& name) {
    return name.GetLength() != 0 &&
           gamecrt::mbsicmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(name)),
                            reinterpret_cast<const unsigned char*>(game::NoneText())) != 0;
}

// The party's copies of `source`'s items, each with the source's count and a reference.
void CopyList(AlignedPtrArray& list, const AlignedPtrArray& source, CParty* party) {
    list.SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        auto* item = static_cast<CItem*>(source.data[i]);
        list.data[i] = party->CopyItem(item);
        auto* copy = static_cast<CItem*>(list.data[i]);
        copy->unknown_48 = item->unknown_48;
        ++copy->refCount_;
    }
}

const AlignedPtrArray& ConstList(CEngineObject** const& data) { return *reinterpret_cast<const AlignedPtrArray*>(&data); }

} // namespace

// --- PartyUnitRecord ----------------------------------------------------------------------------

void PartyUnitRecord::CopyItemsFrom(const PartyUnitRecord* source, CParty* party) {
    ClearItems();
    CopyList(ItemList(weapons), ConstList(source->weapons), party);
    CopyList(ItemList(quickItems), ConstList(source->quickItems), party);
    CopyList(ItemList(armor), ConstList(source->armor), party);
    CopyList(ItemList(spells), ConstList(source->spells), party);
}
EI_REPLACE_METHOD(0x00660500, PartyUnitRecord::CopyItemsFrom);

PartyUnitRecord* PartyUnitRecord::Assign(const PartyUnitRecord* source) {
    for (int i = 0; i < 7; ++i) texts[i] = source->texts[i];
    unknown_01C = source->unknown_01C;
    std::memcpy(stats, source->stats, sizeof(stats));
    // +0x724..+0x780 member by member (the padding after +0x778 is not copied).
    std::memcpy(&unknown_724, &source->unknown_724, offsetof(PartyUnitRecord, unknown_778) - offsetof(PartyUnitRecord, unknown_724));
    unknown_778 = source->unknown_778;
    unknown_77C = source->unknown_77C;
    unknown_780 = source->unknown_780;
    return this;
}
EI_REPLACE_METHOD(0x00664950, PartyUnitRecord::Assign);

// The record lists' own copies of AlignedPtrArray::SetAtGrow (00519E80; the same code): the
// weapons (00664490), quick items (006645D0) and armour (00664710).
EI_REPLACE_METHOD(0x00664490, AlignedPtrArray::SetAtGrow);
EI_REPLACE_METHOD(0x006645D0, AlignedPtrArray::SetAtGrow);
EI_REPLACE_METHOD(0x00664710, AlignedPtrArray::SetAtGrow);

// --- CParty -------------------------------------------------------------------------------------

PartyUnitRecord* CParty::AddMember(const char* name, const char* prototype, const char* extra) {
    if (!itemManager_) Create();
    // The original's stack record: constructed (its statistics are whatever the stack holds),
    // assigned to the new entry, destroyed.
    alignas(PartyUnitRecord) unsigned char storage[sizeof(PartyUnitRecord)];
    auto* temporary = reinterpret_cast<PartyUnitRecord*>(storage);
    temporary->Construct();
    int index = members_.size;
    members_.SetSize(index + 1, -1);
    members_.data[index].Assign(temporary);
    temporary->Destruct();

    PartyUnitRecord* record = &members_.data[members_.size - 1];
    record->texts[1] = name;
    record->unknown_01C = *name == '\0' ? 0xFFFFFFFFu : static_cast<std::uint32_t>(TextKeyFromName(name));
    record->texts[5] = prototype;
    record->texts[0] = extra;
    FillFromPrototype(record);
    return record;
}
EI_REPLACE_METHOD(0x006628D0, CParty::AddMember);

BOOL CParty::FillFromPrototype(PartyUnitRecord* record) {
    auto* prototype = reinterpret_cast<UnitPrototypeRecord*>(unitdb::PrototypeTable().FindByName(record->texts[5]));
    auto* model = reinterpret_cast<UnitModelRecord*>(
        unitdb::Models().At(static_cast<std::uint32_t>(prototype->modelIndex), unitdb::kModelSize));
    record->texts[2] = model->unknown_E8;
    BuildNpcStats(reinterpret_cast<UnitStats*>(record->stats), record->texts[5]);
    record->unknown_728 = prototype->scale[1];
    record->unknown_724 = prototype->scale[0];
    record->height = prototype->scale[2];

    // What 005143E0 copies out of the prototype: armour names, the two weapon names, spell names.
    const CStringVector& armour = prototype->armourNames;
    for (int i = 0; i < static_cast<int>(armour.end - armour.begin); ++i) {
        CItem* item = MakeItem(this, armour.begin[i]);
        CItem* copy = CountedClone(item);
        ItemList(record->armor).SetAtGrow(static_cast<int>(record->armorCount), copy);
        Drop(item);
    }
    for (const mfc::CString* weapon : {&prototype->weaponName, &prototype->secondWeaponName}) {
        if (!Named(*weapon)) continue;
        CItem* item = MakeItem(this, *weapon);
        CItem* copy = CountedClone(item);
        ItemList(record->weapons).SetAtGrow(static_cast<int>(record->weaponCount), copy);
        Drop(item);
    }
    const CStringVector& spellNames = prototype->spellNames;
    for (int i = 0; i < static_cast<int>(spellNames.end - spellNames.begin); ++i) {
        CSpellManager* owner = SpellManager();
        auto* spell = static_cast<CItem*>(FactoryCreateSpellOriginal(ItemManager(), nullptr, spellNames.begin[i], owner));
        CItem* copy = CountedClone(spell);
        AlignedPtrArray& list = ItemList(record->spells);
        int index = list.size;
        list.SetSize(index + 1, -1);
        list.data[index] = copy;
        Drop(spell);
    }
    record->texts[6] = record->texts[5];
    return 1;
}
EI_REPLACE_METHOD(0x006621F0, CParty::FillFromPrototype);

BOOL CParty::EquipFromPrototype(PartyUnitRecord* record) {
    // Each list emptied from the front (the original looks the first entry up, finding it at 0).
    for (AlignedPtrArray* list : {&ItemList(record->spells), &ItemList(record->weapons)}) {
        while (list->size != 0) {
            static_cast<CEngineObject*>(list->data[0])->Release();
            list->RemoveRange(0, 1);
        }
    }
    auto* npc = reinterpret_cast<UnitNpcRecord*>(unitdb::NpcTable().byName[stlp::string(record->texts[5])]);
    if (!npc) return 0;

    const CStringVector& spellNames = npc->startSpells;
    for (unsigned i = 0; i < static_cast<unsigned>(spellNames.end - spellNames.begin); ++i) {
        CSpellManager* owner = SpellManager();
        auto* spell = static_cast<CItem*>(FactoryCreateSpellOriginal(ItemManager(), nullptr, spellNames.begin[i], owner));
        ItemList(record->spells).Add(CountedClone(spell));
        Drop(spell);
    }
    const CStringVector& quickNames = npc->startQuickItems;
    for (unsigned i = 0; i < static_cast<unsigned>(quickNames.end - quickNames.begin); ++i) {
        CItem* item = MakeItem(this, quickNames.begin[i]);
        CItem* copy = CountedClone(item);
        ItemList(record->quickItems).SetAtGrow(static_cast<int>(record->quickItemCount), copy);
        Drop(item);
    }
    const CStringVector& weaponNames = npc->startWeapons;
    for (unsigned i = 0; i < static_cast<unsigned>(weaponNames.end - weaponNames.begin); ++i) {
        CItem* item = MakeItem(this, weaponNames.begin[i]);
        CItem* copy = CountedClone(item);
        ItemList(record->weapons).SetAtGrow(static_cast<int>(record->weaponCount), copy);
        Drop(item);
    }
    Money() += npc->startMoney;
    reinterpret_cast<UnitStats*>(record->stats)->AddExperience(npc->experienceFloor);
    record->texts[6] = record->texts[5];
    return 1;
}
EI_REPLACE_METHOD(0x006625B0, CParty::EquipFromPrototype);

// The record lists' out-of-line Add (spells, 00663E40) and RemoveAt(index, count) (spells
// 00663F60, weapons 00663A70).
EI_REPLACE_METHOD(0x00663E40, AlignedPtrArray::Add);
EI_REPLACE_METHOD(0x00663F60, AlignedPtrArray::RemoveRange);
EI_REPLACE_METHOD(0x00663A70, AlignedPtrArray::RemoveRange);

BOOL CParty::RemoveMember(int key) {
    for (int i = 0; i < members_.size; ++i) {
        PartyUnitRecord* record = &members_.data[i];
        if (static_cast<int>(record->unknown_01C) != key) continue;
        record->Destruct();
        int after = members_.size - i - 1;
        if (after != 0) std::memmove(record, record + 1, static_cast<std::size_t>(after) * sizeof(PartyUnitRecord));
        --members_.size;
        return 1;
    }
    return 0;
}
EI_REPLACE_METHOD(0x006620F0, CParty::RemoveMember);
