// ei-convert's game databases: the round trip of each (convert_databases.h).
#include "convert_databases.h"

#include "engine/core/core_streams.h"
#include "engine/data/chunk_archive.h"
#include "engine/data/resource.h"
#include "game/items/item_db.h"
#include "game/objects/lever.h"
#include "game/spells/spell_db.h"
#include "game/units/unit_db.h"
#include "game/world/quest_records.h"

#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {

// A record's bytes as its serialiser writes it (a chunk archive holding the record as chunk 1 of chunk 1).
template <class Record, class Serialize>
std::string Bytes(const Record& record, Serialize serialize) {
    std::vector<Record> one{record};
    MemoryStream stream;
    ChunkArchive archive;
    archive.Open(&stream, 0, 1);
    RecordChunks(&archive, 1, &one, serialize);
    archive.Close(&stream);
    std::string bytes(stream.Size(), '\0');
    stream.Seek(0);
    stream.Read(bytes.data(), static_cast<std::uint32_t>(bytes.size()));
    return bytes;
}

struct Comparison {
    std::vector<std::string>& lines;

    // The records of a table in order, each by its bytes.
    template <class Record, class Serialize>
    void Table(const std::vector<Record>& a, const std::vector<Record>& b, const std::string& table,
               Serialize serialize) {
        if (a.size() != b.size())
            lines.push_back(table + ": " + std::to_string(a.size()) + " records, read back " +
                            std::to_string(b.size()));
        for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
            if (Bytes(a[i], serialize) != Bytes(b[i], serialize))
                lines.push_back(table + " " + std::to_string(i) + " (" + a[i].name + ") differs");
    }

    // An index by name: the same names, each to the record of the same place.
    template <class Map, class Record>
    void Index(const Map& a, const std::vector<Record>& recordsA, const Map& b, const std::vector<Record>& recordsB,
               const std::string& table) {
        const auto places = [](const Map& index, const std::vector<Record>& records) {
            std::map<std::string, std::ptrdiff_t> places;
            for (const auto& [name, record] : index)
                places[name] = record ? static_cast<std::ptrdiff_t>(record - records.data()) : -1;
            return places;
        };
        if (places(a, recordsA) != places(b, recordsB)) lines.push_back(table + ": the index by name differs");
    }

    template <class Records>
    void RecordTable(const Records& a, const Records& b, const std::string& table) {
        using Record = typename decltype(a.records)::value_type;
        Table(a.records, b.records, table, &Record::Serialize);
        Index(a.names, a.records, b.names, b.records, table);
    }

    // The TOML of both (the first lines that differ: which field), when anything differed.
    void Tomls(const std::string& a, const std::string& b) {
        if (lines.empty()) return;
        std::istringstream x(a), y(b);
        std::string lineA, lineB;
        int line = 0;
        for (int shown = 0; shown < 4;) {
            const bool moreA = static_cast<bool>(std::getline(x, lineA));
            const bool moreB = static_cast<bool>(std::getline(y, lineB));
            ++line;
            if (!moreA && !moreB) break;
            if (moreA && moreB && lineA == lineB) continue;
            lines.push_back("TOML line " + std::to_string(line) + ": " + lineA + " | read back: " + lineB);
            ++shown;
        }
    }
};

void CompareItems(const itemdb::ItemDatabase& a, const itemdb::ItemDatabase& b, Comparison& c) {
    c.RecordTable(a.materials, b.materials, "material");
    c.RecordTable(a.weapons, b.weapons, "weapon");
    c.RecordTable(a.armors, b.armors, "armor");
    c.RecordTable(a.quickItems, b.quickItems, "quick_item");
    c.RecordTable(a.questItems, b.questItems, "quest_item");
    c.RecordTable(a.loot, b.loot, "loot");
}

void CompareSpells(const itemdb::SpellDatabase& a, const itemdb::SpellDatabase& b, Comparison& c) {
    c.RecordTable(a.spells, b.spells, "spell");
    c.RecordTable(a.modifiers, b.modifiers, "modifier");
    using Recipes = std::vector<itemdb::SpellListRecord>;
    const auto recipes = [&](const Recipes& x, const Recipes& y, const std::string& table) {
        if (x.size() != y.size()) c.lines.push_back(table + ": record count");
        for (std::size_t i = 0; i < x.size() && i < y.size(); ++i)
            if (Bytes(x[i], &itemdb::SpellListRecord::Serialize) != Bytes(y[i], &itemdb::SpellListRecord::Serialize))
                c.lines.push_back(table + " " + std::to_string(i) + " (" + x[i].spell + ") differs");
    };
    recipes(a.scrollRecipes, b.scrollRecipes, "scroll_recipe");
    recipes(a.armorRecipes, b.armorRecipes, "armor_recipe");
    recipes(a.weaponRecipes, b.weaponRecipes, "weapon_recipe");
}

