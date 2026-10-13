// Checks for unit_db_load.cpp (A-U12). The maps folder 0055E990 is checked in
// verify_world_map_files.cpp (A-W14 ported it too; one copy kept).
// Side by side:
// - UnitModelTable::FindByName / UnitLevelTable::FindByName (0053D240, 0053D650): reached when
//   the original loader 0053A200 runs, that is at every start of a Rebirth launch (in the vanilla
//   launch our loader calls ours directly). A lookup leaves an entry for an unknown name, so ours
//   runs first and the original then finds what ours left: the same record either way.
// Ours only (the vanilla launch only, Rebirth patches both; they fill the databases, which are
// checked afterwards):
// - UnitDatabase::Load (0053A200), SkillDatabase::Load (00520220): every start of the game, and each
//   switch of the resource database (entering or leaving a network game, 0057CB50).
#include "verify.h"

#include "../../src/game/unit_db.h"

#include <cstring>

namespace {
void* s_unused;

// How many records of [begin, end) the name index does not lead to a record of the same name for
// (a later record of a repeated name replaces the earlier one, so names are compared, not
// records).
template <typename Record, typename Index>
int Unindexed(const Record* begin, const Record* end, const Index& index) {
    int missing = 0;
    for (const Record* record = begin; record != end; ++record) {
        const char* name = record->name;
        auto* node = index.Find(stlp::string(name));
        if (!node || !node->value) {
            ++missing;
            continue;
        }
        const char* found = reinterpret_cast<const Record*>(node->value)->name;
        if (std::strcmp(found, name) != 0) ++missing;
    }
    return missing;
}
} // namespace

// PUSH -1 (2 bytes), PUSH imm32 (5).
static UnitModelRecord*(__fastcall* s_FindModel)(UnitModelTable*, void*, const char*);
static UnitModelRecord* __fastcall FindModelVerify(UnitModelTable* self, void*, const char* name) {
    UnitModelRecord* actual = self->FindByName(name);
    UnitModelRecord* expected = s_FindModel(self, nullptr, name);
    ei::verify::Check("UnitModelTable::FindByName", static_cast<void*>(expected), static_cast<void*>(actual), "%s",
                      name);
    return expected;
}
EI_VERIFY(0x0053D240, FindModelVerify, 7, s_FindModel);

static UnitLevelRecord*(__fastcall* s_FindLevel)(UnitLevelTable*, void*, const char*);
static UnitLevelRecord* __fastcall FindLevelVerify(UnitLevelTable* self, void*, const char* name) {
    UnitLevelRecord* actual = self->FindByName(name);
    UnitLevelRecord* expected = s_FindLevel(self, nullptr, name);
    ei::verify::Check("UnitLevelTable::FindByName", static_cast<void*>(expected), static_cast<void*>(actual), "%s",
                      name);
    return expected;
}
EI_VERIFY(0x0053D650, FindLevelVerify, 7, s_FindLevel);

static int __fastcall UnitDatabaseLoadVerify(UnitDatabase* self, void*, const char* folder, const char* archive) {
    int result = self->Load(folder, archive);
    if (result != 1) return result;
    const auto* prototypes = reinterpret_cast<const UnitPrototypeRecord*>(self->prototypes.begin);
    const auto* prototypesEnd = reinterpret_cast<const UnitPrototypeRecord*>(self->prototypes.end);
    const auto* npcs = reinterpret_cast<const UnitNpcRecord*>(self->npcs.begin);
    const auto* npcsEnd = reinterpret_cast<const UnitNpcRecord*>(self->npcs.end);
    const int models = static_cast<int>(self->models.end - self->models.begin);
    const int levels = static_cast<int>(self->levels.end - self->levels.begin);
    ei::verify::Check("UnitDatabase::Load", true, prototypes != prototypesEnd && models > 0 && levels > 0,
                      "%d prototypes, %d models, %d levels", static_cast<int>(prototypesEnd - prototypes), models,
                      levels);
    ei::verify::Check("UnitDatabase::Load", 0, Unindexed(prototypes, prototypesEnd, self->prototypes.byName),
                      "prototype names");
    ei::verify::Check("UnitDatabase::Load", 0, Unindexed(npcs, npcsEnd, self->npcs.byName), "NPC names");
    ei::verify::Check("UnitDatabase::Load", 0, Unindexed(self->models.begin, self->models.end, self->models.byName),
                      "model names");
    ei::verify::Check("UnitDatabase::Load", 0, Unindexed(self->levels.begin, self->levels.end, self->levels.byName),
                      "level names");
    // The indexes the load resolves: within their tables, and naming the record they stand for.
    int bad = 0;
    for (const UnitPrototypeRecord* p = prototypes; p != prototypesEnd; ++p) {
        if (p->modelIndex < -1 || p->modelIndex >= models) ++bad;
        else if (p->modelIndex >= 0 && std::strcmp(self->models.begin[p->modelIndex].name, p->modelName) != 0) ++bad;
    }
    ei::verify::Check("UnitDatabase::Load", 0, bad, "model indexes");
    bad = 0;
    for (const UnitModelRecord* m = self->models.begin; m != self->models.end; ++m) {
        for (const UnitModelSlot& slot : m->slots) {
            if (slot.levelIndex < -1 || slot.levelIndex >= levels) ++bad;
            else if (slot.levelIndex >= 0 && std::strcmp(self->levels.begin[slot.levelIndex].name, slot.levelName) != 0)
                ++bad;
        }
    }
    ei::verify::Check("UnitDatabase::Load", 0, bad, "level indexes");
    return result;
}
EI_VERIFY(0x0053A200, UnitDatabaseLoadVerify, 0, s_unused);

static int __fastcall SkillDatabaseLoadVerify(SkillDatabase* self, void*, const char* folder, const char* archive) {
    int result = self->Load(folder, archive);
    if (result != 1) return result;
    ei::verify::Check("SkillDatabase::Load", true, self->skills.size() > 0 && self->levels.size() > 0,
                      "%u skills, %u levels", static_cast<unsigned>(self->skills.size()),
                      static_cast<unsigned>(self->levels.size()));
    return result;
}
EI_VERIFY(0x00520220, SkillDatabaseLoadVerify, 0, s_unused);
