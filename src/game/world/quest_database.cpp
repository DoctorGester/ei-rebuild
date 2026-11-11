// The quest database: its load of one quest (called by the quest registration with the image of "<name>.quest.reg"):
// the "quest" section becomes a quest record (name, exp, money, zone index, order, the "{x;y}" quest places) and
// the three briefing sections ("briefing receive", "briefing reject", "briefing complete") briefing records (name,
// exp, money, six string lists); each record is added unless its name is already there. See
// docs/notes/items_spells.md ("Quest database").
#include "game/world/quest_records.h"

#include "game/console/console.h"
#include "engine/core/text_split.h"
#include "engine/data/chunk_archive.h"
#include "engine/data/database_file.h"
#include "engine/data/database_toml.h"
#include "engine/data/resource.h"
#include "engine/data/registry.h"
#include "game/script/script.h"

#include "lib/crt.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iterator>
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
    quest->unreadZone = reg.GetInt("quest", "zone index", 0);
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

bool QuestDatabase::LoadRegistry(const void* image, std::size_t size) {
    FileRegistry reg;
    if (!reg.Attach(image, size)) return false;

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
    MappedResource resource;
    dbfile::OpenDatabase(resource, folder);
    const RecordFile file = resource.FindRecord("quests.qdb");
    if (!file.Any()) game::FatalExit("Can't open quests database\n");
    resource.CloseResource();
    Read(file);
    return 1;
}

void QuestDatabase::Read(const RecordFile& file) {
    if (file.found) {
        MemoryStream stream;
        stream.Fill(file.original.data(), static_cast<std::uint32_t>(file.original.size()));
        LoadTable(&stream, [this](ChunkArchive& archive) { archive.SerializeQuests(kQuestsChunk, &quests); });
        LoadTable(&stream, [this](ChunkArchive& archive) {
            archive.SerializeBriefings(kBriefingsChunk, &briefings.records);
        });
    }
    MergeTomls(file, [this](const toml::Value& root, const std::string& path) { MergeToml(root, path); });
    for (QuestRecord& record : quests) questsByName[record.name] = &record;
    for (QuestBriefingRecord& record : briefings.records) briefings.index[record.name] = &record;
}

// --- As TOML ---------------------------------------------------------------------------------------

using Lists = QuestBriefingRecord::ListKind;

namespace {

// A quest's places on the objectives map (ObjectivesWindow::LoadQuestPlaces: x and y in turn) as a list of [x, y]
// points. A last odd value is not kept (no file has one).
struct Places {
    std::vector<float>& values;
};

std::string ValueText(Places field) {
    std::vector<std::string> points;
    for (std::size_t i = 1; i < field.values.size(); i += 2)
        points.push_back(toml::Array({toml::Float(field.values[i - 1]), toml::Float(field.values[i])}));
    return toml::Array(points);
}
bool ReadValue(const toml::Value& value, Places field) {
    if (!value.IsArray()) return false;
    std::vector<float> read;
    for (int i = 0; i < value.Size(); ++i) {
        float point[2] = {};
        if (value.At(i).Size() != 2 || !dbtoml::ReadValue(value.At(i), point)) return false;
        read.insert(read.end(), std::begin(point), std::end(point));
    }
    field.values = std::move(read);
    return true;
}

} // namespace

template <class Visit>
void Fields(QuestRecord& r, Visit& v) {
    v("name", r.name);
    v("text", r.text);
    v("exp", r.exp);
    v("money", r.money);
    v.Unread("unread_zone", r.unreadZone);
    v("order", r.order);
    v("places", Places{r.places});
    v.Unread("unread", r.unreadField);
}

template <class Visit>
void Fields(QuestBriefingRecord& r, Visit& v) {
    v("name", r.name);
    v("text", r.text);
    v("exp", r.exp);
    v("money", r.money);
    v("give_items", r.lists[Lists::GiveItems]);
    v("remove_items", r.lists[Lists::RemoveItems]);
    v("give_quests", r.lists[Lists::GiveQuests]);
    v("complete_quests", r.lists[Lists::CompleteQuests]);
    v("available_zones", r.lists[Lists::AvailableZones]);
    v("complete_zones", r.lists[Lists::CompleteZones]);
    v("specials", r.specials);
}

namespace {

constexpr const char* kQuests = "quests.qdb";
const auto kNameOf = [](const auto& record) { return record.name; };

} // namespace

void QuestDatabase::MergeToml(const toml::Value& root, const std::string& path) {
    dbtoml::MergeRecords(root["quest"], quests, "name", kNameOf, kQuests, path);
    dbtoml::MergeRecords(root["briefing"], briefings.records, "name", kNameOf, kQuests, path);
    dbtoml::CheckTables(root, {"quest", "briefing"}, kQuests, path);
}

std::string QuestDatabase::ToToml() const {
    toml::Writer writer;
    dbtoml::WriteRecords(writer, "quest", quests, "name");
    dbtoml::WriteRecords(writer, "briefing", briefings.records, "name");
    return writer.text;
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
    ar->Field(3, &unreadZone);
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
