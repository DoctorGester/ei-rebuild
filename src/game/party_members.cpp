// A party's unit records: adding one from a prototype (its texts, statistics and starting items),
// removing one, the record's assignment and the copy of another record's items; the record lists' copies of
// item arrays' SetAtGrow. See party.h, unit.h and docs/notes/items_spells.md ("Party").
#include "item_manager.h"
#include "party.h"

#include "game_strings.h"
#include "item.h"
#include "object_tables.h"
#include "spell.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "lib/containers.h"
#include "lib/text_case.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace {


// A counted copy of `item` (Item::Clone with its context's spell manager); `item` is held
// meanwhile and released after.
Item* CountedClone(Item* item) {
    if (item) item->AddRef();
    Item* copy = item->Clone(item->WorldContext()->GetSpellManager());
    copy->AddRef();
    return copy;
}

void Drop(Item* item) {
    if (item) item->Release();
}

void ReleaseAll(const std::vector<Item*>& list) {
    for (int i = 0; i < std::ssize(list); ++i) list[i]->Release();
}


// A weapon name that gives a weapon: not empty and not "none" (_mbsicmp).
bool Named(const std::string& name) {
    return name.size() != 0 &&
           text::CompareIgnoringCase(name,
                           game::NoneText()) != 0;
}

// The party's copies of `source`'s items, each with the source's count and a reference.
void CopyList(std::vector<Item*>& list, const std::vector<Item*>& source, Party* party) {
    list.resize(std::ssize(source));
    for (int i = 0; i < std::ssize(source); ++i) {
        Item* item = source[i];
        list[i] = party->CopyItem(item);
        Item* copy = list[i];
        copy->count = item->count;
        copy->AddRef();
    }
}

const char* ZoneVariableFormat() { return "z.%s"; }
constexpr int kZoneKindGame = 0;
constexpr int kZoneKindEdge = 1;
constexpr float kOpened = 1.0f;

} // namespace


void PartyUnitRecord::CopyItemsFrom(const PartyUnitRecord* source, Party* party) {
    ClearItems();
    CopyList(weapons, source->weapons, party);
    CopyList(quickItems, source->quickItems, party);
    CopyList(armor, source->armor, party);
    CopyList(spells, source->spells, party);
}

PartyUnitRecord* Party::AddMember(std::string_view name, std::string_view prototype, std::string_view extra) {
    if (!itemManager) Create();
    // The game assigned a constructed stack record to the new entry. Ours is value-initialised: the record's
    // defaults leave the statistics, and BuildNpcStats' reset leaves some of theirs (the layers' spare values, ...),
    // which the game took from its stack and saved with the party (a determinism fix; the harness zeroes the
    // game's too, harness_leftovers.h).
    PartyUnitRecord* record = &members.emplace_back();
    record->texts[1] = name;
    record->unitId = name.empty() ? 0xFFFFFFFFu : static_cast<std::uint32_t>(TextKeyFromName(name));
    record->texts[5] = prototype;
    record->texts[0] = extra;
    FillFromPrototype(record);
    return record;
}

void Party::FillFromPrototype(PartyUnitRecord* record) {
    auto* prototype = g_unitDatabase.prototypes.FindByName(record->texts[5]);
    auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    record->texts[2] = model->figureName;
    BuildNpcStats(&record->stats, record->texts[5]);
    record->scaleY = prototype->scale.y;
    record->scaleX = prototype->scale.x;
    record->height = prototype->scale.z;

    // What the prototype lookup copies out of the prototype: armour names, the two weapon names, spell names.
    const std::vector<std::string>& armour = prototype->armourNames;
    for (int i = 0; i < static_cast<int>(armour.size()); ++i) {
        Item* item = ItemFactory()->CreateItem(armour.begin()[i]);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->armor, lib::Count(record->armor), copy);
        Drop(item);
    }
    for (const auto* weapon : {&prototype->weaponName, &prototype->secondWeaponName}) {
        if (!Named((*weapon))) continue;
        Item* item = ItemFactory()->CreateItem(*weapon);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->weapons, lib::Count(record->weapons), copy);
        Drop(item);
    }
    const std::vector<std::string>& spellNames = prototype->spellNames;
    for (int i = 0; i < static_cast<int>(spellNames.size()); ++i) {
        SpellManager* spellOwner = GetSpellManager();
        auto* spell = ItemFactory()->CreateSpellItem(spellNames.begin()[i], spellOwner);
        Item* copy = CountedClone(spell);
        std::vector<Item*>& list = record->spells;
        int index = lib::Count(list);
        list.resize(index + 1);
        list[index] = copy;
        Drop(spell);
    }
    record->texts[6] = record->texts[5];
}

