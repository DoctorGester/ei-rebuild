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

template <typename T>
T& At(void* base, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<char*>(base) + offset);
}

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

// The record layouts (raw: their members are the original's STLport containers).
// Quest (0x38): +0 name (string), +0xC exp, +0x10 a dword (0), +0x14 money, +0x18 zone index,
// +0x1C order, +0x20 the quest places (vector<float>, x and y in turn), +0x2C a string.
constexpr std::size_t kQuestSize = 0x38;
constexpr std::size_t kQuestExp = 0x0C;
constexpr std::size_t kQuestUnknown10 = 0x10;
constexpr std::size_t kQuestMoney = 0x14;
constexpr std::size_t kQuestZone = 0x18;
constexpr std::size_t kQuestOrder = 0x1C;
constexpr std::size_t kQuestPlaces = 0x20;
constexpr std::size_t kQuestString2C = 0x2C;
// Briefing (0x6C): +0 name, +0xC exp, +0x10 money, six vectors of CString (give items, remove
// items, give quests, complete quests, available zones, complete zones), +0x5C a string, +0x68 a
// dword (0).
constexpr std::size_t kBriefingSize = 0x6C;
constexpr std::size_t kBriefingExp = 0x0C;
constexpr std::size_t kBriefingMoney = 0x10;
constexpr std::size_t kBriefingLists = 0x14;   // six, 12 bytes apart
constexpr std::size_t kBriefingString5C = 0x5C;
constexpr std::size_t kBriefingUnknown68 = 0x68;

// The database: +0 the quests (vector), +0xC their index (hash map by name); +0x20 the briefings,
// +0x2C their index.
constexpr std::size_t kQuests = 0x00;
constexpr std::size_t kQuestIndex = 0x0C;
constexpr std::size_t kBriefings = 0x20;
constexpr std::size_t kBriefingIndex = 0x2C;

// The section and key names, by their original addresses.
constexpr std::uintptr_t kName = 0x007967C4;           // "name"
constexpr std::uintptr_t kQuest = 0x007967BC;          // "quest"
constexpr std::uintptr_t kExp = 0x0079275C;            // "exp"
constexpr std::uintptr_t kMoney = 0x00792760;          // "money"
constexpr std::uintptr_t kZoneIndex = 0x007967B0;      // "zone index"
constexpr std::uintptr_t kOrder = 0x007967A8;          // "order"
constexpr std::uintptr_t kQuestPlacesKey = 0x0079679C; // "questplaces"
constexpr std::uintptr_t kReceive = 0x00796788;        // "briefing receive"
constexpr std::uintptr_t kReject = 0x00796720;         // "briefing reject"
constexpr std::uintptr_t kComplete = 0x0079670C;       // "briefing complete"
constexpr std::uintptr_t kListKeys[6] = {
    0x0079677C,   // "give items"
    0x0079676C,   // "remove items"
    0x00796760,   // "give quests"
    0x00796750,   // "complete quests"
    0x00796740,   // "available zones"
    0x00796730,   // "complete zones"
};

// A CString passed by value: copy-constructed into the argument slot, destroyed by the callee.
struct StringArg {
    char* data;
};
StringArg Copy(const char* text) {
    StringArg arg;
    new (&arg) mfc::CString(text);
    return arg;
}

// The registry, through its game addresses (registry.cpp).
struct Registry {
    CFileRegistry* Construct();                                                       // 00694520
    void Destroy();                                                                   // 00694540
    mfc::BOOL Attach(const void* image);                                              // 00694610
    double GetFloat(const char* section, const char* key, std::uint32_t defaultBits); // 00694C80
    int GetInt(const char* section, const char* key, int defaultValue);              // 00694B90
    mfc::CString GetString(const char* section, const char* key, StringArg defaultValue);   // 00694D20
    mfc::BOOL GetStringArray(const char* section, const char* key, console::CStringArray* out);  // 006958E0

    std::uint8_t storage[sizeof(CFileRegistry)];
};
__declspec(naked) CFileRegistry* Registry::Construct() EI_ORIGINAL(0x00694520)
__declspec(naked) void Registry::Destroy() EI_ORIGINAL(0x00694540)
__declspec(naked) mfc::BOOL Registry::Attach(const void*) EI_ORIGINAL(0x00694610)
__declspec(naked) double Registry::GetFloat(const char*, const char*, std::uint32_t) EI_ORIGINAL(0x00694C80)
__declspec(naked) int Registry::GetInt(const char*, const char*, int) EI_ORIGINAL(0x00694B90)
__declspec(naked) mfc::CString Registry::GetString(const char*, const char*, StringArg) EI_ORIGINAL(0x00694D20)
__declspec(naked) mfc::BOOL Registry::GetStringArray(const char*, const char*, console::CStringArray*) EI_ORIGINAL(0x006958E0)

