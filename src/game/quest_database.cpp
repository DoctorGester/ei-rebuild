// The quest database's load of one quest (006AE8E0, thiscall on the database at 007C87A8, called
// by the quest registration 005810A0 with the image of "<name>.quest.reg"): the "quest" section
// becomes a quest record (name, exp, money, zone index, order, the "{x;y}" quest places) and the
// three briefing sections ("briefing receive", "briefing reject", "briefing complete") briefing
// records (name, exp, money, six string lists); each record is added unless its name is already
// there. The records and the database's containers are STLport objects handled through the
// original members. Names provisional; see docs/notes/items_spells.md ("Quest database").
#include "console.h"
#include "quest_records.h"
#include "game_strings.h"
#include "registry.h"

#include "../lib/mfc.h"
#include "ei/hook.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {


// The records are built in raw stack buffers (their constructors are the original's own calls).
constexpr std::size_t kQuestSize = sizeof(QuestRecord);
constexpr std::size_t kBriefingSize = sizeof(QuestBriefingRecord);

// The section and key names, by their original addresses.
StringArg Copy(const char* text) {
    StringArg arg;
    new (&arg) mfc::CString(text);
    return arg;
}

// The registry, through its game addresses (registry.cpp).
struct Registry : RegistryOriginals {
    double GetFloat(const char* section, const char* key, std::uint32_t defaultBits); // 00694C80
    int GetInt(const char* section, const char* key, int defaultValue);              // 00694B90

    std::uint8_t storage[sizeof(CFileRegistry)];
};
__declspec(naked) double Registry::GetFloat(const char*, const char*, std::uint32_t) EI_ORIGINAL(0x00694C80)
__declspec(naked) int Registry::GetInt(const char*, const char*, int) EI_ORIGINAL(0x00694B90)

// The quest places: vector<float> constructor [006AFB00, allocator] and insert-overflow [006ADD50:
// position, value, count].
__declspec(naked) void __fastcall PlacesConstruct(void*, void*, const void*) EI_ORIGINAL(0x006AFB00)
__declspec(naked) void __fastcall PlacesInsertOverflow(void*, void*, float*, const float*, int) EI_ORIGINAL(0x006ADD50)
// The briefing lists: constructors [0053F4F0 for the first, 0055A3E0 for the others; allocator],
// erase a range [006AFB60], clear [0052E010], reserve [006B02B0], push_back from a CString
// [006AF790], and the reader of a registry string list into one [006AE740: registry, section; key,
// list].
__declspec(naked) void __fastcall ListConstruct(void*, void*, const void*) EI_ORIGINAL(0x0055A3E0)
__declspec(naked) void __fastcall ListErase(void*, void*, void*, void*) EI_ORIGINAL(0x006AFB60)
void __fastcall ListClear(void* a0) { CStringVectorClearOriginal(a0); }
__declspec(naked) void __fastcall ListReserve(void*, void*, int) EI_ORIGINAL(0x006B02B0)
__declspec(naked) void __fastcall ListPushBack(void*, void*, const mfc::CString*) EI_ORIGINAL(0x006AF790)
// Called through its address so the wrapper of the rewrite below (ReadStringList) runs.
__declspec(naked) void __fastcall ReadList(Registry*, const char*, const char*, void*) EI_ORIGINAL(0x006AE740)
// The record destructors [quest 006AF4D0, briefing 006AF5A0].
void __fastcall QuestDestroy(void* record) { static_cast<QuestRecord*>(record)->Destruct(); }
void __fastcall BriefingDestroy(void* record) { static_cast<QuestBriefingRecord*>(record)->Destruct(); }
// The quest index: find by the record's name [006B0870: out iterator, record], the end iterator
// [006B0B10: node, map], push_back [006B0710], clear [006B0520], operator[] [0064EE90: the record,
// returns the value slot]. The briefing index: 006B07C0, 006B0B30, 006B0780, 006B0590, 004297A0,
// and the add-unless-present used for the later briefings [006B0600].
__declspec(naked) void __fastcall QuestFind(void*, void*, void**, const void*) EI_ORIGINAL(0x006B0870)
__declspec(naked) void __fastcall QuestIterator(void**, void*, void*, void*) EI_ORIGINAL(0x006B0B10)
__declspec(naked) void __fastcall QuestPushBack(void*, void*, const void*) EI_ORIGINAL(0x006B0710)
__declspec(naked) void __fastcall QuestIndexClear(void*) EI_ORIGINAL(0x006B0520)
__declspec(naked) void __fastcall BriefingFind(void*, void*, void**, const void*) EI_ORIGINAL(0x006B07C0)
__declspec(naked) void __fastcall BriefingIterator(void**, void*, void*, void*) EI_ORIGINAL(0x006B0B30)
__declspec(naked) void __fastcall BriefingPushBack(void*, void*, const void*) EI_ORIGINAL(0x006B0780)
__declspec(naked) void __fastcall BriefingIndexClear(void*) EI_ORIGINAL(0x006B0590)
__declspec(naked) void __fastcall BriefingAdd(void*, void*, const void*) EI_ORIGINAL(0x006B0600)
// The briefing index's find by a record's name [006B0930, RET 8: out iterator (node, table), key].
__declspec(naked) void __fastcall BriefingFindNode(void*, void*, void**, const void*) EI_ORIGINAL(0x006B0930)

