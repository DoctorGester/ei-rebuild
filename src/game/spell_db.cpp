// The spell database: its records' Serialize, the code-indexed tables and SpellDatabase::Load ("spells.sdb", at
// startup and at every switch of the resource database). See item_db.h and docs/notes/items_spells.md
// ("Databases and naming").
#include "item_db.h"

#include "chunk_archive.h"
#include "core_streams.h"
#include "database_file.h"
#include "unit_db.h"   // InlineStrings

#include "lib/crt.h"

#include <cstdint>
#include <string>
#include <vector>

namespace {

// The chunks of spells.sdb.
constexpr int kSpellsChunk = 1;
constexpr int kModifiersChunk = 2;
constexpr int kScrollRecipesChunk = 3;
constexpr int kArmorRecipesChunk = 4;
constexpr int kWeaponRecipesChunk = 5;

// An int list as chunk `id`: one field 1 per value; loading refills the list with as many zeros as there are
// fields first.
void IntListField(ChunkArchive* ar, int id, std::vector<int>* values) {
    if (!ar->EnterChunk(id)) return;
    int count;
    if (ar->loading) {
        count = ar->CountChunks(1);
        values->assign(static_cast<std::size_t>(count), 0);
    } else {
        count = static_cast<int>(values->size());
    }
    for (int i = 0; i < count; ++i) {
        ar->Current()->occurrence = i + 1;
        ar->Field(1, &(*values)[static_cast<std::size_t>(i)], 4);
    }
    ar->LeaveChunk();
}

} // namespace

namespace itemdb {

// --- Records -------------------------------------------------------------------------------------

void SpellRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->StringField(1, &code);
    ar->StringField(2, &unreadText);
    ar->Field(3, &price);
    ar->Field(4, &castType);
    ar->Field(5, &mana);
    ar->Field(6, &complexity);
    ar->Field(7, &speed);
    ar->Field(8, &range);
    ar->Field(9, &area);
    ar->Field(10, &damage);
    ar->Field(11, &targetKind);
    ar->Field(12, &targetCount);
    ar->Field(13, &duration);
    ar->Field(14, &castTime);
    ar->Field(15, &needsSight);
    IntListField(ar, 16, &unreadIdListA);
    IntListField(ar, 17, &unreadIdListB);
    ar->Field(18, &icon);
    ar->Field(19, &school);
    ar->Field(20, allowedModifiers, 5);
    ar->Field(21, &skill);
    ar->Field(22, &classMask);
    for (int i = 0; i < 5; ++i) ar->Field(23 + i, &extraValues[i]);
}

void SpellModifierRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(0, &name);
    ar->Field(1, &code[0], 4);
    ar->Field(2, &price);
    ar->Field(3, &kind);
    ar->Field(4, &mana);
    ar->Field(5, &strength);
    ar->Field(6, &skill);
    ar->StringField(7, &unreadText);
    ar->Field(8, &classMask);
}

void SpellListRecord::Serialize(ChunkArchive* ar) {
    ar->StringField(1, &spell);
    InlineStrings(ar, 2, &required);
    InlineStrings(ar, 3, &optional);
    ar->StringField(4, &repeated);
    ar->Field(5, &classMask);
}

// --- Tables --------------------------------------------------------------------------------------

template <typename Record>
void CodeIndexedTable<Record>::Load(CStream* source, int chunk) {
    ChunkArchive archive;
    archive.Open(source, 1, 1);
    RecordChunks(&archive, chunk, &records, &Record::Serialize);
    archive.Close(source);
    // A modifier's code is four chars, read as a C string.
    for (Record& record : records) byCode[std::string(record.code)] = &record;
}

template <typename Record>
Record* CodeIndexedTable<Record>::FindByCode(std::string_view code) {
    return byCode[std::string(code)];
}
template SpellRecord* SpellTable::FindByCode(std::string_view);
template SpellModifierRecord* SpellModifierTable::FindByCode(std::string_view);

int SpellDatabase::Load(const std::string& folder, const std::string& archive) {
    std::string path = folder + archive;
    dbfile::LocalResource resource;
    if (!resource.Open(path))
        return game::FatalError("Can't open database file \"%s\"\n", path);
    std::uint32_t size;
    void* data = resource.Get()->CMappedResource::GetFile("spells.sdb", &size);
    if (!data) return game::FatalError("Can't open spells database\n");

    CMemoryStream stream;
    dbfile::FillStream(stream, data, size);
    spells.Load(&stream, kSpellsChunk);
    modifiers.Load(&stream, kModifiersChunk);
    // The three recipe lists through one archive.
    ChunkArchive ar;
    ar.Open(&stream, 1, 1);
    RecordChunks(&ar, kScrollRecipesChunk, &scrollRecipes, &SpellListRecord::Serialize);
    RecordChunks(&ar, kArmorRecipesChunk, &armorRecipes, &SpellListRecord::Serialize);
    RecordChunks(&ar, kWeaponRecipesChunk, &weaponRecipes, &SpellListRecord::Serialize);
    ar.Close(&stream);

    resource.Get()->CMappedResource::CloseFile(data);
    resource.Get()->CMappedResource::CloseResource();
    return 1;
}

} // namespace itemdb
