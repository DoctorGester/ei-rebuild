// Lifetime of the unit databases (docs/notes/units.md): the unit database (constructor, destructor), its prototype / NPC /
// model tables alone, the record destructors and the skill database. The destructors run at exit and when the database file
// changes (the resource reload destroys each table, constructs the database again and loads it); the record destructors
// also from the tables' erase / reallocation code.
#include "unit_db.h"


#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include "lib/crt.h"
#include "lib/stlp_vector.h"

namespace unitdb {

UnitDatabase* __fastcall ConstructUnitDatabase(UnitDatabase* self);
void __fastcall DestroyUnitDatabase(UnitDatabase* self);
void __fastcall DestroySkillDatabase(SkillDatabase* self);

namespace {

constexpr std::size_t kNameBuckets = 100;   // the bucket count each name map asks for

template <typename Text>
void DestroyString(Text* s) {
    std::destroy_at(s);
}

// The inline ~vector<CString> of the records: _Destroy, then the storage (the node pool's deallocate of the
// capacity in whole elements).
void DestroyStringVector(CStringVector* v) {
    DestroyStringRange(v->begin(), v->end());
    v->FreeStorage();
}

} // namespace

// A table's record storage back to the node pool: the capacity in whole records (a signed division of the
// byte span by the record size), when there is any.
template <typename Record>
void FreeRecords(Record* begin, Record* endOfStorage) {
    if (begin) stlp::deallocate(begin, static_cast<std::size_t>(endOfStorage - begin) * sizeof(Record));
}

// The tables are destroyed last to first; in each the name map is cleared (each node's string freed, then
// the node) and its buckets freed, then every record is destroyed (the end read once) and the storage
// freed.
void __fastcall DestroyUnitDatabase(UnitDatabase* self) {
    UnitLevelTable& levels = self->levels;
    levels.byName.Destroy();
    for (UnitLevelRecord *p = levels.begin, *end = levels.end; p != end; ++p) p->DeleteScalar(0);
    FreeRecords(levels.begin, levels.endOfStorage);

    UnitModelTable& models = self->models;
    models.byName.Destroy();
    for (UnitModelRecord *p = models.begin, *end = models.end; p != end; ++p) DestroyModelRecord(p);
    FreeRecords(models.begin, models.endOfStorage);

    NpcPrototypeTable& npcs = self->npcs;
    npcs.byName.Destroy();
    for (UnitNpcRecord *p = npcs.begin, *end = npcs.end; p != end; ++p) DestroyNpcRecord(p);
    FreeRecords(npcs.begin, npcs.endOfStorage);

    UnitPrototypeTable& prototypes = self->prototypes;
    prototypes.byName.Destroy();
    for (UnitPrototypeRecord *p = prototypes.begin, *end = prototypes.end; p != end; ++p) p->DeleteScalar(0);
    FreeRecords(prototypes.begin, prototypes.endOfStorage);
}

// The skill level records (their CStrings in reverse order), their storage, then the skills (the string
// vector, then the two CStrings) and their storage (the node pool's deallocate inlined).
void __fastcall DestroySkillDatabase(SkillDatabase* self) {
    for (SkillLevelRecord *record = self->levels.begin(), *end = self->levels.end(); record != end; ++record) {
        DestroyString(&record->requiredSkillName);
        DestroyString(&record->requiredLevelName);
        DestroyString(&record->name);
        DestroyString(&record->field0);
    }
    self->levels.FreeStorage();
    for (SkillRecord *record = self->skills.begin(), *end = self->skills.end(); record != end; ++record) {
        DestroyStringVector(&record->statNames);
        DestroyString(&record->field1);
        DestroyString(&record->name);
    }
    self->skills.FreeStorage();
}

// A model slot's destructor: its level name (the one member to destroy); the CRT's vector destructor iterator
// runs it on the record's six slots.
void __fastcall DestroyModelSlot(UnitModelSlot* slot) { DestroyString(&slot->levelName); }

void __fastcall DestroyLevelRecord(UnitLevelRecord* record) { DestroyString(&record->name); }

} // namespace unitdb

