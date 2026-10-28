// The quest database's load of one quest (called by the quest registration with the image of "<name>.quest.reg"):
// the "quest" section becomes a quest record (name, exp, money, zone index, order, the "{x;y}" quest places) and
// the three briefing sections ("briefing receive", "briefing reject", "briefing complete") briefing records (name,
// exp, money, six string lists); each record is added unless its name is already there. See
// docs/notes/items_spells.md ("Quest database").
#include "console.h"
#include "quest_records.h"
#include "game_strings.h"
#include "registry.h"

#include "../lib/mfc.h"
#include "lib/static_object.h"
#include "record_vectors.h"
#include "text_split.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>

namespace {

// The quest registry (registry.cpp).
using Registry = CFileRegistry;

using PlaceElement = stlp::PlainElement<float>;
using ListElement = stlp::ObjectElement<std::string>;
using QuestElement = recordvec::QuestVector::Element;
using BriefingElement = recordvec::BriefingVector::Element;

stlp::vector<std::string>* List(QuestBriefingRecord* briefing, int i) { return &briefing->lists[i]; }

// push_back on the quest places as inlined: the value stored at the end, or the insert-overflow
// path when the vector is full.
void PushPlace(QuestRecord* quest, float value) { quest->places.PushBack<PlaceElement>(value); }

// The "quest" section into the quest record.
void ReadQuest(Registry& reg, QuestRecord* quest, console::CStringArray& places) {
    quest->field2 = 0;
    {
        std::string name = reg.GetString(game::questwords::Quest(), game::questwords::Name(), mfc::CString(game::EmptyText())).GetString();
        const char* text = name.c_str();
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
            float x = static_cast<float>(std::atof(parts[0]));
            PushPlace(quest, x);
            float y = static_cast<float>(std::atof(parts[1]));
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

// A briefing's name, exp and money (the "complete" briefing's name is assigned with the
// C-string assignment, the others' with the range one: the same result).
void ReadBriefingHeader(Registry& reg, QuestBriefingRecord* briefing, const char* section) {
    {
        std::string name = reg.GetString(section, game::questwords::Name(), mfc::CString(game::EmptyText())).GetString();
        briefing->name.Assign(name.c_str());
    }
    briefing->exp = static_cast<float>(reg.GetFloat(section, game::questwords::Exp(), 0));
    briefing->money = static_cast<float>(reg.GetFloat(section, game::questwords::Money(), 0));
}

// A string list read inline: emptied (erased over its range, or cleared), reserved for the list's
// size, then every non-empty string appended.
void ReadListInline(Registry& reg, const char* section, int list, QuestBriefingRecord* briefing) {
    console::CStringArray strings;
    reg.GetStringArray(section, game::questwords::ListKey(list), &strings);
    stlp::vector<std::string>* v = List(briefing, list);
    v->EraseRange<ListElement>(v->begin(), v->end());   // erase(begin, end) or clear(): the same
    v->Reserve<ListElement>(strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (strings[i].GetLength() != 0) v->PushBack<ListElement>(strings[i].GetString());
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
    briefing->specials = 0;
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingReceive());
    for (int i = 0; i < 6; ++i) ReadListInline(reg, game::questwords::BriefingReceive(), i, briefing);
    AddReceiveBriefing(db, briefing);
}

// "briefing reject": the item lists read inline (cleared first), the others by ReadStringList; added by
// QuestBriefingTable::Add (also unless the name is there).
void ReadRejectBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingReject());
    ReadListInline(reg, game::questwords::BriefingReject(), 0, briefing);
    ReadListInline(reg, game::questwords::BriefingReject(), 1, briefing);
    for (int i = 2; i < 6; ++i) ReadStringList(&reg, game::questwords::BriefingReject(), game::questwords::ListKey(i), List(briefing, i));
    db->briefings.Add(briefing);
}

// "briefing complete": the name assigned as a C string, all six lists by ReadStringList.
void ReadCompleteBriefing(Registry& reg, QuestDatabase* db, QuestBriefingRecord* briefing) {
    ReadBriefingHeader(reg, briefing, game::questwords::BriefingComplete());
    for (int i = 0; i < 6; ++i) ReadStringList(&reg, game::questwords::BriefingComplete(), game::questwords::ListKey(i), List(briefing, i));
    db->briefings.Add(briefing);
}

} // namespace

bool QuestDatabase::LoadRegistry(const void* image) {
    QuestDatabase* db = this;
    Registry reg;
    reg.Construct();
    if (!reg.Attach(image)) {
        reg.Destroy();
        return false;
    }

    lib::InPlace<QuestRecord> questStorage;   // built and destroyed below
    QuestRecord* quest = questStorage.Get();
    stlp::StringElement::Default(&quest->name);
    quest->places.ConstructEmpty();
    stlp::StringElement::Default(&quest->text);
    {
        console::CStringArray places;
        ReadQuest(reg, quest, places);
        AddQuest(db, quest);

        lib::InPlace<QuestBriefingRecord> briefingStorage;
        QuestBriefingRecord* briefing = briefingStorage.Get();
        ConstructBriefing(briefing);
        ReadReceiveBriefing(reg, db, briefing);
        ReadRejectBriefing(reg, db, briefing);
        ReadCompleteBriefing(reg, db, briefing);
        briefing->Destruct();
    }
    quest->Destruct();
    reg.Destroy();
    return true;
}

// The game inlines the vector's erase, reserve and push_back; the same template members are called here.
void __fastcall ReadStringList(CFileRegistry* registry, const char* section, const char* key, stlp::vector<std::string>* list) {
    console::CStringArray strings;
    registry->GetStringArray(section, key, &strings);
    list->EraseRange<ListElement>(list->begin(), list->end());
    list->Reserve<ListElement>(strings.GetSize());
    for (int i = 0; i < strings.GetSize(); ++i) {
        if (strings[i].GetLength() != 0) list->PushBack<ListElement>(strings[i].GetString());
    }
}

// --- QuestBriefingTable::Add: a briefing added unless its name is there ---------------------------

bool QuestBriefingTable::Add(const QuestBriefingRecord* record) {
    if (!record) return false;
    const Index::Node* node = index.Find(record->name);
    if (node && node->value) return false;
    records.PushBack<BriefingElement>(*record);
    index.Clear();
    for (QuestBriefingRecord* r = records.begin(); r != records.end(); ++r) index.Subscript(r->name) = r;
    return true;
}
