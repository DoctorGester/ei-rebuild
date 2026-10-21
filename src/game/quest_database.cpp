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
#include "record_vectors.h"
#include "text_split.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace {


// The records are built in raw stack buffers (their constructors are the original's own calls).
constexpr std::size_t kQuestSize = sizeof(QuestRecord);
constexpr std::size_t kBriefingSize = sizeof(QuestBriefingRecord);

// The section and key names, by their original addresses.
// The quest registry (registry.cpp).
using Registry = CFileRegistry;

// The quest places: vector<float> (constructor 006AFB00, insert-overflow 006ADD50).
using PlaceElement = stlp::PlainElement<float>;
// The briefing lists: vector<CString> (constructors 0053F4F0 for the first, 0055A3E0 for the
// others; erase a range 006AFB60, clear 0052E010, reserve 006B02B0, push_back from a CString
// 006AF790).
using ListElement = stlp::ObjectElement<mfc::CString>;
// The quest and briefing vectors' push_back (006B0710 / 006B0780).
using QuestElement = recordvec::QuestVector::Element;
using BriefingElement = recordvec::BriefingVector::Element;
// The record destructors [quest 006AF4D0, briefing 006AF5A0].
void __fastcall QuestDestroy(void* record) { static_cast<QuestRecord*>(record)->Destruct(); }
void __fastcall BriefingDestroy(void* record) { static_cast<QuestBriefingRecord*>(record)->Destruct(); }
// The quest index: find by the record's name (006B0870), clear (006B0520), operator[] (0064EE90).
// The briefing index: find 006B07C0 / 006B0930, clear 006B0590, operator[] 004297A0; the
// add-unless-present used for the later briefings 006B0600.

CStringVector* List(QuestBriefingRecord* briefing, int i) { return &briefing->lists[i]; }
int Length(const mfc::CString& s) { return reinterpret_cast<const int*>(static_cast<const char*>(s))[-2]; }

// push_back on the quest places as inlined: the value stored at the end, or the insert-overflow
// path when the vector is full.
void PushPlace(QuestRecord* quest, float value) { quest->places.PushBack<PlaceElement>(value); }

// The "quest" section into the quest record.
void ReadQuest(Registry& reg, QuestRecord* quest, console::CStringArray& places) {
    quest->field2 = 0;
    {
        mfc::CString name = reg.GetString(game::questwords::Quest(), game::questwords::Name(), mfc::CString(game::EmptyText()));
        const char* text = name;
        quest->name.Assign(text);
    }
    quest->exp = static_cast<float>(reg.GetFloat(game::questwords::Quest(), game::questwords::Exp(), 0));
    quest->money = static_cast<float>(reg.GetFloat(game::questwords::Quest(), game::questwords::Money(), 0));
    quest->zone = reg.GetInt(game::questwords::Quest(), game::questwords::ZoneIndex(), 0);
    quest->order = reg.GetInt(game::questwords::Quest(), game::questwords::Order(), 0);
    reg.GetStringArray(game::questwords::Quest(), game::questwords::QuestPlaces(), &places);
    // Each place "{x;y}": the braces trimmed, split at ';'; with two parts x and y are appended.
    for (int i = 0; i < places.GetSize(); ++i) {
        console::CStringArray parts;
        places[i].TrimLeft('{');
        places[i].TrimRight('}');
        text::Split(places[i], &parts, ';');
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
    QuestDatabase::QuestIndex& map = db->questsByName;
    const QuestDatabase::QuestIndex::Node* found = map.Find(quest->name);
    if (found && found->value != nullptr) return;
    db->quests.PushBack<QuestElement>(*quest);
    map.Clear();
    for (QuestRecord* r = db->quests.begin(); r != db->quests.end(); ++r) map.Subscript(r->name) = r;
}

void AddReceiveBriefing(QuestDatabase* db, const QuestBriefingRecord* briefing) {
    QuestBriefingTable* briefings = &db->briefings;
    QuestBriefingTable::Index& map = briefings->index;
    const QuestBriefingTable::Index::Node* found = map.Find(briefing->name);
    if (found && found->value != nullptr) return;
    briefings->records.PushBack<BriefingElement>(*briefing);
    map.Clear();
    for (QuestBriefingRecord* r = briefings->records.begin(); r != briefings->records.end(); ++r) map.Subscript(r->name) = r;
}

// A briefing's name, exp and money (the original assigns the "complete" briefing's name with the
// C-string assignment, the others' with the range one: the same result).
void ReadBriefingHeader(Registry& reg, QuestBriefingRecord* briefing, const char* section) {
    {
        mfc::CString name = reg.GetString(section, game::questwords::Name(), mfc::CString(game::EmptyText()));
        briefing->name.Assign(static_cast<const char*>(name));
    }
    briefing->exp = static_cast<float>(reg.GetFloat(section, game::questwords::Exp(), 0));
    briefing->money = static_cast<float>(reg.GetFloat(section, game::questwords::Money(), 0));
}

// A string list read inline: emptied (erased over its range, or cleared), reserved for the list's
// size, then every non-empty string appended.
void ReadListInline(Registry& reg, const char* section, int list, QuestBriefingRecord* briefing) {
    console::CStringArray strings;
    reg.GetStringArray(section, game::questwords::ListKey(list), &strings);
    CStringVector* v = List(briefing, list);
    v->EraseRange<ListElement>(v->begin(), v->end());   // erase(begin, end) or clear(): the same
    v->Reserve<ListElement>(strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (Length(strings[i]) != 0) v->PushBack<ListElement>(strings[i]);
    }
}

void ConstructBriefing(QuestBriefingRecord* briefing) {
    stlp::StringElement::Default(&briefing->name);
    for (int i = 0; i < 6; ++i) List(briefing, i)->ConstructEmpty();
    stlp::StringElement::Default(&briefing->text);
}

// "briefing receive": the lists read inline (the first three erased over their range, the others
// cleared), added like the quest.
void ReadReceiveBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    briefing->field10 = 0;
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingReceive());
    for (int i = 0; i < 6; ++i) ReadListInline(reg, game::questwords::BriefingReceive(), i, briefing);
    AddReceiveBriefing(db, briefing);
}

