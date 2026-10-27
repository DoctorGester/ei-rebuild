// A party's unit records: adding one from a prototype (its texts, statistics and starting items),
// removing one, the record's assignment and the copy of another record's items; the record lists' copies of
// AlignedPtrArray::SetAtGrow. See party.h, unit.h and docs/notes/items_spells.md ("Party").
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
#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <string>

namespace {

// The spell manager of an item's manager context (of the world or the party).
CSpellManager* SpellManagerOf(CItem* item) { return item->WorldContext()->SpellManager(); }

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
    return party->ItemManager()->CreateItem(name);
}

// A weapon name that gives a weapon: not empty and not "none" (_mbsicmp).
bool Named(const std::string& name) {
    return name.size() != 0 &&
           gamecrt::mbsicmp(name.c_str(),
                           game::NoneText()) != 0;
}

// The party's copies of `source`'s items, each with the source's count and a reference.
void CopyList(PartyItemArray& list, const PartyItemArray& source, CParty* party) {
    list.SetSize(source.size, -1);
    for (int i = 0; i < source.size; ++i) {
        CItem* item = source.data[i];
        list.data[i] = party->CopyItem(item);
        CItem* copy = list.data[i];
        copy->count_ = item->count_;
        ++copy->refCount_;
    }
}

const PartyItemArray& ConstList(CItem** const& data) { return *reinterpret_cast<const PartyItemArray*>(&data); }

} // namespace


void PartyUnitRecord::CopyItemsFrom(const PartyUnitRecord* source, CParty* party) {
    ClearItems();
    CopyList(ItemList(weapons), ConstList(source->weapons), party);
    CopyList(ItemList(quickItems), ConstList(source->quickItems), party);
    CopyList(ItemList(armor), ConstList(source->armor), party);
    CopyList(ItemList(spells), ConstList(source->spells), party);
}
EI_REWRITTEN(0x00660500, PartyUnitRecord::CopyItemsFrom);

PartyUnitRecord* PartyUnitRecord::Assign(const PartyUnitRecord* source) {
    for (int i = 0; i < 7; ++i) texts[i] = source->texts[i];
    unitId = source->unitId;
    std::memcpy(&stats, &source->stats, sizeof(stats));
    // +0x724..+0x780 member by member (the padding after +0x778 is not copied).
    std::memcpy(&scaleX, &source->scaleX, offsetof(PartyUnitRecord, selected) - offsetof(PartyUnitRecord, scaleX));
    selected = source->selected;
    leaderId = source->leaderId;
    fightAtWill = source->fightAtWill;
    return this;
}
EI_REWRITTEN(0x00664950, PartyUnitRecord::Assign);

EI_REWRITTEN(0x00664490, AlignedPtrArray::SetAtGrow);
EI_REWRITTEN(0x006645D0, AlignedPtrArray::SetAtGrow);
EI_REWRITTEN(0x00664710, AlignedPtrArray::SetAtGrow);

PartyUnitRecord* CParty::AddMember(const char* name, const char* prototype, const char* extra) {
    if (!itemManager_) Create();
    // A stack record: constructed, assigned to the new entry, destroyed. It starts zeroed: the constructor
    // leaves the statistics, and BuildNpcStats' reset leaves some of theirs (the layers' spare values, ...),
    // which the game took from its stack and saved with the party (a determinism fix; the harness zeroes the
    // game's too, harness_leftovers.h).
    alignas(PartyUnitRecord) unsigned char storage[sizeof(PartyUnitRecord)] = {};
    auto* temporary = reinterpret_cast<PartyUnitRecord*>(storage);
    temporary->Construct();
    int index = members_.size;
    members_.SetSize(index + 1, -1);
    members_.data[index].Assign(temporary);
    temporary->Destruct();

    PartyUnitRecord* record = &members_.data[members_.size - 1];
    record->texts[1] = name;
    record->unitId = *name == '\0' ? 0xFFFFFFFFu : static_cast<std::uint32_t>(TextKeyFromName(name));
    record->texts[5] = prototype;
    record->texts[0] = extra;
    FillFromPrototype(record);
    return record;
}
EI_REWRITTEN(0x006628D0, CParty::AddMember);

