// The named party records of the object at 007AA5A0 (+0x24): reset at new game (004A5BD0), and
// the script commands' helpers to find (004A5E10), add (004A5E90) and remove (004A5FA0) a record
// by name. See party_records.h and docs/notes/world.md ("Party records and class templates").
#include "party_records.h"
#include "core_refcount.h"
#include "game_strings.h"
#include "party.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "originals_gameplay.h"
#include "originals_engine.h"
#include "originals_library.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// 006E848B _mbscmp.
int __cdecl MbsCmp(const char* a0, const char* a1) { return GameMbscmp(a0, a1); }
// 00661050 constructor of the embedded party (returns this).
void* __fastcall PartyConstruct(void* a0) { return PartyRecordsConstructOriginal(a0); }
// Counted pointer helpers: 004AB910 copy(first, last, result) (__fastcall, returns the result's
// end), 004AB960 destructor (__thiscall (flags)), 004A5D70 release, 004A5D50 copy constructor,
// 004AB320 vector _M_insert_overflow(pos, x, n).
__declspec(naked) PartyRecord** __fastcall RecordCopy(PartyRecord**, PartyRecord**, PartyRecord**) EI_ORIGINAL(0x004AB910)
__declspec(naked) void __fastcall RecordPtrDestroy(PartyRecord**, void*, int) EI_ORIGINAL(0x004AB960)
__declspec(naked) void __fastcall RecordPtrRelease(PartyRecord**) EI_ORIGINAL(0x004A5D70)
void __fastcall RecordPtrCopy(PartyRecord** a0, void*, PartyRecord* const* a2) { CountedPtrCopyOriginal((void*)a0, nullptr, (void* const*)a2); }
__declspec(naked) void __fastcall RecordInsertOverflow(NamedPartyRecords*, void*, PartyRecord**, PartyRecord* const*,
                                                       unsigned) EI_ORIGINAL(0x004AB320)
// The embedded party: 006628D0 adds a member record (three strings), 00460BC0 sets a named
// variable (+0x54 table).
void __fastcall PartyAddMember(void* a0, void*, const char* a2, const char* a3, const char* a4) { PartyAddMemberOriginal(a0, nullptr, a2, a3, a4); }
void __fastcall PartySetVar(void* a0, void*, const char* a2, std::uint32_t a3) { NamedTableSet(a0, nullptr, a2, a3); }

const char* const kHero = reinterpret_cast<const char*>(0x0078B0F4);           // "Hero"
const char* const kHumanHero = reinterpret_cast<const char*>(0x0078B0FC);      // "Human Hero"
const char* const kFirstZone = reinterpret_cast<const char*>(0x0078B0EC);      // "gz1g"
const char* const kGameTime = reinterpret_cast<const char*>(0x0073D0BC);       // "gtime"
constexpr std::uint32_t kSix = 0x40C00000;                                     // 6.0f

// The record's inlined constructor (operator new's result may be null; the caller then writes
// through null as the original does).
PartyRecord* NewRecord() {
    auto* record = static_cast<PartyRecord*>(gamecrt::operator_new(sizeof(PartyRecord)));
    *reinterpret_cast<std::uint32_t*>(&record->refCount) = 0;
    if (!record) return nullptr;
    record->vtable = reinterpret_cast<void*>(0x0073BB68);
    PartyConstruct(record->party);
    *reinterpret_cast<void**>(&record->name) = *reinterpret_cast<void**>(0x00796E48);   // afxEmptyString
    record->vtable = reinterpret_cast<void*>(0x0073C360);
    reinterpret_cast<CParty*>(record->party)->Create();   // slot 2
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
    PartyRecord** newEnd = RecordCopy(end, end, list->records.Start());
    for (PartyRecord** p = newEnd; p != end; ++p) RecordPtrDestroy(p, nullptr, 0);
    list->records.Finish() = newEnd;
    CStringAssignTextOriginal(&list->text0C, nullptr, game::EmptyText());
    CStringAssignTextOriginal(&list->text10, nullptr, game::EmptyText());

    PartyRecord* temp = NewRecord();
    if (temp) ++temp->refCount;
    if (list->records.Finish() == list->records.EndOfStorage()) {
        RecordInsertOverflow(list, nullptr, list->records.Finish(), &temp, 1);
    } else {
        if (list->records.Finish()) RecordPtrCopy(list->records.Finish(), nullptr, &temp);
        ++list->records.Finish();
    }
    Release(temp);

    char* party = (*list->records.Start())->party;
    PartyAddMember(party, nullptr, kHero, kHumanHero, game::EmptyText());
    CStringAssignTextOriginal(party + 0x68, nullptr, kFirstZone);
    *reinterpret_cast<int*>(party + 0x6C) = 0;
    PartySetVar(party + 0x54, nullptr, kGameTime, kSix);
}
EI_REPLACE(0x004A5BD0, PartyRecordsReset);

