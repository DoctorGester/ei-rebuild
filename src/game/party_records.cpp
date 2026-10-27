// The named party records of the party manager: reset at new game, and the script commands' helpers to
// find, add and remove a record by name. See party_records.h and docs/notes/world.md ("Party records and
// class templates").
#include "object_tables.h"
#include "party_records.h"
#include "party_manager.h"
#include "core_refcount.h"
#include "game_strings.h"
#include "class_templates.h"
#include "game_lists.h"
#include "party.h"

#include "lib/crt.h"
#include "game_hashes.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "lib/stlp_vector_refs.h"
#include "game_data.h"

#include <bit>

namespace {

int __cdecl MbsCmp(std::string_view a0, std::string_view a1) { return gamecrt::mbscmp(a0, a1); }

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

using Elements = stlp::CountedPtrElement<PartyRecord>;

void Release(PartyRecord* record) {
    if (record && --record->refs_ == 0) delete record;
}

const char* NameOf(const PartyRecord* record) { return record->name.c_str(); }

} // namespace

void __fastcall PartyRecordsReset(NamedPartyRecords* list) {
    PartyRecord** end = list->records.Finish();
    PartyRecord** newEnd = stlp::CopyAssigned<PartyRecord*, Elements>(end, end, list->records.Start());
    for (PartyRecord** p = newEnd; p != end; ++p) Release(*p);
    list->records.Finish() = newEnd;
    list->text0C = game::EmptyText();
    list->text10 = game::EmptyText();

    PartyRecord* temp = NewRecord();
    if (temp) ++temp->refs_;
    if (list->records.Finish() == list->records.EndOfStorage()) {
        list->records.InsertOverflow<Elements>(list->records.Finish(), temp, 1);
    } else {
        if (list->records.Finish()) Elements::Construct(list->records.Finish(), temp);
        ++list->records.Finish();
    }
    Release(temp);

    CParty* party = (*list->records.Start())->Party();
    party->AddMember(kHero, kHumanHero, game::EmptyText());
    party->zone_ = kFirstZone;
    party->entrance_ = 0;
    party->variables_.Set(kGameTime, std::bit_cast<float>(kSix));
}
EI_REWRITTEN(0x004A5BD0, PartyRecordsReset);

CParty* NamedPartyRecords::Find(const char* name) const {
    CParty* unnamed = nullptr;
    for (unsigned i = 0; i < static_cast<unsigned>(records.size()); ++i) {
        if (MbsCmp(NameOf(records.begin()[i]), name) == 0) return records.begin()[i]->Party();
        if (MbsCmp(NameOf(records.begin()[i]), game::EmptyText()) == 0) unnamed = records.begin()[i]->Party();
    }
    return unnamed;
}
EI_REWRITTEN(0x004A5E10, NamedPartyRecords::Find);

void NamedPartyRecords::Add(const char* name) {
    PartyRecord* record = NewRecord();
    record->name = name;
    PartyRecord* temp = record;
    if (temp) ++temp->refs_;
    if (records.Finish() == records.EndOfStorage()) {
        records.InsertOverflow<Elements>(records.Finish(), temp, 1);
    } else {
        if (records.Finish()) {
            *records.Finish() = temp;
            if (temp) ++temp->refs_;
        }
        ++records.Finish();
    }
    Release(temp);
}
EI_REWRITTEN(0x004A5E90, NamedPartyRecords::Add);

void NamedPartyRecords::Remove(const char* name) {
    for (unsigned i = 0; i < static_cast<unsigned>(records.Finish() - records.Start()); ++i) {
        if (MbsCmp(NameOf(records.Start()[i]), name) != 0) continue;
        // erase(i): the tail moved down one slot by counted assignment, the last slot released.
        // The index still advances, so the record moved into slot i is not tested.
        PartyRecord** p = records.Start() + i;
        if (p + 1 != records.Finish()) {
            for (int n = static_cast<int>(records.Finish() - (p + 1)); n > 0; --n, ++p) {
                PartyRecord* next = p[1];
                PartyRecord* old = p[0];
                p[0] = next;
                if (next) ++next->refs_;
                Release(old);
            }
        }
        --records.Finish();
        Release(*records.Finish());
    }
}
EI_REWRITTEN(0x004A5FA0, NamedPartyRecords::Remove);

// ---- The party manager -----------------------------------------------------------------

namespace {

// The party stored under `key` (find, inlined), null if none.
CParty* FindParty(const PartyMap& parties, int key) {
    PartyMap::Node* node = parties.Find(static_cast<std::uint32_t>(key));
    return node ? node->value.mapped.Get() : nullptr;
}

} // namespace

void __fastcall PartyManagerNewGame(PartyManager* manager) {
    PartyRecordsReset(&manager->records);
    manager->templates.Build(0);
}
EI_REWRITTEN(0x004A88A0, PartyManagerNewGame);

void __fastcall PartyManagerNewGameFour(PartyManager* manager) {
    PartyRecordsReset(&manager->records);
    manager->templates.Build(1);
}
EI_REWRITTEN(0x004A88C0, PartyManagerNewGameFour);

void PartyManager::BeginRedeploy(CParty* party) { records.BeginRedeploy(party); }
EI_REWRITTEN(0x004A8890, PartyManager::BeginRedeploy);

void PartyManager::EndRedeploy(CParty* party) { records.EndRedeploy(party); }
EI_REWRITTEN(0x004A8880, PartyManager::EndRedeploy);

int g_nextPartyId = /*0078B0E8*/ 1;

int __stdcall PartyNextId(int* out) {
    *out = g_nextPartyId;
    return ++g_nextPartyId;
}
EI_REWRITTEN(0x004A8910, PartyNextId);

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
EI_REWRITTEN(0x004A8790, PartyManager::LoadParty);

bool PartyManager::StoreParty(const int* key, CParty* source) {
    CParty* value = FindParty(parties, *key);
    if (!value) value = parties.At(key)->Get();
    value->Assign(source);
    return true;
}
EI_REWRITTEN(0x004A8800, PartyManager::StoreParty);
