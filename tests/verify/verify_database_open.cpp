// Checks for item_db_open.cpp, model_db_open.cpp and quest_db_open.cpp (A-H26): the loads of the
// spell and item databases (00684550, 006A67A0), of prints.db and acks.db (006D0630, 006D7620) and
// of the quest database (006AE3A0).
// All five are Rebirth-patched, so they are hooked in the vanilla launch only. Ours only (they
// fill the databases; a second load would replace every record): the tables are checked afterwards.
// - SpellDatabase::Load, ItemDatabase::Load: every start of the game, and each switch of the
//   resource database (entering or leaving a network game, 0057CB50).
// - PrintsDatabase::Load, AckDatabase::Load, QuestDatabase::Load: every start of the game
//   (WinMain), on the main thread.
#include "verify.h"

#include "../../src/game/acks_db.h"
#include "../../src/game/item_db.h"
#include "../../src/game/model.h"
#include "../../src/game/quest_records.h"

#include <cstring>

namespace {
void* s_unused;

// How many records of `records` the index does not lead to a record of the same key for (a later
// record of a repeated key replaces the earlier one, so keys are compared, not records).
template <typename Index, typename Key>
int Unindexed(const RecordTable& records, int stride, const Index& index, Key key) {
    int missing = 0;
    for (const char* record = records.begin; record != records.end; record += stride) {
        auto* node = index.Find(stlp::string(key(record)));
        if (!node || !node->value || std::strcmp(key(node->value), key(record)) != 0) ++missing;
    }
    return missing;
}
template <typename Record, typename Index, typename Name>
int Unindexed(const stlp::vector<Record>& records, const Index& index, Name name) {
    int missing = 0;
    for (const Record* record = records.begin(); record != records.end(); ++record) {
        auto* node = index.Find(name(*record));
        if (!node || !node->value || !(name(*node->value) == name(*record))) ++missing;
    }
    return missing;
}

int Count(const RecordTable& records, int stride) { return static_cast<int>(records.end - records.begin) / stride; }

const char* SpellCode(const char* record) { return reinterpret_cast<const itemdb::SpellRecord*>(record)->code; }
const char* ModifierCode(const char* record) {
    return reinterpret_cast<const itemdb::SpellModifierRecord*>(record)->code;
}
const char* MaterialKey(const char* record) { return reinterpret_cast<const itemdb::MaterialRecordHead*>(record)->key; }
const char* ItemName(const char* record) { return reinterpret_cast<const itemdb::ItemRecordHead*>(record)->text04; }

// Whether `record`'s kind map leads from `kind` to `list` (null: entered without a list).
bool Says(const acks::AckRecordHead& record, int kind, const acks::SpeechList* list) {
    auto* node = record.speeches.Find(kind);
    return node && node->value == list;
}

int __fastcall SpellDatabaseLoadVerify(itemdb::SpellDatabase* self, void*, const char* folder, const char* archive) {
    const int result = self->Load(folder, archive);
    if (result != 1) return result;
    const int spells = Count(self->spells.records, itemdb::kSpellSize);
    const int modifiers = Count(self->modifiers.records, itemdb::kSpellModifierSize);
    ei::verify::Check("SpellDatabase::Load", true, spells > 0 && modifiers > 0, "%d spells, %d modifiers, lists %u %u %u",
                      spells, modifiers, static_cast<unsigned>(self->lists[0].size()),
                      static_cast<unsigned>(self->lists[1].size()), static_cast<unsigned>(self->lists[2].size()));
    ei::verify::Check("SpellDatabase::Load", 0,
                      Unindexed(self->spells.records, itemdb::kSpellSize, self->spells.byCode, SpellCode), "spell codes");
    ei::verify::Check("SpellDatabase::Load", 0,
                      Unindexed(self->modifiers.records, itemdb::kSpellModifierSize, self->modifiers.byCode, ModifierCode),
                      "modifier codes");
    return result;
}

int __fastcall ItemDatabaseLoadVerify(itemdb::ItemDatabase* self, void*, const char* folder, const char* archive) {
    const int result = self->Load(folder, archive);
    if (result != 1) return result;
    const int materials = Count(self->materials.records, itemdb::kMaterialSize);
    const int weapons = Count(self->weapons.records, itemdb::kWeaponSize);
    const int armors = Count(self->armors.records, itemdb::kArmorSize);
    ei::verify::Check("ItemDatabase::Load", true, materials > 0 && weapons > 0 && armors > 0,
                      "%d materials, %d weapons, %d armors", materials, weapons, armors);
    ei::verify::Check("ItemDatabase::Load", 0,
                      Unindexed(self->materials.records, itemdb::kMaterialSize, self->materials.ByName(), MaterialKey),
                      "material keys");
    ei::verify::Check("ItemDatabase::Load", 0,
                      Unindexed(self->weapons.records, itemdb::kWeaponSize, self->weapons.ByName(), ItemName),
                      "weapon names");
    ei::verify::Check("ItemDatabase::Load", 0,
                      Unindexed(self->armors.records, itemdb::kArmorSize, self->armors.ByName(), ItemName),
                      "armor names");
    return result;
}

int __fastcall PrintsDatabaseLoadVerify(PrintsDatabase* self, void*, const char* folder) {
    const int result = self->Load(folder);
    if (result != 1) return result;
    const auto name = [](const auto& record) -> const stlp::string& { return record.name; };
    ei::verify::Check("PrintsDatabase::Load", true,
                      self->blood.size() > 0 && self->footprints.size() > 0 && self->scorch.size() > 0,
                      "%u blood, %u footprints, %u scorch", static_cast<unsigned>(self->blood.size()),
                      static_cast<unsigned>(self->footprints.size()), static_cast<unsigned>(self->scorch.size()));
    ei::verify::Check("PrintsDatabase::Load", 0, Unindexed(self->blood, self->bloodByName, name), "blood names");
    ei::verify::Check("PrintsDatabase::Load", 0, Unindexed(self->footprints, self->footprintsByName, name),
                      "footprint names");
    ei::verify::Check("PrintsDatabase::Load", 0, Unindexed(self->scorch, self->scorchByName, name), "scorch names");
    return result;
}

int __fastcall AckDatabaseLoadVerify(acks::AckDatabase* self, void*, const char* folder) {
    const int result = self->Load(folder);
    if (result != 1) return result;
    const auto name = [](const auto& record) -> const stlp::string& { return record.head.name; };
    ei::verify::Check("AckDatabase::Load", true, self->units.records.size() > 0, "%u units, %u models, %u extras",
                      static_cast<unsigned>(self->units.records.size()),
                      static_cast<unsigned>(self->models.records.size()),
                      static_cast<unsigned>(self->extras.records.size()));
    ei::verify::Check("AckDatabase::Load", 0, Unindexed(self->units.records, self->units.byName, name), "unit names");
    ei::verify::Check("AckDatabase::Load", 0, Unindexed(self->models.records, self->models.byName, name),
                      "model names");
    ei::verify::Check("AckDatabase::Load", 0, Unindexed(self->extras.records, self->extras.byName, name),
                      "extra names");
    // The kind maps: a sample of each range of the units' (the original's own offsets: lists 0, 21,
    // 22, 28, 26, 32 and the named speeches; 0x16 without a list), every kind of the others.
    int bad = 0;
    for (const acks::UnitAcks* r = self->units.records.begin(); r != self->units.records.end(); ++r) {
        const bool ok = r->head.speeches.Size() == 40 && Says(r->head, 0x00, &r->lists[0]) &&
                        Says(r->head, 0x15, &r->lists[21]) && Says(r->head, 0x16, nullptr) &&
                        Says(r->head, 0x1B, nullptr) && Says(r->head, 0x1C, &r->lists[22]) &&
                        Says(r->head, 0x20, &r->lists[28]) && Says(r->head, 0x23, &r->lists[26]) &&
                        Says(r->head, 0x28, &r->lists[32]) && Says(r->head, 0x2A, &r->namedSpeeches);
        if (!ok) ++bad;
    }
    ei::verify::Check("AckDatabase::Load", 0, bad, "unit kinds");
    bad = 0;
    for (const acks::ModelAcks* r = self->models.records.begin(); r != self->models.records.end(); ++r) {
        bool ok = r->head.speeches.Size() == 5;
        for (int i = 0; ok && i < 5; ++i) ok = Says(r->head, 0x2E + i, &r->lists[i]);
        if (!ok) ++bad;
    }
    ei::verify::Check("AckDatabase::Load", 0, bad, "model kinds");
    bad = 0;
    for (const acks::ExtraAcks* r = self->extras.records.begin(); r != self->extras.records.end(); ++r) {
        if (!(r->head.speeches.Size() == 2 && Says(r->head, 0x2C, &r->lists[0]) && Says(r->head, 0x2D, &r->lists[1])))
            ++bad;
    }
    ei::verify::Check("AckDatabase::Load", 0, bad, "extra kinds");
    return result;
}

int __fastcall QuestDatabaseLoadVerify(QuestDatabase* self, void*, const char* folder) {
    const int result = self->Load(folder);
    if (result != 1) return result;
    const auto name = [](const auto& record) -> const stlp::string& { return record.name; };
    ei::verify::Check("QuestDatabase::Load", true, self->quests.size() > 0 && self->briefings.records.size() > 0,
                      "%u quests, %u briefings", static_cast<unsigned>(self->quests.size()),
                      static_cast<unsigned>(self->briefings.records.size()));
    ei::verify::Check("QuestDatabase::Load", 0, Unindexed(self->quests, self->questsByName, name), "quest names");
    ei::verify::Check("QuestDatabase::Load", 0, Unindexed(self->briefings.records, self->briefings.index, name),
                      "briefing names");
    return result;
}

} // namespace

EI_VERIFY(0x006AE3A0, QuestDatabaseLoadVerify, 0, s_unused);
EI_VERIFY(0x00684550, SpellDatabaseLoadVerify, 0, s_unused);
EI_VERIFY(0x006A67A0, ItemDatabaseLoadVerify, 0, s_unused);
EI_VERIFY(0x006D0630, PrintsDatabaseLoadVerify, 0, s_unused);
EI_VERIFY(0x006D7620, AckDatabaseLoadVerify, 0, s_unused);
