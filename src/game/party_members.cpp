// A party's unit records: adding one from a prototype (its texts, statistics and starting items),
// removing one, the record's assignment and the copy of another record's items; the record lists' copies of
// item arrays' SetAtGrow. See party.h, unit.h and docs/notes/items_spells.md ("Party").
#include "party.h"

#include "game_strings.h"
#include "item.h"
#include "object_tables.h"
#include "spell.h"
#include "unit.h"
#include "unit_create.h"
#include "unit_db.h"
#include "world.h"

#include "lib/text_case.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

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
           text::CompareIgnoringCase(name,
                           game::NoneText()) != 0;
}

// The party's copies of `source`'s items, each with the source's count and a reference.
void CopyList(PartyItemArray& list, const PartyItemArray& source, CParty* party) {
    list.resize(std::ssize(source));
    for (int i = 0; i < std::ssize(source); ++i) {
        CItem* item = source[i];
        list[i] = party->CopyItem(item);
        CItem* copy = list[i];
        copy->count_ = item->count_;
        ++copy->refCount_;
    }
}

} // namespace


void PartyUnitRecord::CopyItemsFrom(const PartyUnitRecord* source, CParty* party) {
    ClearItems();
    CopyList(weapons, source->weapons, party);
    CopyList(quickItems, source->quickItems, party);
    CopyList(armor, source->armor, party);
    CopyList(spells, source->spells, party);
}

PartyUnitRecord* CParty::AddMember(const char* name, const char* prototype, const char* extra) {
    if (!itemManager_) Create();
    // The game assigned a constructed stack record to the new entry. Ours is value-initialised: the record's
    // defaults leave the statistics, and BuildNpcStats' reset leaves some of theirs (the layers' spare values, ...),
    // which the game took from its stack and saved with the party (a determinism fix; the harness zeroes the
    // game's too, harness_leftovers.h).
    PartyUnitRecord* record = &members_.emplace_back();
    record->texts[1] = name;
    record->unitId = *name == '\0' ? 0xFFFFFFFFu : static_cast<std::uint32_t>(TextKeyFromName(name));
    record->texts[5] = prototype;
    record->texts[0] = extra;
    FillFromPrototype(record);
    return record;
}

BOOL CParty::FillFromPrototype(PartyUnitRecord* record) {
    auto* prototype = unitdb::PrototypeTable().FindByName(record->texts[5].c_str());
    auto* model = unitdb::ModelRecord(static_cast<std::uint32_t>(prototype->modelIndex));
    record->texts[2] = model->figureName;
    BuildNpcStats(&record->stats, record->texts[5].c_str());
    record->scaleY = prototype->scale.y;
    record->scaleX = prototype->scale.x;
    record->height = prototype->scale.z;

    // What the prototype lookup copies out of the prototype: armour names, the two weapon names, spell names.
    const std::vector<std::string>& armour = prototype->armourNames;
    for (int i = 0; i < static_cast<int>(armour.size()); ++i) {
        CItem* item = MakeItem(this, armour.begin()[i].c_str());
        CItem* copy = CountedClone(item);
        lib::SetAtGrow(record->armor, std::ssize(record->armor), copy);
        Drop(item);
    }
    for (const auto* weapon : {&prototype->weaponName, &prototype->secondWeaponName}) {
        if (!Named((*weapon))) continue;
        CItem* item = MakeItem(this, (*weapon).c_str());
        CItem* copy = CountedClone(item);
        lib::SetAtGrow(record->weapons, std::ssize(record->weapons), copy);
        Drop(item);
    }
    const std::vector<std::string>& spellNames = prototype->spellNames;
    for (int i = 0; i < static_cast<int>(spellNames.size()); ++i) {
        CSpellManager* owner = SpellManager();
        auto* spell = ItemManager()->CreateSpellItem(spellNames.begin()[i].c_str(), owner);
        CItem* copy = CountedClone(spell);
        PartyItemArray& list = record->spells;
        int index = std::ssize(list);
        list.resize(index + 1);
        list[index] = copy;
        Drop(spell);
    }
    record->texts[6] = record->texts[5];
    return 1;
}

BOOL CParty::EquipFromPrototype(PartyUnitRecord* record) {
    // Each list emptied from the front (the first entry is looked up and found at 0).
    for (PartyItemArray* list : {&record->spells, &record->weapons}) {
        while (!list->empty()) {
            (*list)[0]->Release();
            list->erase(list->begin() + 0, list->begin() + (0 + 1));
        }
    }
    auto* npc = unitdb::NpcTable().byName[std::string(record->texts[5].c_str())];
    if (!npc) return 0;

    const std::vector<std::string>& spellNames = npc->startSpells;
    for (unsigned i = 0; i < static_cast<unsigned>(spellNames.size()); ++i) {
        CSpellManager* owner = SpellManager();
        auto* spell = ItemManager()->CreateSpellItem(spellNames.begin()[i].c_str(), owner);
        record->spells.push_back(CountedClone(spell));
        Drop(spell);
    }
    const std::vector<std::string>& quickNames = npc->startQuickItems;
    for (unsigned i = 0; i < static_cast<unsigned>(quickNames.size()); ++i) {
        CItem* item = MakeItem(this, quickNames.begin()[i].c_str());
        CItem* copy = CountedClone(item);
        lib::SetAtGrow(record->quickItems, std::ssize(record->quickItems), copy);
        Drop(item);
    }
    const std::vector<std::string>& weaponNames = npc->startWeapons;
    for (unsigned i = 0; i < static_cast<unsigned>(weaponNames.size()); ++i) {
        CItem* item = MakeItem(this, weaponNames.begin()[i].c_str());
        CItem* copy = CountedClone(item);
        lib::SetAtGrow(record->weapons, std::ssize(record->weapons), copy);
        Drop(item);
    }
    Money() += npc->startMoney;
    record->stats.AddExperience(npc->experienceFloor);
    record->texts[6] = record->texts[5];
    return 1;
}

BOOL CParty::RemoveMember(int key) {
    for (int i = 0; i < std::ssize(members_); ++i) {
        if (static_cast<int>(members_[i].unitId) != key) continue;
        members_.erase(members_.begin() + i);
        return 1;
    }
    return 0;
}

namespace {
const char* ZoneVariableFormat() { return "z.%s"; }
constexpr int kZoneKindGame = 0;
constexpr int kZoneKindEdge = 1;
constexpr float kOpened = 1.0f;
} // namespace

void CParty::OpenEdgeExits() {
    std::string variable;
    MapRecord* zone = TheMapDatabase()->FindMap(zone_.c_str());
    if (zone == nullptr || zone->kind != kZoneKindEdge) return;
    NamedFloatTable& variables = variables_;
    for (int i = 0; i < std::ssize(zone->exits); ++i) {
        MapRecord* target = TheMapDatabase()->FindMap(zone->exits[i].target.c_str());
        if (target == nullptr || target->kind != kZoneKindGame) continue;
        variable = str::Format(ZoneVariableFormat(), target->name.c_str());
        float value = variables.Get(variable.c_str());
        EI_NAN_AUDIT(value);
        if (value == 0.0f) variables.Set(variable.c_str(), kOpened);
    }
}