UnitLevelRecord* UnitLevelRecord::DeleteScalar(unsigned flags) {
    unitdb::DestroyLevelRecord(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

UnitPrototypeRecord* UnitPrototypeRecord::DeleteScalar(unsigned flags) {
    unitdb::DestroyPrototypeRecord(this);
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}

namespace unitdb {

void __fastcall DestroyStringRange(mfc::CString* first, mfc::CString* last) {
    for (; first != last; ++first) DestroyString(first);
}

// A unit model record's destructor, members in reverse order: the sound counts (storage only), the sound
// names, the two strings, the two unread string vectors, the figure name, the six slots and the name.
void __fastcall DestroyModelRecord(UnitModelRecord* record) {
    record->soundCounts.FreeStorage();
    DestroyStringVector(&record->soundNames);
    DestroyString(&record->soundFolder);
    DestroyString(&record->hitMaterial);
    DestroyStringVector(&record->field20);
    DestroyStringVector(&record->field1F);
    DestroyString(&record->figureName);
    for (int i = 6; --i >= 0;) DestroyModelSlot(&record->slots[i]);
    DestroyString(&record->name);
}

// A unit prototype record's destructor, members in reverse order: the two weapon names, the armour, spell and
// loot item string vectors, the sound folder, the model name and the name.
void __fastcall DestroyPrototypeRecord(UnitPrototypeRecord* record) {
    DestroyString(&record->secondWeaponName);
    DestroyString(&record->weaponName);
    DestroyStringVector(&record->armourNames);
    DestroyStringVector(&record->spellNames);
    DestroyStringVector(&record->lootItems);
    DestroyString(&record->soundFolder);
    DestroyString(&record->modelName);
    DestroyString(&record->name);
}

// An NPC record's destructor: the four string vectors last to first, then the name.
void __fastcall DestroyNpcRecord(UnitNpcRecord* record) {
    DestroyStringVector(&record->startSpells);
    DestroyStringVector(&record->startQuickItems);
    DestroyStringVector(&record->startWeapons);
    DestroyStringVector(&record->skillNames);
    DestroyString(&record->name);
}

namespace {
// A table as the database constructor builds it: no records, a name map with 100 buckets asked for.
template <typename Table>
void ConstructTable(Table& table) {
    table.begin = nullptr;
    table.end = nullptr;
    table.endOfStorage = nullptr;
    table.byName.Construct(kNameBuckets);
}
} // namespace

// The database constructor (the dynamic initialiser and the reload). The four tables in order: each record
// vector empty, each name map with 100 buckets asked for (hashtable(n, hash, equal, allocator); the functor
// bytes, never read, are 0 here). Returns the database.
UnitDatabase* __fastcall ConstructUnitDatabase(UnitDatabase* self) {
    ConstructTable(self->prototypes);
    ConstructTable(self->npcs);
    ConstructTable(self->models);
    ConstructTable(self->levels);
    return self;
}

} // namespace unitdb

EI_REWRITTEN(0x0053EE70, unitdb::DestroyModelRecord);
EI_REWRITTEN(0x0053F010, unitdb::DestroyModelSlot);
EI_REWRITTEN(0x0053A050, unitdb::DestroyUnitDatabase);
EI_REWRITTEN(0x0051FB50, unitdb::DestroySkillDatabase);
EI_REWRITTEN(0x0053EBC0, unitdb::DestroyPrototypeRecord);
EI_REWRITTEN(0x0053ED30, unitdb::DestroyNpcRecord);
EI_REWRITTEN(0x00539A10, unitdb::ConstructUnitDatabase);
EI_REWRITTEN(0x0053EFF0, unitdb::DestroyLevelRecord);
EI_REWRITTEN(0x00540840, UnitLevelRecord::DeleteScalar);
EI_REWRITTEN(0x005407E0, UnitPrototypeRecord::DeleteScalar);
EI_REWRITTEN(0x005214B0, unitdb::DestroyStringRange);

// ---- The unit and skill databases --------------------------------------------------------------------------------

// Built empty by static initialisers in the list's order and destroyed by the exit functions those register
// (docs/notes/standalone.md, "The static initialisers' objects").
namespace unitdb {

namespace {

void __cdecl DestroyUnitDatabaseGlobal() {
    DestroyUnitDatabase(&g_unitDatabase.Get());
}
void __cdecl DestroySkillDatabaseGlobal() {
    DestroySkillDatabase(&g_skillDatabase.Get());
}
} // namespace

void __cdecl ConstructUnitDatabaseGlobal() {
    ConstructUnitDatabase(&g_unitDatabase.Get());
    libcrt::AtExit(DestroyUnitDatabaseGlobal);
}
void __cdecl ConstructSkillDatabaseGlobal() {
    g_skillDatabase->Construct();
    libcrt::AtExit(DestroySkillDatabaseGlobal);
}

} // namespace unitdb

EI_REWRITTEN(0x005399F0, unitdb::ConstructUnitDatabaseGlobal);
EI_REWRITTEN(0x0053A040, unitdb::DestroyUnitDatabaseGlobal);
EI_REWRITTEN(0x0051FAF0, unitdb::ConstructSkillDatabaseGlobal);
EI_REWRITTEN(0x0051FB40, unitdb::DestroySkillDatabaseGlobal);
