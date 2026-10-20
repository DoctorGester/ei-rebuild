// The spell database's and the item database's loads from their archive at startup and at every
// switch of the resource database (00684550 / 006A67A0; 0057CB50). A-H26.
// Rebirth patches both loads (it redirects their file lookup to a mod folder's copy): these are the
// vanilla functions and install in the vanilla launch only (rule 9); callers go through
// SpellTablesLoadOriginal / ItemDatabaseLoadOriginal. As unit_db_load.cpp; see
// docs/notes/items_spells.md ("A-H26").
#include "chunk_archive.h"
#include "item_db.h"

#include "database_file.h"
#include "lib/crt.h"
#include "originals_engine.h"   // game::FatalError

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

namespace {

using dbfile::FillStream;
using dbfile::LoadTable;
using dbfile::LocalResource;
using dbfile::LocalStream;

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
template <typename Index, typename Key>
void FileRecords(const RecordTable& records, int stride, Index& index, Key key) {
    for (char* record = records.begin; record != records.end; record += stride) {
        stlp::string text(key(record));
        index[text] = record;
    }
}

const char* SpellCode(const char* record) { return reinterpret_cast<const itemdb::SpellRecord*>(record)->code; }
const char* ModifierCode(const char* record) {
    return reinterpret_cast<const itemdb::SpellModifierRecord*>(record)->code;
}
const char* MaterialKey(const char* record) { return reinterpret_cast<const itemdb::MaterialRecordHead*>(record)->key; }
const char* ItemName(const char* record) { return reinterpret_cast<const itemdb::ItemRecordHead*>(record)->text04; }

} // namespace

namespace itemdb {

// --- The spell database ---------------------------------------------------------------------------

int SpellDatabase::Load(const char* folder, const char* archive) {
    mfc::CString path = mfc::CString(folder) + archive;
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", static_cast<const char*>(path));
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFileImpl("spells.sdb", &size);
    if (!data) return game::FatalError("Can't open spells database\n");

    LocalStream local;
    CMemoryStream* stream = &local.stream;
    FillStream(*stream, data, size);
    // The serialisers' unused third argument, as the original passes it: the file's size.
    void* const unused = reinterpret_cast<void*>(static_cast<std::uintptr_t>(size));

    LoadTable(stream, kSpellsChunk, &ChunkArchive::SerializeSpells, &spells, unused,
              [this] { FileRecords(spells.records, kSpellSize, spells.byCode, SpellCode); });
    LoadTable(stream, kModifiersChunk, &ChunkArchive::SerializeSpellModifiers, &modifiers, unused,
              [this] { FileRecords(modifiers.records, kSpellModifierSize, modifiers.byCode, ModifierCode); });

    // The three lists through one archive, built and destroyed in place.
    ChunkArchive ar;
    ar.ConstructLocal();
    ar.Open(stream, 1, 1);
    for (int i = 0; i < 3; ++i) ar.SerializeSpellLists(kFirstListChunk + i, &lists[i], unused);
    ar.Close(stream);
    resource.Get()->CMappedResource::CloseFileImpl(data);
    resource.Get()->CMappedResource::CloseResource();
    ar.DestructLocal();
    return 1;
}
EI_REPLACE_METHOD(0x00684550, SpellDatabase::Load);

// --- The item database ----------------------------------------------------------------------------

int ItemDatabase::Load(const char* folder, const char* archive) {
    mfc::CString path = mfc::CString(folder) + archive;
    LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", static_cast<const char*>(path));
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFileImpl("items.idb", &size);
    if (!data) return game::FatalError("Can't open items database\n");

    LocalStream local;
    CMemoryStream* stream = &local.stream;
    FillStream(*stream, data, size);

    // The serialisers' unused third argument is the file's data here.
    LoadTable(stream, kMaterialsChunk, &ChunkArchive::SerializeMaterials, &materials, data,
              [this] { FileRecords(materials.records, kMaterialSize, materials.ByName(), MaterialKey); });
    LoadTable(stream, kWeaponsChunk, &ChunkArchive::SerializeWeapons, &weapons, data,
              [this] { FileRecords(weapons.records, kWeaponSize, weapons.ByName(), ItemName); });
    LoadTable(stream, kArmorsChunk, &ChunkArchive::SerializeArmors, &armors, data,
              [this] { FileRecords(armors.records, kArmorSize, armors.ByName(), ItemName); });
    quickItems.LoadQuickItems(stream, kQuickItemsChunk);
    questItems.LoadQuestItems(stream, kQuestItemsChunk);
    loot.LoadLoot(stream, kLootChunk);

    resource.Get()->CMappedResource::CloseFileImpl(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}
EI_REPLACE_METHOD(0x006A67A0, ItemDatabase::Load);

} // namespace itemdb
