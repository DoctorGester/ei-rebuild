// The named party records of the object at 007AA5A0 (+0x24): reset at new game (004A5BD0), and
// the script commands' helpers to find (004A5E10), add (004A5E90) and remove (004A5FA0) a record
// by name. See party_records.h and docs/notes/world.md ("Party records and class templates").
#include "object_tables.h"
#include "party_records.h"
#include "core_refcount.h"
#include "game_strings.h"
#include "class_templates.h"
#include "game_lists.h"
#include "party.h"

#include "lib/crt.h"
#include "game_hashes.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_gameplay.h"
#include "originals_engine.h"
#include "originals_library.h"
#include "lib/stlp_vector_refs.h"
#include "game_data.h"

#include <bit>

#pragma warning(disable : 4740) // naked forwarders

namespace {

// 006E848B _mbscmp.
int __cdecl MbsCmp(const char* a0, const char* a1) { return GameMbscmp(a0, a1); }
// Counted pointer helpers: 004AB910 copy(first, last, result) (__fastcall, returns the result's
// end), 004AB960 destructor (__thiscall (flags)), 004A5D70 release, 004A5D50 copy constructor,
// 004AB320 vector _M_insert_overflow(pos, x, n) (stlp::CountedRefVector's).
// The embedded party: 006628D0 adds a member record (three strings), 00460BC0 sets a named
// variable (+0x54 table).

const char* const kHero = "Hero";
const char* const kHumanHero = "Human Hero";
const char* const kFirstZone = "gz1g";
const char* const kGameTime = "gtime";
constexpr std::uint32_t kSix = 0x40C00000;                                     // 6.0f

// The record's inlined constructor (operator new's result may be null; the caller then writes
// through null as the original does).
PartyRecord* NewRecord() {
    auto* record = gamecrt::operator_new_as<PartyRecord>();
    *reinterpret_cast<std::uint32_t*>(&record->refCount) = 0;
    if (!record) return nullptr;
    record->vtable = ei::GamePointer<void>(0x0073BB68);
    record->Party()->Construct();
    ::new (&record->name) mfc::CString;   // afxEmptyString
    record->vtable = PartyRecord::Vtable();
    record->Party()->Create();   // slot 2
    return record;
}

void Release(PartyRecord* record) {
    // A counted object (base vtable 0073BB68): slot 0, the deleting destructor, with 1 at zero.
    if (record && --record->refCount == 0) delete reinterpret_cast<CRefCounted*>(record);
}

const char* NameOf(const PartyRecord* record) { return *reinterpret_cast<const char* const*>(&record->name); }

} // namespace

void __fastcall PartyRecordsReset(NamedPartyRecords* list) {
    PartyRecord** end = list->records.Finish();
    auto* newEnd = reinterpret_cast<PartyRecord**>(stlp::CopyAssigned<RefPtr, stlp::CountedRefElement>(
        stlp::AsRefPtr(end), stlp::AsRefPtr(end), stlp::AsRefPtr(list->records.Start())));   // 004AB910
    for (PartyRecord** p = newEnd; p != end; ++p) reinterpret_cast<RefPtr*>(p)->DeleteScalar(0);   // 004AB960
    list->records.Finish() = newEnd;
    list->text0C = game::EmptyText();
    list->text10 = game::EmptyText();

    PartyRecord* temp = NewRecord();
    if (temp) ++temp->refCount;
    if (list->records.Finish() == list->records.EndOfStorage()) {
        stlp::AsRefPtrs(list->records).InsertOverflow<stlp::CountedRefElement>(stlp::AsRefPtr(list->records.Finish()), stlp::AsRefPtr(temp), 1);
    } else {
        if (list->records.Finish()) reinterpret_cast<RefPtr*>(list->records.Finish())->CopyCounted(reinterpret_cast<const RefPtr*>(&temp));
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
    if (temp) ++temp->refCount;
    if (records.Finish() == records.EndOfStorage()) {
        stlp::AsRefPtrs(records).InsertOverflow<stlp::CountedRefElement>(stlp::AsRefPtr(records.Finish()), stlp::AsRefPtr(temp), 1);
    } else {
        if (records.Finish()) {
            *records.Finish() = temp;
            if (temp) ++temp->refCount;
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
                if (next) ++next->refCount;
                Release(old);
            }
        }
        --records.Finish();
        RefPtrRelease(reinterpret_cast<RefPtr*>(records.Finish()));   // 004A5D70
    }
}
EI_REWRITTEN(0x004A5FA0, NamedPartyRecords::Remove);

// ---- The party manager (007AA5A0) -----------------------------------------------------------------

namespace {

// The party object: 00661300 CopyFrom(const Party*), 00661700 Reset(int, int).

char* Field(void* manager, std::size_t offset) { return static_cast<char*>(manager) + offset; }

// The party stored under `key` (find, inlined), null if none.
CParty* FindParty(const PartyMap& parties, int key) {
    PartyMap::Node* node = parties.Find(static_cast<std::uint32_t>(key));
    return node ? node->value.mapped.Get() : nullptr;
}

} // namespace

void __fastcall PartyManagerNewGame(void* manager) {
    PartyRecordsReset(reinterpret_cast<NamedPartyRecords*>(Field(manager, 0x24)));
    reinterpret_cast<ClassTemplateVector*>(Field(manager, 0x38))->Build(0);
}
EI_REPLACE(0x004A88A0, PartyManagerNewGame);

void __fastcall PartyManagerNewGameFour(void* manager) {
    PartyRecordsReset(reinterpret_cast<NamedPartyRecords*>(Field(manager, 0x24)));
    reinterpret_cast<ClassTemplateVector*>(Field(manager, 0x38))->Build(1);
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

bool PartyManager::LoadParty(int, int, const int* key, CParty* target) const {
    if (*key == 0) {
        PartyResetOriginal((void*)target, nullptr, 1, 0);
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
    if (!value) value = parties.At(key)->Get();   // 004A91B0
    value->Assign(source);
    return true;
}
EI_REWRITTEN(0x004A8800, PartyManager::StoreParty);