void* NamedPartyRecords::Find(const char* name) const {
    void* unnamed = nullptr;
    for (unsigned i = 0; i < static_cast<unsigned>(records.size()); ++i) {
        if (MbsCmp(NameOf(records.begin()[i]), name) == 0) return records.begin()[i]->party;
        if (MbsCmp(NameOf(records.begin()[i]), game::EmptyText()) == 0) unnamed = records.begin()[i]->party;
    }
    return unnamed;
}
EI_REPLACE_METHOD(0x004A5E10, NamedPartyRecords::Find);

void NamedPartyRecords::Add(const char* name) {
    PartyRecord* record = NewRecord();
    CStringAssignTextOriginal(&record->name, nullptr, name);
    PartyRecord* temp = record;
    if (temp) ++temp->refCount;
    if (records.Finish() == records.EndOfStorage()) {
        RecordInsertOverflow(this, nullptr, records.Finish(), &temp, 1);
    } else {
        if (records.Finish()) {
            *records.Finish() = temp;
            if (temp) ++temp->refCount;
        }
        ++records.Finish();
    }
    Release(temp);
}
EI_REPLACE_METHOD(0x004A5E90, NamedPartyRecords::Add);

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
        RecordPtrRelease(records.Finish());
    }
}
EI_REPLACE_METHOD(0x004A5FA0, NamedPartyRecords::Remove);

// ---- The party manager (007AA5A0) -----------------------------------------------------------------

namespace {

// Through their addresses (the rewritten ones run their verify wrappers).
__declspec(naked) void __fastcall RecordsResetOrig(void*) EI_ORIGINAL(0x004A5BD0)
__declspec(naked) void __fastcall TemplatesBuildOrig(void*, void*, unsigned) EI_ORIGINAL(0x004A7630)
// 004A6050 / 004A6360 (not rewritten): copy the redeployed players' party into / out of the
// current record through a memory stream.
__declspec(naked) void __fastcall RecordsBeginRedeploy(void*, void*, void*) EI_ORIGINAL(0x004A6050)
__declspec(naked) void __fastcall RecordsEndRedeploy(void*, void*, void*) EI_ORIGINAL(0x004A6360)
// The party object: 00661300 CopyFrom(const Party*), 00661700 Reset(int, int).
inline void __fastcall PartyReset(void* a0, void*, int a2, int a3) { PartyResetOriginal((void*)a0, nullptr, a2, a3); }

char* Field(void* manager, std::size_t offset) { return static_cast<char*>(manager) + offset; }

// The node of `key` in an STLport hash_map<int, Party> (buckets +0x04..+0x08; node: next, key,
// value at +8), null if none.
struct MapNode {
    MapNode* next;
    int key;
};
MapNode* FindNode(const void* map, int key) {
    auto* buckets = *reinterpret_cast<MapNode* const* const*>(static_cast<const char*>(map) + 4);
    auto* bucketsEnd = *reinterpret_cast<MapNode* const* const*>(static_cast<const char*>(map) + 8);
    unsigned count = static_cast<unsigned>(bucketsEnd - buckets);
    for (MapNode* node = buckets[static_cast<unsigned>(key) % count]; node; node = node->next)
        if (node->key == key) return node;
    return nullptr;
}
void* NodeValue(MapNode* node) { return reinterpret_cast<char*>(node) + 8; }

} // namespace

void __fastcall PartyManagerNewGame(void* manager) {
    RecordsResetOrig(Field(manager, 0x24));
    TemplatesBuildOrig(Field(manager, 0x38), nullptr, 0);
}
EI_REPLACE(0x004A88A0, PartyManagerNewGame);

void __fastcall PartyManagerNewGameFour(void* manager) {
    RecordsResetOrig(Field(manager, 0x24));
    TemplatesBuildOrig(Field(manager, 0x38), nullptr, 1);
}
EI_REPLACE(0x004A88C0, PartyManagerNewGameFour);

void __fastcall PartyManagerBeginRedeploy(void* manager, void*, void* players) {
    RecordsBeginRedeploy(Field(manager, 0x24), nullptr, players);
}
EI_REPLACE(0x004A8890, PartyManagerBeginRedeploy);

void __fastcall PartyManagerEndRedeploy(void* manager, void*, void* players) {
    RecordsEndRedeploy(Field(manager, 0x24), nullptr, players);
}
EI_REPLACE(0x004A8880, PartyManagerEndRedeploy);

int __stdcall PartyNextId(int* out) {
    int& counter = *reinterpret_cast<int*>(0x0078B0E8);
    *out = counter;
    return ++counter;
}
EI_REPLACE(0x004A8910, PartyNextId);

bool __fastcall PartyMapLoad(const void* map, void*, int, int, const int* key, void* target) {
    if (*key == 0) {
        PartyReset(target, nullptr, 1, 0);
        return true;
    }
    MapNode* node = FindNode(map, *key);
    if (!node) return false;
    PartyCopyFrom(target, nullptr, NodeValue(node));
    return true;
}
EI_REPLACE(0x004A8790, PartyMapLoad);

bool __fastcall PartyMapStore(void* map, void*, const int* key, const void* source) {
    MapNode* node = FindNode(map, *key);
    void* value = node ? NodeValue(node) : PartyMapAtOriginal(map, nullptr, key);
    PartyCopyFrom(value, nullptr, source);
    return true;
}
EI_REPLACE(0x004A8800, PartyMapStore);
