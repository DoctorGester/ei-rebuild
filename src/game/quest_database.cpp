// The quest database's load of one quest (called by the quest registration with the image of "<name>.quest.reg"):
// the "quest" section becomes a quest record (name, exp, money, zone index, order, the "{x;y}" quest places) and
// the three briefing sections ("briefing receive", "briefing reject", "briefing complete") briefing records (name,
// exp, money, six string lists); each record is added unless its name is already there. See
// docs/notes/items_spells.md ("Quest database").
#include "console.h"

#include "database_file.h"
#include "game_strings.h"
#include "quest_records.h"
#include "registry.h"
#include "script.h"
#include "text_split.h"
#include "lib/crt.h"
#include "lib/str.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>
#include <string>
#include <vector>

namespace {

// The "quest" section into the quest record.
void ReadQuest(FileRegistry& reg, QuestRecord* quest, std::vector<std::string>& places) {
    quest->unreadField = 0;
    quest->name = reg.GetString(game::questwords::Quest(), game::questwords::Name(), std::string(game::kEmptyText));
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
        parts = text::Split(place, ';');
        if (parts.size() == 2) {
            quest->places.push_back(str::ToFloat(parts[0]));
            quest->places.push_back(str::ToFloat(parts[1]));
        }
    }
}

// A briefing's name, exp, money and six string lists from `section`.
void ReadBriefing(FileRegistry& reg, QuestBriefingRecord* briefing, const char* section) {
    briefing->name = reg.GetString(section, game::questwords::Name(), std::string(game::kEmptyText));
    briefing->exp = static_cast<float>(reg.GetFloat(section, game::questwords::Exp(), 0));
    briefing->money = static_cast<float>(reg.GetFloat(section, game::questwords::Money(), 0));
    for (int i = 0; i < 6; ++i) ReadStringList(&reg, section, game::questwords::ListKey(i), &briefing->lists[i]);
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
    for (const char* section : {game::questwords::BriefingReceive(), game::questwords::BriefingReject(),
                                game::questwords::BriefingComplete()}) {
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

// The quest database's load from database.res, once at startup (called from WinMain). Rebirth patches the
// load (it redirects the file lookup to a mod folder's copy): this is the vanilla function and installs in
// the vanilla launch only (rule 9). As unit_db.cpp; see docs/notes/items_spells.md.

namespace {

using dbfile::LoadTable;

// The chunks of quests.qdb.
constexpr int kQuestsChunk = 1;
constexpr int kBriefingsChunk = 2;

} // namespace

int QuestDatabase::Load(const std::string& folder) {
    std::string base = folder;
    MappedResource resource;
    if (!dbfile::OpenDatabase(resource, base)) return dbfile::ReportNoDatabase(base);
    std::uint32_t size;
    void* data = resource.GetFile("quests.qdb", &size);
    if (!data) return game::FatalError("Can't open quests database\n");

    MemoryStream stream;
    stream.Fill(data, size);

    LoadTable(&stream, kQuestsChunk, &ChunkArchive::SerializeQuests, &quests, [this] {
        for (QuestRecord& record : quests) questsByName[record.name] = &record;
    });
    LoadTable(&stream, kBriefingsChunk, &ChunkArchive::SerializeBriefings, &briefings.records, [this] {
        for (QuestBriefingRecord& record : briefings.records) briefings.index[record.name] = &record;
    });

    resource.CloseFile(data);
    resource.CloseResource();
    return 1;
}

// operator[]: an unknown name is filed with null.
QuestRecord* QuestDatabase::FindQuest(std::string_view name) { return questsByName[std::string(name)]; }
