// Lifetime of the unit databases (docs/notes/units.md): the unit database at 007AF358 (constructor
// 00539A10, destructor 0053A050), its prototype / NPC / model tables alone (00539B20, 00539CE0,
// 00539E70), the record destructors (0053EBC0, 0053ED30, 0053EE70) and the skill database at
// 007AF268 (0051FB50). All are thiscall (or fastcall) with no arguments; the destructors run at exit
// (0053A050 and 0051FB50 from the dynamic atexit functions 0053A040 / 0051FB40) and when the
// database file changes (0057DEE0 destroys each table, constructs the database again and loads it);
// the record destructors also from the tables' erase / reallocation code.
#include "unit_db.h"


#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"

#include <cstddef>
#include <cstdint>
#include "lib/crt.h"
#include "lib/stlp_vector.h"

namespace unitdb {

UnitDatabase* __fastcall ConstructUnitDatabase(UnitDatabase* self);
void __fastcall DestroyUnitDatabase(UnitDatabase* self);
void __fastcall DestroyPrototypeTable(UnitPrototypeTable* self);
void __fastcall DestroyNpcTable(NpcPrototypeTable* self);
void __fastcall DestroyModelTable(UnitModelTable* self);
void __fastcall DestroySkillDatabase(SkillDatabase* self);

namespace {

constexpr std::size_t kNameBuckets = 100;   // the bucket count each name map asks for

void DestroyString(mfc::CString* s) { s->~CString(); }

// The inline ~vector<CString> of the records: _Destroy (005214B0), then the storage (00520EB0 /
// 00404D70: the node pool's deallocate of the capacity in whole elements).
void DestroyStringVector(CStringVector* v) {
    DestroyStringRange(v->begin(), v->end());
    v->FreeStorage();
}

} // namespace

// A table's record storage back to the node pool: the capacity in whole records (the original's
// signed division of the byte span by the record size), when there is any.
template <typename Record>
void FreeRecords(Record* begin, Record* endOfStorage) {
    if (begin) stlp::deallocate(begin, static_cast<std::size_t>(endOfStorage - begin) * sizeof(Record));
}

// 0053A050. The tables are destroyed last to first; in each the name map is cleared (each node's
// string freed, then the node; 0053C5E0, 0053C430, 0053C280, 0053C0D0) and its buckets freed
// (0053BF00 ...), then every record is destroyed (the end read once) and the storage freed.
void __fastcall DestroyUnitDatabase(UnitDatabase* self) {
    UnitLevelTable& levels = self->levels;
    levels.byName.Destroy();
    for (UnitLevelRecord *p = levels.begin, *end = levels.end; p != end; ++p) p->DeleteScalar(0);   // 00540840
    FreeRecords(levels.begin, levels.endOfStorage);   // deallocate(p, n), 0053BEB0

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

// 00539CE0. The NPC table: the name map's clear inlined (each chain's values destroyed and nodes
// freed, each bucket emptied, the bucket count read again every pass; then the count 0), the
// buckets freed, then each record (0x60 bytes, UnitNpcRecord) destroyed in reverse member order
// and the storage freed. The original leaves junk in EAX; nothing reads it.
void __fastcall DestroyNpcTable(NpcPrototypeTable* self) {
    self->byName.Destroy();
    for (UnitNpcRecord *record = self->begin, *end = self->end; record != end; ++record) {
        DestroyStringVector(&record->startSpells);
        DestroyStringVector(&record->startQuickItems);
        stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>::Destruct(&record->startWeapons);
        stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>::Destruct(&record->skillNames);
        DestroyString(&record->name);
    }
    FreeRecords(self->begin, self->endOfStorage);
}

// 0051FB50. The skill level records (0x44 bytes at +0x0C; their CStrings in reverse order), their
// storage, then the skills (0x18 bytes at +0x00: the string vector, then the two CStrings) and
// their storage (the node pool's deallocate inlined; the same code as 00404D70).
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

// 0053F010. A model slot's destructor: a jump to CString::~CString on its level name (the one
// member to destroy); the CRT's vector destructor iterator runs it on the record's six slots.
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

// 0053EE70. A unit model record's destructor, members in reverse order: the vector of sound
// counts (+0x144, storage only), the sound names, the CStrings +0x124 / +0x118, the string
// vectors +0xF8 / +0xEC, the CString +0xE8, the six slots (their level names, through the CRT's
// vector destructor iterator) and the name.
void __fastcall DestroyModelRecord(UnitModelRecord* record) {
    record->soundCounts.FreeStorage();
    DestroyStringVector(&record->soundNames);
    DestroyString(&record->soundFolder);
    DestroyString(&record->hitMaterial);
    DestroyStringVector(&record->field20);
    DestroyStringVector(&record->field1F);
    DestroyString(&record->figureName);
    for (int i = 6; --i >= 0;) DestroyModelSlot(&record->slots[i]);   // the CRT's vector destructor iterator (006E859F)
    DestroyString(&record->name);
}

// 0053EBC0. A unit prototype record's destructor, members in reverse order: the two weapon names,
// the armour, spell and +0xE8 string vectors, the sound folder, the model name and the name.
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

// 0053ED30. An NPC record's destructor: the four string vectors last to first, then the name.
void __fastcall DestroyNpcRecord(UnitNpcRecord* record) {
    DestroyStringVector(&record->startSpells);
    DestroyStringVector(&record->startQuickItems);
    DestroyStringVector(&record->startWeapons);
    DestroyStringVector(&record->skillNames);
    DestroyString(&record->name);
}

// 00539B20 / 00539E70. The prototype / model table as 0057DEE0 destroys it before a reload (the
// same steps as DestroyNpcTable): the name map cleared and its buckets freed, every record
// destroyed (the record destructors inlined; the same calls), the storage freed. The original
// leaves junk in EAX; nothing reads it.
void __fastcall DestroyPrototypeTable(UnitPrototypeTable* self) {
    self->byName.Destroy();
    for (UnitPrototypeRecord *p = self->begin, *end = self->end; p != end; ++p) DestroyPrototypeRecord(p);
    FreeRecords(self->begin, self->endOfStorage);
}
void __fastcall DestroyModelTable(UnitModelTable* self) {
    self->byName.Destroy();
    for (UnitModelRecord *p = self->begin, *end = self->end; p != end; ++p) DestroyModelRecord(p);
    FreeRecords(self->begin, self->endOfStorage);
}

namespace {
// A table as 00539A10 builds it: no records (0053B440 ...), a name map with 100 buckets asked for.
template <typename Table>
void ConstructTable(Table& table) {
    table.begin = nullptr;
    table.end = nullptr;
    table.endOfStorage = nullptr;
    table.byName.Construct(kNameBuckets);
}
} // namespace

// 00539A10 (the dynamic initialiser of 007AF358 and 0057DEE0's reload). The four tables in order:
// each record vector empty (0053B440 ...), each name map with 100 buckets asked for (hashtable(n,
// hash, equal, allocator): 0053B4B0 ...; the functor bytes, never read, are 0 here). Returns the
// database.
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
EI_REWRITTEN(0x00539CE0, unitdb::DestroyNpcTable);
EI_REWRITTEN(0x0051FB50, unitdb::DestroySkillDatabase);
EI_REWRITTEN(0x0053EBC0, unitdb::DestroyPrototypeRecord);
EI_REWRITTEN(0x0053ED30, unitdb::DestroyNpcRecord);
EI_REWRITTEN(0x00539B20, unitdb::DestroyPrototypeTable);
EI_REWRITTEN(0x00539E70, unitdb::DestroyModelTable);
EI_REWRITTEN(0x00539A10, unitdb::ConstructUnitDatabase);
EI_REWRITTEN(0x0053EFF0, unitdb::DestroyLevelRecord);
EI_REWRITTEN(0x00540840, UnitLevelRecord::DeleteScalar);
EI_REWRITTEN(0x005407E0, UnitPrototypeRecord::DeleteScalar);
EI_REWRITTEN(0x005214B0, unitdb::DestroyStringRange);

// ---- The unit and skill databases (unit_db.h, unit_db_globals.cpp) --------------------------------------------------

// Built empty by the static initialisers 005399F0 and 0051FAF0 in the list's order and destroyed by the exit functions
// those register (0053A040, 0051FB40); ported on the names and registered at their addresses (docs/notes/standalone.md,
// "The static initialisers' objects").
namespace unitdb {

namespace {
using AtExitFunction = void(__cdecl*)();

void __cdecl ConstructUnitDatabaseGlobal() {   // 005399F0
    ConstructUnitDatabase(&g_unitDatabase.Get());
    libcrt::AtExit(ei::Code<AtExitFunction>(0x0053A040));
}
void __cdecl DestroyUnitDatabaseGlobal() {   // 0053A040
    DestroyUnitDatabase(&g_unitDatabase.Get());
}
void __cdecl ConstructSkillDatabaseGlobal() {   // 0051FAF0
    g_skillDatabase->Construct();
    libcrt::AtExit(ei::Code<AtExitFunction>(0x0051FB40));
}
void __cdecl DestroySkillDatabaseGlobal() {   // 0051FB40
    DestroySkillDatabase(&g_skillDatabase.Get());
}
} // namespace

} // namespace unitdb

EI_REPLACE(0x005399F0, unitdb::ConstructUnitDatabaseGlobal);
EI_REPLACE(0x0053A040, unitdb::DestroyUnitDatabaseGlobal);
EI_REPLACE(0x0051FAF0, unitdb::ConstructSkillDatabaseGlobal);
EI_REPLACE(0x0051FB40, unitdb::DestroySkillDatabaseGlobal);
