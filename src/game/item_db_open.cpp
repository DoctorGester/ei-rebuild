// The spell database's and the item database's loads from their archive at startup and at every switch of the
// resource database. As unit_db_load.cpp; see docs/notes/items_spells.md.
#include "chunk_archive.h"
#include "item_db.h"

#include "database_file.h"
#include "lib/crt.h"

#include <cstdint>
#include <string>


namespace {

using dbfile::FillStream;
using dbfile::LoadTable;
using dbfile::LocalResource;

// The chunks of spells.sdb and items.idb.
constexpr int kSpellsChunk = 1;
constexpr int kModifiersChunk = 2;
constexpr int kFirstListChunk = 3;
constexpr int kMaterialsChunk = 1;
constexpr int kWeaponsChunk = 2;
constexpr int kArmorsChunk = 3;
constexpr int kQuickItemsChunk = 4;
constexpr int kQuestItemsChunk = 5;
constexpr int kLootChunk = 6;

// Every record of `records` (`stride` bytes each) filed in `index` under the text `key` gives for
// it (the index's operator[]: a record of a text seen before replaces the earlier one).
template <typename Record, typename Key>
void FileRecords(itemdb::NamedRecordTable<Record>& table, Key key) {
    for (Record& record : table.records) {
        std::string text(key(&record));
        table.names[text] = &record;
    }
}

// A record's code as text: a spell's string, a modifier's four letters.
const char* CodeText(const std::string& code) { return code.c_str(); }
const char* CodeText(const char* code) { return code; }

// The same for the spell tables: every record filed under its code.
template <typename Record>
void FileByCode(itemdb::CodeIndexedTable<Record>& table) {
    for (Record& record : table.records) {
        std::string text(CodeText(record.code));
        table.byCode[text] = &record;
    }
}

const char* MaterialKey(const itemdb::MaterialRecordHead* record) { return record->key.c_str(); }
const char* ItemName(const itemdb::ItemRecordHead* record) { return record->name.c_str(); }

} // namespace

namespace itemdb {

// --- The spell database ---------------------------------------------------------------------------

int SpellDatabase::Load(const std::string& folder, const std::string& archive) {
    std::string path = folder + archive;
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", path);
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("spells.sdb", &size);
    if (!data) return game::FatalError("Can't open spells database\n");

    CMemoryStream local;
    CMemoryStream* stream = &local;
    FillStream(*stream, data, size);

    LoadTable(stream, kSpellsChunk, &ChunkArchive::SerializeSpells, &spells.records,
              [this] { FileByCode(spells); });
    LoadTable(stream, kModifiersChunk, &ChunkArchive::SerializeSpellModifiers, &modifiers.records,
              [this] { FileByCode(modifiers); });

    // The three lists through one archive, built and destroyed in place.
    ChunkArchive ar;
    ar.Open(stream, 1, 1);
    ar.SerializeSpellLists(kFirstListChunk, &scrollRecipes);
    ar.SerializeSpellLists(kFirstListChunk + 1, &armorRecipes);
    ar.SerializeSpellLists(kFirstListChunk + 2, &weaponRecipes);
    ar.Close(stream);
    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}

// --- The item database ----------------------------------------------------------------------------

int ItemDatabase::Load(const std::string& folder, const std::string& archive) {
    std::string path = folder + archive;
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", path);
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("items.idb", &size);
    if (!data) return game::FatalError("Can't open items database\n");

    CMemoryStream local;
    CMemoryStream* stream = &local;
    FillStream(*stream, data, size);

    LoadTable(stream, kMaterialsChunk, &ChunkArchive::SerializeMaterials, &materials.records,
              [this] { FileRecords(materials, MaterialKey); });
    LoadTable(stream, kWeaponsChunk, &ChunkArchive::SerializeWeapons, &weapons.records,
              [this] { FileRecords(weapons, ItemName); });
    LoadTable(stream, kArmorsChunk, &ChunkArchive::SerializeArmors, &armors.records,
              [this] { FileRecords(armors, ItemName); });
    quickItems.Load(stream, kQuickItemsChunk);
    questItems.Load(stream, kQuestItemsChunk);
    loot.Load(stream, kLootChunk);

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}

} // namespace itemdb
