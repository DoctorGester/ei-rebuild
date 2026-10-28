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
#include "lib/str.h"
#include "text_split.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

// The quest registry (registry.cpp).
using Registry = CFileRegistry;

using PlaceElement = stlp::PlainElement<float>;
using ListElement = stlp::ObjectElement<std::string>;

std::vector<std::string>* List(QuestBriefingRecord* briefing, int i) { return &briefing->lists[i]; }

// push_back on the quest places as inlined: the value stored at the end, or the insert-overflow
// path when the vector is full.
void PushPlace(QuestRecord* quest, float value) { quest->places.push_back(value); }

// The "quest" section into the quest record.
void ReadQuest(Registry& reg, QuestRecord* quest, std::vector<std::string>& places) {
    quest->field2 = 0;
    {
        std::string name = reg.GetString(game::questwords::Quest(), game::questwords::Name(), std::string(game::EmptyText()));
        const char* text = name.c_str();
        quest->name.assign(text);
    }
    quest->exp = static_cast<float>(reg.GetFloat(game::questwords::Quest(), game::questwords::Exp(), 0));
    quest->money = static_cast<float>(reg.GetFloat(game::questwords::Quest(), game::questwords::Money(), 0));
    quest->zone = reg.GetInt(game::questwords::Quest(), game::questwords::ZoneIndex(), 0);
    quest->order = reg.GetInt(game::questwords::Quest(), game::questwords::Order(), 0);
    reg.GetStringArray(game::questwords::Quest(), game::questwords::QuestPlaces(), &places);
    // Each place "{x;y}": the braces trimmed, split at ';'; with two parts x and y are appended.
    for (std::string& place : places) {
        std::vector<std::string> parts;
        str::TrimLeft(place, '{');
        str::TrimRight(place, '}');
        text::Split(place.c_str(), &parts, ';');
        if (parts.size() == 2) {
            float x = static_cast<float>(std::atof(parts[0].c_str()));
            PushPlace(quest, x);
            float y = static_cast<float>(std::atof(parts[1].c_str()));
            PushPlace(quest, y);
        }
    }
}

// Adds the quest unless its name is indexed with a record already; the index is rebuilt over the
// whole vector after the push_back (it may have moved the records).
void AddQuest(QuestDatabase* db, const QuestRecord* quest) {
    QuestDatabase::QuestIndex& map = db->questsByName;
    auto found = map.find(quest->name);
    if (found != map.end() && found->second != nullptr) return;
    db->quests.push_back(*quest);
    map.clear();
    for (QuestRecord& r : db->quests) map[r.name] = &r;
}

void AddReceiveBriefing(QuestDatabase* db, const QuestBriefingRecord* briefing) {
    QuestBriefingTable* briefings = &db->briefings;
    QuestBriefingTable::Index& map = briefings->index;
    auto found = map.find(briefing->name);
    if (found != map.end() && found->second != nullptr) return;
    briefings->records.push_back(*briefing);
    map.clear();
    for (QuestBriefingRecord& r : briefings->records) map[r.name] = &r;
}

// A briefing's name, exp and money (the "complete" briefing's name is assigned with the
// C-string assignment, the others' with the range one: the same result).
void ReadBriefingHeader(Registry& reg, QuestBriefingRecord* briefing, const char* section) {
    {
        std::string name = reg.GetString(section, game::questwords::Name(), std::string(game::EmptyText()));
        briefing->name.assign(name.c_str());
    }
    briefing->exp = static_cast<float>(reg.GetFloat(section, game::questwords::Exp(), 0));
    briefing->money = static_cast<float>(reg.GetFloat(section, game::questwords::Money(), 0));
}

// A string list read inline: emptied (erased over its range, or cleared), reserved for the list's
// size, then every non-empty string appended.
void ReadListInline(Registry& reg, const char* section, int list, QuestBriefingRecord* briefing) {
    std::vector<std::string> strings;
    reg.GetStringArray(section, game::questwords::ListKey(list), &strings);
    std::vector<std::string>* v = List(briefing, list);
    v->clear();   // erase(begin, end) or clear(): the same
    v->reserve(strings.size());
    for (const std::string& listed : strings) {
        if (!listed.empty()) v->push_back(listed);
    }
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

    QuestRecord quest{};
    {
        std::vector<std::string> places;
        ReadQuest(reg, &quest, places);
        AddQuest(db, &quest);

        QuestBriefingRecord briefing{};
        ReadReceiveBriefing(reg, db, &briefing);
        ReadRejectBriefing(reg, db, &briefing);
        ReadCompleteBriefing(reg, db, &briefing);
    }
    reg.Destroy();
    return true;
}

// The game inlines the vector's erase, reserve and push_back; the same template members are called here.
void ReadStringList(CFileRegistry* registry, const char* section, const char* key, std::vector<std::string>* list) {
    std::vector<std::string> strings;
    registry->GetStringArray(section, key, &strings);
    list->clear();
    list->reserve(strings.size());
    for (const std::string& listed : strings) {
        if (!listed.empty()) list->push_back(listed);
    }
}

// --- QuestBriefingTable::Add: a briefing added unless its name is there ---------------------------

bool QuestBriefingTable::Add(const QuestBriefingRecord* record) {
    if (!record) return false;
    auto node = index.find(record->name);
    if (node != index.end() && node->second) return false;
    records.push_back(*record);
    index.clear();
    for (QuestBriefingRecord& r : records) index[r.name] = &r;
    return true;
}