bool Party::EquipFromPrototype(PartyUnitRecord* record) {
    // Each list emptied from the front (the first entry is looked up and found at 0).
    for (std::vector<Item*>* list : {&record->spells, &record->weapons}) {
        while (!list->empty()) {
            (*list)[0]->Release();
            list->erase(list->begin() + 0, list->begin() + (0 + 1));
        }
    }
    auto* npc = g_unitDatabase.npcs.byName[std::string(record->texts[5])];
    if (!npc) return false;

    const std::vector<std::string>& spellNames = npc->startSpells;
    for (unsigned i = 0; i < static_cast<unsigned>(spellNames.size()); ++i) {
        SpellManager* spellOwner = GetSpellManager();
        auto* spell = ItemFactory()->CreateSpellItem(spellNames.begin()[i], spellOwner);
        record->spells.push_back(CountedClone(spell));
        Drop(spell);
    }
    const std::vector<std::string>& quickNames = npc->startQuickItems;
    for (unsigned i = 0; i < static_cast<unsigned>(quickNames.size()); ++i) {
        Item* item = ItemFactory()->CreateItem(quickNames.begin()[i]);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->quickItems, lib::Count(record->quickItems), copy);
        Drop(item);
    }
    const std::vector<std::string>& weaponNames = npc->startWeapons;
    for (unsigned i = 0; i < static_cast<unsigned>(weaponNames.size()); ++i) {
        Item* item = ItemFactory()->CreateItem(weaponNames.begin()[i]);
        Item* copy = CountedClone(item);
        lib::SetAtGrow(record->weapons, lib::Count(record->weapons), copy);
        Drop(item);
    }
    money += npc->startMoney;
    record->stats.AddExperience(npc->experienceFloor);
    record->texts[6] = record->texts[5];
    return true;
}

bool Party::RemoveMember(int key) {
    for (int i = 0; i < std::ssize(members); ++i) {
        if (static_cast<int>(members[i].unitId) != key) continue;
        members.erase(members.begin() + i);
        return true;
    }
    return false;
}

void Party::OpenEdgeExits() {
    std::string variable;
    MapRecord* newZone = g_mapDatabase.FindMap(zone);
    if (newZone == nullptr || newZone->kind != kZoneKindEdge) return;
    NamedFloatTable& currentVariables = variables;
    for (int i = 0; i < std::ssize(newZone->exits); ++i) {
        MapRecord* target = g_mapDatabase.FindMap(newZone->exits[i].target);
        if (target == nullptr || target->kind != kZoneKindGame) continue;
        variable = str::Format(ZoneVariableFormat(), target->name);
        float value = currentVariables.Get(variable);
        EI_NAN_AUDIT(value);
        if (value == 0.0f) currentVariables.Set(variable, kOpened);
    }
}

// --- The record's move assignment and item clear ---------------------------------------------------

PartyUnitRecord& PartyUnitRecord::operator=(PartyUnitRecord&& from) noexcept {
    if (this != &from) {
        std::destroy_at(this);
        std::construct_at(this, std::move(from));
    }
    return *this;
}

void PartyUnitRecord::ClearItems() {
    ReleaseAll(weapons);
    ReleaseAll(quickItems);
    ReleaseAll(armor);
    ReleaseAll(spells);
    weapons.clear();
    quickItems.clear();
    armor.clear();
    spells.clear();
}