// "briefing reject": the item lists read inline (cleared first), the others by 006AE740; added by
// 006B0600 (also unless the name is there).
void ReadRejectBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingReject());
    ReadListInline(reg, game::questwords::BriefingReject(), 0, briefing);
    ReadListInline(reg, game::questwords::BriefingReject(), 1, briefing);
    for (int i = 2; i < 6; ++i) ReadStringList(&reg, game::questwords::BriefingReject(), game::questwords::ListKey(i), List(briefing, i));
    static_cast<QuestBriefingTable*>(&db->briefings)->Add(briefing);
}

// "briefing complete": the name assigned as a C string, all six lists by 006AE740.
void ReadCompleteBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingComplete());
    for (int i = 0; i < 6; ++i) ReadStringList(&reg, game::questwords::BriefingComplete(), game::questwords::ListKey(i), List(briefing, i));
    static_cast<QuestBriefingTable*>(&db->briefings)->Add(briefing);
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
    stlp::StringElement::Default(&quest->name);
    quest->places.ConstructEmpty();
    stlp::StringElement::Default(&quest->text);
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
EI_REWRITTEN(0x006AE8E0, QuestDatabaseLoad);

// 006AE740 (A-H19). The original inlines the vector's erase, reserve and push_back; the same
// template members are called here.
void __fastcall ReadStringList(CFileRegistry* registry, const char* section, const char* key, CStringVector* list) {
    console::CStringArray strings;
    registry->GetStringArray(section, key, &strings);
    list->EraseRange<ListElement>(list->begin(), list->end());
    list->Reserve<ListElement>(strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (Length(strings[i]) != 0) list->PushBack<ListElement>(strings[i]);
    }
}
EI_REWRITTEN(0x006AE740, ReadStringList);

// --- 006B0600 (A-H25): a briefing added unless its name is there ---------------------------------------

bool QuestBriefingTable::Add(const QuestBriefingRecord* record) {
    if (!record) return false;
    const Index::Node* node = index.Find(record->name);
    if (node && node->value) return false;
    records.PushBack<BriefingElement>(*record);
    index.Clear();
    for (QuestBriefingRecord* r = records.begin(); r != records.end(); ++r) index.Subscript(r->name) = r;
    return true;
}
EI_REWRITTEN(0x006B0600, QuestBriefingTable::Add);
