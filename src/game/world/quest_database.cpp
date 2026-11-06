// The quest database: its load of one quest (called by the quest registration with the image of "<name>.quest.reg"):
// the "quest" section becomes a quest record (name, exp, money, zone index, order, the "{x;y}" quest places) and
// the three briefing sections ("briefing receive", "briefing reject", "briefing complete") briefing records (name,
// exp, money, six string lists); each record is added unless its name is already there. See
// docs/notes/items_spells.md ("Quest database").
#include "game/world/quest_records.h"

#include "game/console/console.h"
#include "game/core/text_split.h"
#include "game/data/chunk_archive.h"
#include "game/data/database_file.h"
#include "game/data/registry.h"
#include "game/script/script.h"

#include "lib/crt.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

// The database of the quests: built empty by the C++ runtime, loaded at start.
QuestDatabase g_questDatabase;

namespace {

// The keys of a briefing's six string lists (QuestBriefingRecord::lists, in order).
constexpr const char* kBriefingListKeys[6] = {"give items",      "remove items",    "give quests",
                                              "complete quests", "available zones", "complete zones"};

// The "quest" section into the quest record.
void ReadQuest(FileRegistry& reg, QuestRecord* quest, std::vector<std::string>& places) {
    quest->unreadField = 0;
    quest->name = reg.GetString("quest", "name", std::string());
    quest->exp = static_cast<float>(reg.GetFloat("quest", "exp", 0));
    quest->money = static_cast<float>(reg.GetFloat("quest", "money", 0));
    quest->zone = reg.GetInt("quest", "zone index", 0);
    quest->order = reg.GetInt("quest", "order", 0);
    reg.GetStringArray("quest", "questplaces", &places);
    // Each place "{x;y}": the braces trimmed, split at ';'; with two parts x and y are appended.
    for (std::string& place : places) {
        std::vector<std::string> parts;
        str::TrimLeft(place, '{');
        str::TrimRight(place, '}');
        parts = text::Split(place, ';');
        if (parts.size() == 2) {
            quest->places.push_back(str::ToFloat(parts[0]));
            quest->places.push_back(str::ToFloat(parts[1]));
        }
    }
}

// A briefing's name, exp, money and six string lists from `section`.
void ReadBriefing(FileRegistry& reg, QuestBriefingRecord* briefing, const char* section) {
    briefing->name = reg.GetString(section, "name", std::string());
    briefing->exp = static_cast<float>(reg.GetFloat(section, "exp", 0));
    briefing->money = static_cast<float>(reg.GetFloat(section, "money", 0));
    for (int i = 0; i < 6; ++i) ReadStringList(&reg, section, kBriefingListKeys[i], &briefing->lists[i]);
}

} // namespace

bool QuestDatabase::LoadRegistry(const void* image) {
    FileRegistry reg;
    if (!reg.Attach(image)) return false;

    QuestRecord quest{};
    std::vector<std::string> places;
    ReadQuest(reg, &quest, places);
    AddQuest(quest);

    QuestBriefingRecord briefing{};
    for (const char* section : {"briefing receive", "briefing reject",
                                "briefing complete"}) {
        ReadBriefing(reg, &briefing, section);
        briefings.Add(&briefing);
    }
    return true;
}

// Adds the quest unless its name is indexed with a record already; the index is rebuilt over the whole vector after
// the push_back (it may have moved the records).
void QuestDatabase::AddQuest(const QuestRecord& quest) {
    auto found = questsByName.find(quest.name);
    if (found != questsByName.end() && found->second != nullptr) return;
    quests.push_back(quest);
    questsByName.clear();
    for (QuestRecord& r : quests) questsByName[r.name] = &r;
}

void ReadStringList(FileRegistry* registry, const char* section, const char* key, std::vector<std::string>* list) {
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

// --- Quest db open -----------------------------------------------------------------------------------

// The quest database's load from database.res, once at startup (called from WinMain). As unit_db.cpp; see
// docs/notes/items_spells.md.

namespace {

using dbfile::LoadTable;

// The chunks of quests.qdb.
constexpr int kQuestsChunk = 1;
constexpr int kBriefingsChunk = 2;

} // namespace

int QuestDatabase::Load(const std::string& folder) {
    std::string base = folder;
    MappedResource resource;
    dbfile::OpenDatabase(resource, base);
    std::uint32_t size;
    void* data = resource.GetFile("quests.qdb", &size);
    if (!data) game::FatalExit("Can't open quests database\n");

    MemoryStream stream;
    stream.Fill(data, size);

    LoadTable(&stream, [this](ChunkArchive& archive) { archive.SerializeQuests(kQuestsChunk, &quests); });
    for (QuestRecord& record : quests) questsByName[record.name] = &record;
    LoadTable(&stream, [this](ChunkArchive& archive) {
        archive.SerializeBriefings(kBriefingsChunk, &briefings.records);
    });
    for (QuestBriefingRecord& record : briefings.records) briefings.index[record.name] = &record;

    resource.CloseFile(data);
    resource.CloseResource();
    return 1;
}

// operator[]: an unknown name is filed with null.
QuestRecord* QuestDatabase::FindQuest(std::string_view name) { return questsByName[std::string(name)]; }

// --- The records in an archive -----------------------------------------------------------------------

// The quest database's records in an archive: the records' Serialize (quests, briefings) and the two vectors'. See
// quest_records.h and docs/notes/items_spells.md ("Quest database").
void QuestRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &exp);
    ar->Field(2, &unreadField);
    ar->Field(3, &zone);
    ar->StringField(4, &text);
    ar->Field(5, &money);
    ar->Field(6, &order);
    if (!ar->EnterChunk(7)) return;
    int count;
    if (ar->loading) {
        // erase(begin, end), then resize(count, 0.0f): the vector is empty, so a fill insert.
        places.clear();
        count = ar->CountChunks(1);
        places.insert(places.end(), static_cast<unsigned>(count), 0.0f);
    } else {
        count = static_cast<int>(places.size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->Field(1, &places[i]);
    }
    ar->LeaveChunk();
}

void QuestBriefingRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &exp);
    ar->Field(2, &money);
    ar->SerializeStrings(3, &lists[0]);
    ar->StringField(4, &text);
    for (int i = 1; i < 6; ++i) InlineStrings(ar, 4 + i, &lists[i]);
    ar->Field(10, &specials);
}

void ChunkArchive::SerializeQuests(int id, std::vector<QuestRecord>* quests) {
    RecordChunks(this, id, quests, &QuestRecord::Serialize);
}

void ChunkArchive::SerializeBriefings(int id, std::vector<QuestBriefingRecord>* briefings) {
    RecordChunks(this, id, briefings, &QuestBriefingRecord::Serialize);
}