// STLport string members: default constructor [0041F460], assign a range [00426A80], assign a
// C string [0041F550].
void __fastcall StringAssignRange(void* a0, void*, const char* a2, const char* a3) { StringAssignRangeOriginal(a0, nullptr, a2, a3); }
void __fastcall StringAssign(void* a0, void*, const char* a2) { StringAssignCStrOriginal(a0, nullptr, a2); }
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
void __fastcall QuestDestroy(void* record) { QuestRecordDestroyOriginal(record); }
void __fastcall BriefingDestroy(void* record) { BriefingRecordDestroyOriginal(record); }
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
inline void** __fastcall BriefingIndexAt(void* a0, void*, const void* a2) { return (void**)MapRegistryFindOriginal((void*)a0, nullptr, a2); }
__declspec(naked) void __fastcall BriefingAdd(void*, void*, const void*) EI_ORIGINAL(0x006B0600)
// MFC: CString TrimLeft / TrimRight of one character [006FE1D3 / 006FE0DD]; the game's splitter of
// a string at a separator into a CStringArray [004DBE40, __fastcall: text, out; separator]; atof.
void __fastcall TrimLeftChar(mfc::CString* a0, void*, char a2) { CStringTrimLeftCharOriginal((void*)a0, nullptr, a2); }
void __fastcall TrimRightChar(mfc::CString* a0, void*, char a2) { CStringTrimRightCharOriginal((void*)a0, nullptr, a2); }

// An allocator argument: the members read at most a byte through it.
const std::uint8_t kAllocator = 0;

char* List(char* briefing, int i) { return briefing + kBriefingLists + i * 12; }
int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// push_back on the quest places as inlined: the value stored at the end, or the insert-overflow
// path when the vector is full.
void PushPlace(char* quest, float value) {
    float*& finish = At<float*>(quest, kQuestPlaces + 4);
    if (finish == At<float*>(quest, kQuestPlaces + 8)) {
        PlacesInsertOverflow(quest + kQuestPlaces, nullptr, finish, &value, 1);
        return;
    }
    if (finish) std::memcpy(finish, &value, 4);
    ++finish;
}

