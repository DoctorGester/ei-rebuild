// The named party records of the party manager: reset at new game, and the script commands' helpers to
// find, add and remove a record by name. See party_records.h and docs/notes/world.md ("Party records and
// class templates").
#include "object_tables.h"
#include "party_records.h"
#include "party_manager.h"
#include "core_refcount.h"
#include "game_strings.h"
#include "class_templates.h"
#include "party.h"

#include <cstdint>

#include "lib/str.h"

#include <bit>

namespace {

const char* const kHero = "Hero";
const char* const kHumanHero = "Human Hero";
const char* const kFirstZone = "gz1g";
const char* const kGameTime = "gtime";
constexpr std::uint32_t kSix = 0x40C00000;                                     // 6.0f

// A new record with no references (the count dword is cleared only after the allocation is tested).
PartyRecord* NewRecord() {
    auto* record = new PartyRecord();
    if (!record) return nullptr;
    record->refs_ = 0;
    record->flags6_ = 0;
    record->flags7_ = 0;
    return record;
}

const char* NameOf(const PartyRecord* record) { return record->name.c_str(); }

} // namespace

void PartyRecordsReset(NamedPartyRecords* list) {
    list->records.clear();
    list->text0C = game::kEmptyText;
    list->text10 = game::kEmptyText;

    list->records.emplace_back(NewRecord());

    CParty* party = list->records.front()->Party();
    party->AddMember(kHero, kHumanHero, game::kEmptyText);
    party->zone_ = kFirstZone;
    party->entrance_ = 0;
    party->variables_.Set(kGameTime, std::bit_cast<float>(kSix));
}

CParty* NamedPartyRecords::Find(const char* name) const {
    CParty* unnamed = nullptr;
    for (PartyRecord* record : records) {
        if (str::Compare(NameOf(record), name) == 0) return record->Party();
        if (str::Compare(NameOf(record), game::kEmptyText) == 0) unnamed = record->Party();
    }
    return unnamed;
}

void NamedPartyRecords::Add(const char* name) {
    PartyRecord* record = NewRecord();
    record->name = name;
    records.emplace_back(record);
}

void NamedPartyRecords::Remove(const char* name) {
    for (unsigned i = 0; i < static_cast<unsigned>(records.size()); ++i) {
        if (str::Compare(NameOf(records[i]), name) != 0) continue;
        // The index still advances, so the record moved into slot i is not tested (original behaviour).
        records.erase(records.begin() + i);
    }
}

// ---- The party manager -----------------------------------------------------------------

namespace {

// The party stored under `key` (find, inlined), null if none. Not const: the parties' copies read their source
// non-const.
CParty* FindParty(const PartyMap& parties, int key) {
    auto found = parties.find(key);
    return found != parties.end() ? const_cast<CParty*>(&found->second) : nullptr;
}

} // namespace

CParty* PartyAt(PartyMap& parties, int key) {
    CParty made;
    CParty pair(made);
    return &parties.try_emplace(key, pair).first->second;
}

void PartyManagerNewGame(PartyManager* manager) {
    PartyRecordsReset(&manager->records);
    manager->templates.Build(0);
}

void PartyManagerNewGameFour(PartyManager* manager) {
    PartyRecordsReset(&manager->records);
    manager->templates.Build(1);
}

void PartyManager::BeginRedeploy(CParty* party) { records.BeginRedeploy(party); }

void PartyManager::EndRedeploy(CParty* party) { records.EndRedeploy(party); }

int g_nextPartyId = /*0078B0E8*/ 1;

int PartyNextId(int* out) {
    *out = g_nextPartyId;
    return ++g_nextPartyId;
}

bool PartyManager::LoadParty(const char*, const char*, const int* key, CParty* target) const {
    if (*key == 0) {
        target->Clear(1, 0);
        return true;
    }
    CParty* stored = FindParty(parties, *key);
    if (!stored) return false;
    target->Assign(stored);
    return true;
}

bool PartyManager::StoreParty(const int* key, CParty* source) {
    CParty* value = FindParty(parties, *key);
    if (!value) value = PartyAt(parties, *key);
    value->Assign(source);
    return true;
}