BOOL CParty::FillFromPrototype(PartyUnitRecord* record) {
    auto* prototype = unitdb::PrototypeTable().FindByName(record->texts[5].c_str());
    auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    record->texts[2] = model->figureName;
    BuildNpcStats(&record->stats, record->texts[5].c_str());
    record->scaleY = prototype->scale.y;
    record->scaleX = prototype->scale.x;
    record->height = prototype->scale.z;

    // What the prototype lookup copies out of the prototype: armour names, the two weapon names, spell names.
    const CStringVector& armour = prototype->armourNames;
    for (int i = 0; i < static_cast<int>(armour.size()); ++i) {
        CItem* item = MakeItem(this, armour.begin()[i]);
        CItem* copy = CountedClone(item);
        ItemList(record->armor).SetAtGrow(static_cast<int>(record->armorCount), copy);
        Drop(item);
    }
    for (const auto* weapon : {&prototype->weaponName, &prototype->secondWeaponName}) {
        if (!Named((*weapon).c_str())) continue;
        CItem* item = MakeItem(this, (*weapon).c_str());
        CItem* copy = CountedClone(item);
        ItemList(record->weapons).SetAtGrow(static_cast<int>(record->weaponCount), copy);
        Drop(item);
    }
    const CStringVector& spellNames = prototype->spellNames;
    for (int i = 0; i < static_cast<int>(spellNames.size()); ++i) {
        CSpellManager* owner = SpellManager();
        auto* spell = ItemManager()->CreateSpellItem(spellNames.begin()[i], owner);
        CItem* copy = CountedClone(spell);
        PartyItemArray& list = ItemList(record->spells);
        int index = list.size;
        list.SetSize(index + 1, -1);
        list.data[index] = copy;
        Drop(spell);
    }
    record->texts[6] = record->texts[5];
    return 1;
}
EI_REWRITTEN(0x006621F0, CParty::FillFromPrototype);

BOOL CParty::EquipFromPrototype(PartyUnitRecord* record) {
    // Each list emptied from the front (the first entry is looked up and found at 0).
    for (PartyItemArray* list : {&ItemList(record->spells), &ItemList(record->weapons)}) {
        while (list->size != 0) {
            list->data[0]->Release();
            list->RemoveRange(0, 1);
        }
    }
    auto* npc = unitdb::NpcTable().byName[stlp::string(record->texts[5].c_str())];
    if (!npc) return 0;

    const CStringVector& spellNames = npc->startSpells;
    for (unsigned i = 0; i < static_cast<unsigned>(spellNames.size()); ++i) {
        CSpellManager* owner = SpellManager();
        auto* spell = ItemManager()->CreateSpellItem(spellNames.begin()[i], owner);
        ItemList(record->spells).Add(CountedClone(spell));
        Drop(spell);
    }
    const CStringVector& quickNames = npc->startQuickItems;
    for (unsigned i = 0; i < static_cast<unsigned>(quickNames.size()); ++i) {
        CItem* item = MakeItem(this, quickNames.begin()[i]);
        CItem* copy = CountedClone(item);
        ItemList(record->quickItems).SetAtGrow(static_cast<int>(record->quickItemCount), copy);
        Drop(item);
    }
    const CStringVector& weaponNames = npc->startWeapons;
    for (unsigned i = 0; i < static_cast<unsigned>(weaponNames.size()); ++i) {
        CItem* item = MakeItem(this, weaponNames.begin()[i]);
        CItem* copy = CountedClone(item);
        ItemList(record->weapons).SetAtGrow(static_cast<int>(record->weaponCount), copy);
        Drop(item);
    }
    Money() += npc->startMoney;
    record->stats.AddExperience(npc->experienceFloor);
    record->texts[6] = record->texts[5];
    return 1;
}
EI_REWRITTEN(0x006625B0, CParty::EquipFromPrototype);

EI_REWRITTEN(0x00663E40, AlignedPtrArray::Add);
EI_REWRITTEN(0x00663F60, AlignedPtrArray::RemoveRange);
EI_REWRITTEN(0x00663A70, AlignedPtrArray::RemoveRange);
BOOL CParty::RemoveMember(int key) {
    for (int i = 0; i < members_.size; ++i) {
        PartyUnitRecord* record = &members_.data[i];
        if (static_cast<int>(record->unitId) != key) continue;
        record->Destruct();
        int after = members_.size - i - 1;
        if (after != 0) std::memmove(record, record + 1, static_cast<std::size_t>(after) * sizeof(PartyUnitRecord));
        --members_.size;
        return 1;
    }
    return 0;
}
EI_REWRITTEN(0x006620F0, CParty::RemoveMember);


namespace {
const char* ZoneVariableFormat() { return "z.%s"; }
constexpr int kZoneKindGame = 0;
constexpr int kZoneKindEdge = 1;
constexpr float kOpened = 1.0f;
} // namespace

void CParty::OpenEdgeExits() {
    std::string variable;
    MapRecord* zone = TheMapDatabase()->FindMap(zone_);
    if (zone == nullptr || zone->kind != kZoneKindEdge) return;
    NamedFloatTable& variables = variables_;
    for (int i = 0; i < zone->exits.size; ++i) {
        MapRecord* target = TheMapDatabase()->FindMap(zone->exits[i].target.c_str());
        if (target == nullptr || target->kind != kZoneKindGame) continue;
        variable = str::Format(ZoneVariableFormat(), target->name.c_str());
        float value = variables.Get(variable.c_str());
        EI_NAN_AUDIT(value);
        if (value == 0.0f) variables.Set(variable.c_str(), kOpened);
    }
}
EI_REWRITTEN(0x006629D0, CParty::OpenEdgeExits);
