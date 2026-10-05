// The named party records of the object at 007AA5A0 (+0x24): reset at new game (004A5BD0), and
// the script commands' helpers to find (004A5E10), add (004A5E90) and remove (004A5FA0) a record
// by name. See party_records.h and docs/notes/range_490.md ("Party records").
#include "party_records.h"

#include "lib/crt.h"
#include "lib/mfc.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "originals_gameplay.h"
#include "originals_engine.h"
#include "originals_library.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

// 006E848B _mbscmp.
int __cdecl MbsCmp(const char* a0, const char* a1) { return GameMbscmp(a0, a1); }
// 00661050 constructor of the embedded party (returns this).
__declspec(naked) void* __fastcall PartyConstruct(void*) EI_ORIGINAL(0x00661050)
// 00700AF9 CString::operator=(LPCTSTR) on a raw CString slot.
inline void __fastcall AssignText(void* s, void*, const char* t) { CStringAssignTextOriginal(s, nullptr, t); }
// Counted pointer helpers: 004AB910 copy(first, last, result) (__fastcall, returns the result's
// end), 004AB960 destructor (__thiscall (flags)), 004A5D70 release, 004A5D50 copy constructor,
// 004AB320 vector _M_insert_overflow(pos, x, n).
__declspec(naked) PartyRecord** __fastcall RecordCopy(PartyRecord**, PartyRecord**, PartyRecord**) EI_ORIGINAL(0x004AB910)
__declspec(naked) void __fastcall RecordPtrDestroy(PartyRecord**, void*, int) EI_ORIGINAL(0x004AB960)
__declspec(naked) void __fastcall RecordPtrRelease(PartyRecord**) EI_ORIGINAL(0x004A5D70)
__declspec(naked) void __fastcall RecordPtrCopy(PartyRecord**, void*, PartyRecord* const*) EI_ORIGINAL(0x004A5D50)
__declspec(naked) void __fastcall RecordInsertOverflow(PartyRecordList*, void*, PartyRecord**, PartyRecord* const*,
                                                       unsigned) EI_ORIGINAL(0x004AB320)
// The embedded party: 006628D0 adds a member record (three strings), 00460BC0 sets a named
// variable (+0x54 table).
__declspec(naked) void __fastcall PartyAddMember(void*, void*, const char*, const char*, const char*) EI_ORIGINAL(0x006628D0)
void __fastcall PartySetVar(void* a0, void*, const char* a2, std::uint32_t a3) { NamedTableSet(a0, nullptr, a2, a3); }

const char* const kEmpty = reinterpret_cast<const char*>(0x0079B298);
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
    ei::VCall<void>(record->party, 2);
    return record;
}

void Release(PartyRecord* record) {
    if (record && --record->refCount == 0 && record) ei::VCall<void>(record, 0, 1);
}

const char* NameOf(const PartyRecord* record) { return *reinterpret_cast<const char* const*>(&record->name); }

} // namespace

void __fastcall PartyRecordsReset(PartyRecordList* list) {
    PartyRecord** end = list->finish;
    PartyRecord** newEnd = RecordCopy(end, end, list->start);
    for (PartyRecord** p = newEnd; p != end; ++p) RecordPtrDestroy(p, nullptr, 0);
    list->finish = newEnd;
    AssignText(&list->text0C, nullptr, kEmpty);
    AssignText(&list->text10, nullptr, kEmpty);

    PartyRecord* temp = NewRecord();
    if (temp) ++temp->refCount;
    if (list->finish == list->eos) {
        RecordInsertOverflow(list, nullptr, list->finish, &temp, 1);
    } else {
        if (list->finish) RecordPtrCopy(list->finish, nullptr, &temp);
        ++list->finish;
    }
    Release(temp);

    char* party = (*list->start)->party;
    PartyAddMember(party, nullptr, kHero, kHumanHero, kEmpty);
    AssignText(party + 0x68, nullptr, kFirstZone);
    *reinterpret_cast<int*>(party + 0x6C) = 0;
    PartySetVar(party + 0x54, nullptr, kGameTime, kSix);
}
EI_REPLACE(0x004A5BD0, PartyRecordsReset);

void* __fastcall PartyRecordsFind(const PartyRecordList* list, void*, const char* name) {
    void* unnamed = nullptr;
    for (unsigned i = 0; i < static_cast<unsigned>(list->finish - list->start); ++i) {
        if (MbsCmp(NameOf(list->start[i]), name) == 0) return list->start[i]->party;
        if (MbsCmp(NameOf(list->start[i]), kEmpty) == 0) unnamed = list->start[i]->party;
    }
    return unnamed;
}
EI_REPLACE(0x004A5E10, PartyRecordsFind);

void __fastcall PartyRecordsAdd(PartyRecordList* list, void*, const char* name) {
    PartyRecord* record = NewRecord();
    AssignText(&record->name, nullptr, name);
    PartyRecord* temp = record;
    if (temp) ++temp->refCount;
    if (list->finish == list->eos) {
        RecordInsertOverflow(list, nullptr, list->finish, &temp, 1);
    } else {
        if (list->finish) {
            *list->finish = temp;
            if (temp) ++temp->refCount;
        }
        ++list->finish;
    }
    Release(temp);
}
EI_REPLACE(0x004A5E90, PartyRecordsAdd);

void __fastcall PartyRecordsRemove(PartyRecordList* list, void*, const char* name) {
    for (unsigned i = 0; i < static_cast<unsigned>(list->finish - list->start); ++i) {
        if (MbsCmp(NameOf(list->start[i]), name) != 0) continue;
        // erase(i): the tail moved down one slot by counted assignment, the last slot released.
        // The index still advances, so the record moved into slot i is not tested.
        PartyRecord** p = list->start + i;
        if (p + 1 != list->finish) {
            for (int n = static_cast<int>(list->finish - (p + 1)); n > 0; --n, ++p) {
                PartyRecord* next = p[1];
                PartyRecord* old = p[0];
                p[0] = next;
                if (next) ++next->refCount;
                Release(old);
            }
        }
        --list->finish;
        RecordPtrRelease(list->finish);
    }
}
EI_REPLACE(0x004A5FA0, PartyRecordsRemove);

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
// 004A91B0: hash_map<int, Party>::operator[] (inserts a default party for a new key).
__declspec(naked) void* __fastcall PartyMapAt(void*, void*, const int*) EI_ORIGINAL(0x004A91B0)

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
    void* value = node ? NodeValue(node) : PartyMapAt(map, nullptr, key);
    PartyCopyFrom(value, nullptr, source);
    return true;
}
EI_REPLACE(0x004A8800, PartyMapStore);