// An allocator argument: the members read at most a byte through it.
const std::uint8_t kAllocator = 0;

CStringVector* List(QuestBriefingRecord* briefing, int i) { return &briefing->lists[i]; }
int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// push_back on the quest places as inlined: the value stored at the end, or the insert-overflow
// path when the vector is full.
void PushPlace(QuestRecord* quest, float value) {
    float*& finish = quest->places.Finish();
    if (finish == quest->places.EndOfStorage()) {
        PlacesInsertOverflow(&quest->places, nullptr, finish, &value, 1);
        return;
    }
    if (finish) std::memcpy(finish, &value, 4);
    ++finish;
}

// The "quest" section into the quest record.
void ReadQuest(Registry& reg, QuestRecord* quest, console::CStringArray& places) {
    quest->field2 = 0;
    {
        mfc::CString name = reg.GetString(game::questwords::Quest(), game::questwords::Name(), Copy(game::EmptyText()));
        const char* text = name;
        StringAssignRangeOriginal(&quest->name, nullptr, text, text + std::strlen(text));
    }
    quest->exp = static_cast<float>(reg.GetFloat(game::questwords::Quest(), game::questwords::Exp(), 0));
    quest->money = static_cast<float>(reg.GetFloat(game::questwords::Quest(), game::questwords::Money(), 0));
    quest->zone = reg.GetInt(game::questwords::Quest(), game::questwords::ZoneIndex(), 0);
    quest->order = reg.GetInt(game::questwords::Quest(), game::questwords::Order(), 0);
    reg.GetStringArray(game::questwords::Quest(), game::questwords::QuestPlaces(), &places);
    // Each place "{x;y}": the braces trimmed, split at ';'; with two parts x and y are appended.
    for (int i = 0; i < places.GetSize(); ++i) {
        console::CStringArray parts;
        CStringTrimLeftCharOriginal(&places[i], nullptr, '{');
        CStringTrimRightCharOriginal(&places[i], nullptr, '}');
        StringSplit(places[i], &parts, ';');
        if (parts.GetSize() == 2) {
            float x = static_cast<float>(GameAtof(parts[0]));
            PushPlace(quest, x);
            float y = static_cast<float>(GameAtof(parts[1]));
            PushPlace(quest, y);
        }
    }
}

// Adds the quest unless its name is indexed with a record already; the index is rebuilt over the
// whole vector after the push_back (it may have moved the records).
void AddQuest(QuestDatabase* db, const QuestRecord* quest) {
    void* map = &db->questsByName;
    void* found[2];
    QuestFind(map, nullptr, found, quest);
    void* end[2];
    QuestIterator(end, nullptr, nullptr, map);
    if (found[0] != end[0] && static_cast<const QuestDatabase::QuestIndex::Node*>(found[0])->value != nullptr) return;
    QuestPushBack(&db->quests, nullptr, quest);
    QuestIndexClear(map);
    for (QuestRecord* r = db->quests.begin(); r != db->quests.end(); ++r) {
        *QuestIndexAt(map, nullptr, r) = r;
    }
}

void AddReceiveBriefing(QuestDatabase* db, const QuestBriefingRecord* briefing) {
    QuestBriefingTable* briefings = &db->briefings;
    void* map = &briefings->index;
    void* found[2];
    BriefingFind(map, nullptr, found, briefing);
    void* end[2];
    BriefingIterator(end, nullptr, nullptr, map);
    if (found[0] != end[0] && static_cast<const QuestBriefingTable::Index::Node*>(found[0])->value != nullptr) return;
    BriefingPushBack(&briefings->records, nullptr, briefing);
    BriefingIndexClear(map);
    for (QuestBriefingRecord* r = briefings->records.begin(); r != briefings->records.end(); ++r) {
        *(void**)MapRegistryFindOriginal((void*)map, nullptr, r) = r;
    }
}

// A briefing's name, exp and money; `assignChars` uses the C-string assignment (the "complete"
// briefing) instead of the range one.
void ReadBriefingHeader(Registry& reg, QuestBriefingRecord* briefing, const char* section, bool assignChars) {
    {
        mfc::CString name = reg.GetString(section, game::questwords::Name(), Copy(game::EmptyText()));
        const char* text = name;
        if (assignChars) {
            StringAssignCStrOriginal(&briefing->name, nullptr, text);
        } else {
            StringAssignRangeOriginal(&briefing->name, nullptr, text, text + std::strlen(text));
        }
    }
    briefing->exp = static_cast<float>(reg.GetFloat(section, game::questwords::Exp(), 0));
    briefing->money = static_cast<float>(reg.GetFloat(section, game::questwords::Money(), 0));
}