// The "quest" section into the quest record.
void ReadQuest(Registry& reg, char* quest, console::CStringArray& places) {
    At<std::uint32_t>(quest, kQuestUnknown10) = 0;
    {
        mfc::CString name = reg.GetString(Text(kQuest), Text(kName), Copy(game::EmptyText()));
        const char* text = name;
        StringAssignRange(quest, nullptr, text, text + std::strlen(text));
    }
    At<float>(quest, kQuestExp) = static_cast<float>(reg.GetFloat(Text(kQuest), Text(kExp), 0));
    At<float>(quest, kQuestMoney) = static_cast<float>(reg.GetFloat(Text(kQuest), Text(kMoney), 0));
    At<int>(quest, kQuestZone) = reg.GetInt(Text(kQuest), Text(kZoneIndex), 0);
    At<int>(quest, kQuestOrder) = reg.GetInt(Text(kQuest), Text(kOrder), 0);
    reg.GetStringArray(Text(kQuest), Text(kQuestPlacesKey), &places);
    // Each place "{x;y}": the braces trimmed, split at ';'; with two parts x and y are appended.
    for (int i = 0; i < places.GetSize(); ++i) {
        console::CStringArray parts;
        TrimLeftChar(&places[i], nullptr, '{');
        TrimRightChar(&places[i], nullptr, '}');
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
void AddQuest(char* db, const char* quest) {
    void* map = db + kQuestIndex;
    void* found[2];
    QuestFind(map, nullptr, found, quest);
    void* end[2];
    QuestIterator(end, nullptr, nullptr, map);
    if (found[0] != end[0] && At<void*>(found[0], 0x10) != nullptr) return;
    QuestPushBack(db + kQuests, nullptr, quest);
    QuestIndexClear(map);
    for (char* r = At<char*>(db, kQuests); r != At<char*>(db, kQuests + 4); r += kQuestSize) {
        *QuestIndexAt(map, nullptr, r) = r;
    }
}

void AddReceiveBriefing(char* db, const char* briefing) {
    char* briefings = db + kBriefings;
    void* map = briefings + 0xC;
    void* found[2];
    BriefingFind(map, nullptr, found, briefing);
    void* end[2];
    BriefingIterator(end, nullptr, nullptr, map);
    if (found[0] != end[0] && At<void*>(found[0], 0x10) != nullptr) return;
    BriefingPushBack(briefings, nullptr, briefing);
    BriefingIndexClear(map);
    for (char* r = At<char*>(briefings, 0); r != At<char*>(briefings, 4); r += kBriefingSize) {
        *BriefingIndexAt(map, nullptr, r) = r;
    }
}

// A briefing's name, exp and money; `assignChars` uses the C-string assignment (the "complete"
// briefing) instead of the range one.
void ReadBriefingHeader(Registry& reg, char* briefing, std::uintptr_t section, bool assignChars) {
    {
        mfc::CString name = reg.GetString(Text(section), Text(kName), Copy(game::EmptyText()));
        const char* text = name;
        if (assignChars) {
            StringAssign(briefing, nullptr, text);
        } else {
            StringAssignRange(briefing, nullptr, text, text + std::strlen(text));
        }
    }
    At<float>(briefing, kBriefingExp) = static_cast<float>(reg.GetFloat(Text(section), Text(kExp), 0));
    At<float>(briefing, kBriefingMoney) = static_cast<float>(reg.GetFloat(Text(section), Text(kMoney), 0));
}

// A string list read inline: emptied (erased over its range, or cleared), reserved for the list's
// size, then every non-empty string appended.
void ReadListInline(Registry& reg, std::uintptr_t section, int list, char* briefing, bool erase) {
    console::CStringArray strings;
    reg.GetStringArray(Text(section), Text(kListKeys[list]), &strings);
    char* v = List(briefing, list);
    if (erase) {
        ListErase(v, nullptr, At<void*>(v, 0), At<void*>(v, 4));
    } else {
        ListClear(v);
    }
    ListReserve(v, nullptr, strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (Length(strings[i]) != 0) ListPushBack(v, nullptr, &strings[i]);
    }
}

void ConstructBriefing(char* briefing) {
    StringInitEmpty(briefing);
    FirstListConstruct(List(briefing, 0), nullptr, &kAllocator);
    for (int i = 1; i < 6; ++i) ListConstruct(List(briefing, i), nullptr, &kAllocator);
    StringInitEmpty(briefing + kBriefingString5C);
}

// "briefing receive": the lists read inline (the first three erased over their range, the others
// cleared), added like the quest.
void ReadReceiveBriefing(Registry& reg, char* db, char* briefing) {
    At<std::uint32_t>(briefing, kBriefingUnknown68) = 0;
    ReadBriefingHeader(reg, briefing, kReceive, false);
    for (int i = 0; i < 6; ++i) ReadListInline(reg, kReceive, i, briefing, i < 3);
    AddReceiveBriefing(db, briefing);
}

// "briefing reject": the item lists read inline (cleared first), the others by 006AE740; added by
// 006B0600 (also unless the name is there).
void ReadRejectBriefing(Registry& reg, char* db, char* briefing) {
    ReadBriefingHeader(reg, briefing, kReject, false);
    ReadListInline(reg, kReject, 0, briefing, false);
    ReadListInline(reg, kReject, 1, briefing, false);
    for (int i = 2; i < 6; ++i) ReadList(&reg, Text(kReject), Text(kListKeys[i]), List(briefing, i));
    BriefingAdd(db + kBriefings, nullptr, briefing);
}

// "briefing complete": the name assigned as a C string, all six lists by 006AE740.
void ReadCompleteBriefing(Registry& reg, char* db, char* briefing) {
    ReadBriefingHeader(reg, briefing, kComplete, true);
    for (int i = 0; i < 6; ++i) ReadList(&reg, Text(kComplete), Text(kListKeys[i]), List(briefing, i));
    BriefingAdd(db + kBriefings, nullptr, briefing);
}

} // namespace

// 006AE8E0 (thiscall on the database, RET 4). `image` is the quest's registry file in memory.
// Returns false when it is not a registry, true otherwise.
bool __fastcall QuestDatabaseLoad(char* db, void*, const void* image) {
    Registry reg;
    reg.Construct();
    if (!reg.Attach(image)) {
        reg.Destroy();
        return false;
    }

    alignas(4) char quest[kQuestSize];
    StringInitEmpty(quest);
    PlacesConstruct(quest + kQuestPlaces, nullptr, &kAllocator);
    StringInitEmpty(quest + kQuestString2C);
    {
        console::CStringArray places;
        ReadQuest(reg, quest, places);
        AddQuest(db, quest);

        alignas(4) char briefing[kBriefingSize];
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
    ListErase(list, nullptr, list->begin, list->end);
    ListReserve(list, nullptr, strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (Length(strings[i]) != 0) ListPushBack(list, nullptr, &strings[i]);
    }
}
EI_REPLACE(0x006AE740, ReadStringList);