void CompareUnits(const UnitDatabase& a, const UnitDatabase& b, Comparison& c) {
    c.Table(a.levels.records, b.levels.records, "level", &UnitLevelRecord::Serialize);
    c.Table(a.models.records, b.models.records, "model", &UnitModelRecord::Serialize);
    c.Table(a.prototypes.records, b.prototypes.records, "unit", &UnitPrototypeRecord::Serialize);
    c.Table(a.npcs.records, b.npcs.records, "npc", &UnitNpcRecord::Serialize);
    c.Index(a.levels.byName, a.levels.records, b.levels.byName, b.levels.records, "level");
    c.Index(a.models.byName, a.models.records, b.models.byName, b.models.records, "model");
    c.Index(a.prototypes.byName, a.prototypes.records, b.prototypes.byName, b.prototypes.records, "unit");
    c.Index(a.npcs.byName, a.npcs.records, b.npcs.byName, b.npcs.records, "npc");
}

void CompareSkills(const SkillDatabase& a, const SkillDatabase& b, Comparison& c) {
    c.Table(a.skills, b.skills, "skill", &SkillRecord::Serialize);
    c.Table(a.levels, b.levels, "perk", &SkillLevelRecord::Serialize);
}

void CompareQuests(const QuestDatabase& a, const QuestDatabase& b, Comparison& c) {
    c.Table(a.quests, b.quests, "quest", &QuestRecord::Serialize);
    c.Table(a.briefings.records, b.briefings.records, "briefing", &QuestBriefingRecord::Serialize);
    c.Index(a.questsByName, a.quests, b.questsByName, b.quests, "quest");
    c.Index(a.briefings.index, a.briefings.records, b.briefings.index, b.briefings.records, "briefing");
}

// The levers with the spots past each one's count cleared: the TOML leaves them out (no code uses them).
std::vector<LeverRecord> UsedSpots(std::vector<LeverRecord> records) {
    for (LeverRecord& record : records)
        for (std::uint32_t i = record.spotCount; i < std::size(record.spots); ++i)
            record.spots[i][0] = record.spots[i][1] = 0.0f;
    return records;
}

void CompareLevers(const LeverRecordTable& a, const LeverRecordTable& b, Comparison& c) {
    c.Table(UsedSpots(a.records), UsedSpots(b.records), "lever", &LeverRecord::Archive);
    c.Index(a.byName, a.records, b.byName, b.records, "lever");
}

template <class Database, class Compare>
void RoundTrip(const RecordFile& original, const std::string& name, std::string (*wholeFile)(const std::string&),
               std::string* toml, std::vector<std::string>& differences, Compare compare) {
    const auto a = std::make_unique<Database>();
    const auto b = std::make_unique<Database>();
    a->Read(original);
    *toml = wholeFile(a->ToToml());
    RecordFile read;
    read.tomls.push_back({name + ".toml", *toml});
    b->Read(read);
    Comparison comparison{differences};
    compare(*a, *b, comparison);
    comparison.Tomls(a->ToToml(), b->ToToml());
}

} // namespace

bool IsDatabase(std::string_view lowerName) {
    return lowerName == "items.idb" || lowerName == "spells.sdb" || lowerName == "units.udb" ||
           lowerName == "perks.pdb" || lowerName == "quests.qdb" || lowerName == "levers.ldb";
}

void ConvertDatabase(std::string_view lowerName, const RecordFile& original, const std::string& name,
                     std::string (*wholeFile)(const std::string&), std::string* toml,
                     std::vector<std::string>& differences) {
    if (lowerName == "items.idb")
        RoundTrip<itemdb::ItemDatabase>(original, name, wholeFile, toml, differences, CompareItems);
    else if (lowerName == "spells.sdb")
        RoundTrip<itemdb::SpellDatabase>(original, name, wholeFile, toml, differences, CompareSpells);
    else if (lowerName == "units.udb")
        RoundTrip<UnitDatabase>(original, name, wholeFile, toml, differences, CompareUnits);
    else if (lowerName == "perks.pdb")
        RoundTrip<SkillDatabase>(original, name, wholeFile, toml, differences, CompareSkills);
    else if (lowerName == "quests.qdb")
        RoundTrip<QuestDatabase>(original, name, wholeFile, toml, differences, CompareQuests);
    else if (lowerName == "levers.ldb")
        RoundTrip<LeverRecordTable>(original, name, wholeFile, toml, differences, CompareLevers);
}