// A string list read inline: emptied (erased over its range, or cleared), reserved for the list's
// size, then every non-empty string appended.
void ReadListInline(Registry& reg, const char* section, int list, QuestBriefingRecord* briefing, bool erase) {
    console::CStringArray strings;
    reg.GetStringArray(section, game::questwords::ListKey(list), &strings);
    CStringVector* v = List(briefing, list);
    if (erase) {
        ListErase(v, nullptr, v->begin(), v->end());
    } else {
        ListClear(v);
    }
    ListReserve(v, nullptr, strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (Length(strings[i]) != 0) ListPushBack(v, nullptr, &strings[i]);
    }
}

void ConstructBriefing(QuestBriefingRecord* briefing) {
    StringInitEmpty(&briefing->name);
    FirstListConstruct(List(briefing, 0), nullptr, &kAllocator);
    for (int i = 1; i < 6; ++i) ListConstruct(List(briefing, i), nullptr, &kAllocator);
    StringInitEmpty(&briefing->text);
}

// "briefing receive": the lists read inline (the first three erased over their range, the others
// cleared), added like the quest.
void ReadReceiveBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    briefing->field10 = 0;
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingReceive(), false);
    for (int i = 0; i < 6; ++i) ReadListInline(reg, game::questwords::BriefingReceive(), i, briefing, i < 3);
    AddReceiveBriefing(db, briefing);
}

// "briefing reject": the item lists read inline (cleared first), the others by 006AE740; added by
// 006B0600 (also unless the name is there).
void ReadRejectBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingReject(), false);
    ReadListInline(reg, game::questwords::BriefingReject(), 0, briefing, false);
    ReadListInline(reg, game::questwords::BriefingReject(), 1, briefing, false);
    for (int i = 2; i < 6; ++i) ReadList(&reg, game::questwords::BriefingReject(), game::questwords::ListKey(i), List(briefing, i));
    BriefingAdd(&db->briefings, nullptr, briefing);
}

// "briefing complete": the name assigned as a C string, all six lists by 006AE740.
void ReadCompleteBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingComplete(), true);
    for (int i = 0; i < 6; ++i) ReadList(&reg, game::questwords::BriefingComplete(), game::questwords::ListKey(i), List(briefing, i));
    BriefingAdd(&db->briefings, nullptr, briefing);
}

} // namespace

// 006AE8E0 (thiscall on the database, RET 4). `image` is the quest's registry file in memory.
// Returns false when it is not a registry, true otherwise.
bool __fastcall QuestDatabaseLoad(char* database, void*, const void* image) {
    auto* db = reinterpret_cast<QuestDatabase*>(database);
    Registry reg;
    reg.Construct();
    if (!reg.Attach(image)) {
        reg.Destroy();
        return false;
    }

    alignas(4) char questStorage[kQuestSize];
    auto* quest = reinterpret_cast<QuestRecord*>(questStorage);
    StringInitEmpty(&quest->name);
    PlacesConstruct(&quest->places, nullptr, &kAllocator);
    StringInitEmpty(&quest->text);
    {
        console::CStringArray places;
        ReadQuest(reg, quest, places);
        AddQuest(db, quest);

        alignas(4) char briefingStorage[kBriefingSize];
        auto* briefing = reinterpret_cast<QuestBriefingRecord*>(briefingStorage);
        ConstructBriefing(briefing);
        ReadReceiveBriefing(reg, db, briefing);
        ReadRejectBriefing(reg, db, briefing);
        ReadCompleteBriefing(reg, db, briefing);
        BriefingDestroy(briefing);
    }
    QuestDestroy(quest);
    reg.Destroy();
    return true;
}
EI_REPLACE(0x006AE8E0, QuestDatabaseLoad);

// 006AE740 (A-H19). The original inlines the vector's erase, reserve and push_back; the same
// template members are called here.
void __fastcall ReadStringList(CFileRegistry* registry, const char* section, const char* key, CStringVector* list) {
    console::CStringArray strings;
    registry->GetStringArray(section, key, &strings);
    ListErase(list, nullptr, list->begin(), list->end());
    ListReserve(list, nullptr, strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (Length(strings[i]) != 0) ListPushBack(list, nullptr, &strings[i]);
    }
}
EI_REPLACE(0x006AE740, ReadStringList);

// --- 006B0600 (A-H25): a briefing added unless its name is there ---------------------------------------

bool QuestBriefingTable::Add(const QuestBriefingRecord* record) {
    if (!record) return false;
    void* iterator[2];
    BriefingFindNode(&index, nullptr, iterator, record);
    const Index::Node* node = static_cast<const Index::Node*>(iterator[0]);
    if (node && node->value) return false;
    BriefingPushBack(&records, nullptr, record);
    BriefingIndexClear(&index);
    for (QuestBriefingRecord* r = records.begin(); r != records.end(); ++r) {
        *(void**)MapRegistryFindOriginal((void*)&index, nullptr, r) = r;
    }
    return true;
}
EI_REPLACE_METHOD(0x006B0600, QuestBriefingTable::Add);
