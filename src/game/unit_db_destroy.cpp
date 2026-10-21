// Lifetime of the unit databases (docs/notes/units.md): the unit database at 007AF358 (constructor
// 00539A10, destructor 0053A050), its prototype / NPC / model tables alone (00539B20, 00539CE0,
// 00539E70), the record destructors (0053EBC0, 0053ED30, 0053EE70) and the skill database at
// 007AF268 (0051FB50). All are thiscall (or fastcall) with no arguments; the destructors run at exit
// (0053A050 and 0051FB50 from the dynamic atexit functions 0053A040 / 0051FB40) and when the
// database file changes (0057DEE0 destroys each table, constructs the database again and loads it);
// the record destructors also from the tables' erase / reallocation code.
#include "unit_db.h"


#include "ei/hook.h"
#include "originals_library.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"

#include <cstddef>
#include <cstdint>
#include "originals_units.h"
#include "lib/crt.h"
#include "lib/stlp_vector.h"

namespace unitdb {

// An STLport vector, its element type left open (sizes are passed explicitly).
using RawVector = stlp::vector<char>;
static_assert(sizeof(RawVector) == 0xC);

// The map from a record's name to its index (STLport hash_map<string, int>, 0x14 bytes, 20-byte
// nodes).
using NameTable = stlp::NameMap;
static_assert(sizeof(NameTable) == 0x14);

// One table of the unit database (0x20 bytes): the records and the map from name to index.
struct RecordTable {
    RawVector records;   // +0x00
    NameTable names;     // +0x0C
};
static_assert(sizeof(RecordTable) == 0x20);

// The unit database (007AF358): prototypes (0x120-byte records), NPCs (0x60), models (0x168) and
// skill levels (0x44).
struct UnitDatabase {
    RecordTable prototypes;   // +0x00
    RecordTable npcs;         // +0x20
    RecordTable models;       // +0x40
    RecordTable levels;       // +0x60
};
static_assert(sizeof(UnitDatabase) == 0x80);

// The skill database (007AF268): a vector of 0x18-byte level records, then one of 0x44-byte skill
// records.
struct SkillDatabase {
    RawVector levels;   // +0x00
    RawVector skills;   // +0x0C
};

UnitDatabase* __fastcall ConstructUnitDatabase(UnitDatabase* self);
void __fastcall DestroyUnitDatabase(UnitDatabase* self);
void __fastcall DestroyPrototypeTable(RecordTable* self);
void __fastcall DestroyNpcTable(RecordTable* self);
void __fastcall DestroyModelTable(RecordTable* self);
void __fastcall DestroySkillDatabase(SkillDatabase* self);
void __fastcall DestroyPrototypeRecord(UnitPrototypeRecord* record);
void __fastcall DestroyNpcRecord(UnitNpcRecord* record);
void __fastcall DestroyModelRecord(UnitModelRecord* record);

namespace {

// The record destructors as the tables call them.
void __fastcall ModelRecordDestroy(char* record) { DestroyModelRecord(reinterpret_cast<UnitModelRecord*>(record)); }
void __fastcall NpcRecordDestroy(char* record) { DestroyNpcRecord(reinterpret_cast<UnitNpcRecord*>(record)); }

constexpr int kNameBuckets = 100;   // the bucket count each name map asks for

// A name map's destructor as the tables inline it (clear: 0053C5E0, 0053C430, 0053C280, 0053C0D0,
// each node's string freed (0053D990 ...) and the node (0053CAA0 ...); then the buckets: 0053BF00
// ...).
void DestroyNames(NameTable& names) { names.Destroy(); }

void DestroyString(mfc::CString* s) { s->~CString(); }

// The storage of `v` (elements of `size` bytes) back to the node pool, the byte count rounded
// down to whole elements (signed division, as the original's IMUL).
void FreeStorage(RawVector* v, int size) {
    if (!v->begin()) return;
    int count = static_cast<int>(v->EndOfStorage() - v->begin()) / size;
    stlp::deallocate(v->begin(), static_cast<std::size_t>(count * size));
}

// The inline ~vector<CString> of the records: _Destroy, then the storage (00520EB0).
void DestroyStringVector(CStringVector* v) {
    DestroyStringRange(v->begin(), v->end());
    v->FreeStorage();
}

// A skill level record of the skill database (0x18 bytes). The layout is unit_db.h's SkillRecord
// (the table at +0x00, 007AF268); this file's two record names are the other way round.
struct SkillLevelRecord {
    mfc::CString name;          // +0x00
    mfc::CString field1;
    std::uint32_t field2;
    CStringVector statNames;
};
static_assert(sizeof(SkillLevelRecord) == 0x18);

// A skill record of the skill database (0x44 bytes): four CStrings among plain fields (unit_db.h's
// SkillLevelRecord, the table at +0x0C).
struct SkillRecord {
    mfc::CString field0;        // +0x00
    mfc::CString name;          // +0x04
    std::uint32_t field2;
    mfc::CString requiredLevelName;
    std::uint32_t exclusionGroup;
    mfc::CString requiredSkillName;
    std::uint8_t unused_18[0x2C];
};
static_assert(sizeof(SkillRecord) == 0x44);

} // namespace

// 0053A050. The tables are destroyed last to first; in each the name map is cleared and its
// buckets freed, then every record is destroyed (the end read once) and the storage freed.
void __fastcall DestroyUnitDatabase(UnitDatabase* self) {
    RecordTable& levels = self->levels;
    DestroyNames(levels.names);
    for (char *p = levels.records.begin(), *end = levels.records.end(); p != end; p += 0x44)
        reinterpret_cast<UnitLevelRecord*>(p)->DeleteScalar(0);   // 00540840
    FreeStorage(&levels.records, 0x44);   // deallocate(p, n), 0053BEB0

    RecordTable& models = self->models;
    DestroyNames(models.names);
    for (char *p = models.records.begin(), *end = models.records.end(); p != end; p += 0x168)
        ModelRecordDestroy(p);
    models.records.FreeStorage();

    RecordTable& npcs = self->npcs;
    DestroyNames(npcs.names);
    for (char *p = npcs.records.begin(), *end = npcs.records.end(); p != end; p += 0x60)
        NpcRecordDestroy(p);
    npcs.records.FreeStorage();

    RecordTable& prototypes = self->prototypes;
    DestroyNames(prototypes.names);
    for (char *p = prototypes.records.begin(), *end = prototypes.records.end(); p != end; p += 0x120)
        reinterpret_cast<UnitPrototypeRecord*>(p)->DeleteScalar(0);
    prototypes.records.FreeStorage();
}

// 00539CE0. The NPC table: the name map's clear inlined (each chain's values destroyed and nodes
// freed, each bucket emptied, the bucket count read again every pass; then the count 0), the
// buckets freed, then each record (0x60 bytes, UnitNpcRecord) destroyed in reverse member order
// and the storage freed. The original leaves junk in EAX; nothing reads it.
void __fastcall DestroyNpcTable(RecordTable* self) {
    DestroyNames(self->names);
    for (char *p = self->records.begin(), *end = self->records.end(); p != end; p += sizeof(UnitNpcRecord)) {
        auto* record = reinterpret_cast<UnitNpcRecord*>(p);
        DestroyStringVector(&record->startSpells);
        DestroyStringVector(&record->startQuickItems);
        stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>::Destruct(&record->startWeapons);
        stlp::VectorAbi<mfc::CString, stlp::ObjectElement<mfc::CString>>::Destruct(&record->skillNames);
        DestroyString(&record->name);
    }
    FreeStorage(&self->records, sizeof(UnitNpcRecord));
}

// 0051FB50. The skill records (their CStrings in reverse order), their storage, then the level
// records (the string vector, then the two CStrings) and their storage (the node pool's
// deallocate inlined; the same code as 00404D70).
void __fastcall DestroySkillDatabase(SkillDatabase* self) {
    for (char *p = self->skills.begin(), *end = self->skills.end(); p != end; p += sizeof(SkillRecord)) {
        auto* record = reinterpret_cast<SkillRecord*>(p);
        DestroyString(&record->requiredSkillName);
        DestroyString(&record->requiredLevelName);
        DestroyString(&record->name);
        DestroyString(&record->field0);
    }
    FreeStorage(&self->skills, sizeof(SkillRecord));
    for (char *p = self->levels.begin(), *end = self->levels.end(); p != end; p += sizeof(SkillLevelRecord)) {
        auto* record = reinterpret_cast<SkillLevelRecord*>(p);
        DestroyStringVector(&record->statNames);
        DestroyString(&record->field1);
        DestroyString(&record->name);
    }
    FreeStorage(&self->levels, sizeof(SkillLevelRecord));
}

namespace {
// The CRT's `eh vector destructor iterator` (array, element size, count, destructor; last to
// first) [006E859F, library]; the model slot's destructor, a jump to CString::~CString [0053F010].
void __stdcall VectorDestructor(void* a0, unsigned a1, int a2, void* a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }

// The record's inline ~vector<CString> with the storage freed by the node pool (00404D70 when
// begin is set; 00520F60 for the sound names does the same test itself), the end read once.
void DestroyStringsAndFree(CStringVector* v) {
    for (mfc::CString *p = v->begin(), *end = v->end(); p != end; ++p) DestroyString(p);
    if (v->begin())
        stlp::deallocate(v->begin(), static_cast<std::size_t>((static_cast<int>(reinterpret_cast<char*>(v->EndOfStorage()) -
                                                                                reinterpret_cast<char*>(v->begin())) >> 2) * 4));
}
} // namespace

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
    auto* counts = reinterpret_cast<RawVector*>(&record->soundCounts);
    if (counts->begin()) stlp::deallocate(counts->begin(), static_cast<std::size_t>((static_cast<int>(counts->EndOfStorage() - counts->begin()) >> 2) * 4));
    DestroyStringsAndFree(&record->soundNames);
    DestroyString(&record->soundFolder);
    DestroyString(&record->hitMaterial);
    DestroyStringsAndFree(&record->field20);
    DestroyStringsAndFree(&record->field1F);
    DestroyString(&record->figureName);
    VectorDestructor(record->slots, sizeof(UnitModelSlot), 6, reinterpret_cast<void*>(DestroyModelSlot));
    DestroyString(&record->name);
}

// 0053EBC0. A unit prototype record's destructor, members in reverse order: the two weapon names,
// the armour, spell and +0xE8 string vectors, the sound folder, the model name and the name.
void __fastcall DestroyPrototypeRecord(UnitPrototypeRecord* record) {
    DestroyString(&record->secondWeaponName);
    DestroyString(&record->weaponName);
    DestroyStringsAndFree(&record->armourNames);
    DestroyStringsAndFree(&record->spellNames);
    DestroyStringsAndFree(&record->lootItems);
    DestroyString(&record->soundFolder);
    DestroyString(&record->modelName);
    DestroyString(&record->name);
}

// 0053ED30. An NPC record's destructor: the four string vectors last to first, then the name.
void __fastcall DestroyNpcRecord(UnitNpcRecord* record) {
    DestroyStringsAndFree(&record->startSpells);
    DestroyStringsAndFree(&record->startQuickItems);
    DestroyStringsAndFree(&record->startWeapons);
    DestroyStringsAndFree(&record->skillNames);
    DestroyString(&record->name);
}

// 00539B20 / 00539E70. The prototype / model table as 0057DEE0 destroys it before a reload (the
// same steps as DestroyNpcTable): the name map cleared and its buckets freed, every record
// destroyed (the record destructors inlined; the same calls), the storage freed. The original
// leaves junk in EAX; nothing reads it.
void __fastcall DestroyPrototypeTable(RecordTable* self) {
    DestroyNames(self->names);
    for (char *p = self->records.begin(), *end = self->records.end(); p != end; p += sizeof(UnitPrototypeRecord))
        DestroyPrototypeRecord(reinterpret_cast<UnitPrototypeRecord*>(p));
    FreeStorage(&self->records, sizeof(UnitPrototypeRecord));
}
void __fastcall DestroyModelTable(RecordTable* self) {
    DestroyNames(self->names);
    for (char *p = self->records.begin(), *end = self->records.end(); p != end; p += sizeof(UnitModelRecord))
        DestroyModelRecord(reinterpret_cast<UnitModelRecord*>(p));
    FreeStorage(&self->records, sizeof(UnitModelRecord));
}

// 00539A10 (the dynamic initialiser of 007AF358 and 0057DEE0's reload). The four tables in order:
// each record vector empty (0053B440 ...), each name map with 100 buckets asked for (hashtable(n,
// hash, equal, allocator): 0053B4B0 ...; the functor bytes, never read, are 0 here). Returns the
// database.
UnitDatabase* __fastcall ConstructUnitDatabase(UnitDatabase* self) {
    for (RecordTable* table : {&self->prototypes, &self->npcs, &self->models, &self->levels}) {
        table->records.ConstructEmpty();
        table->names.Construct(kNameBuckets, 0, 0, 0);
    }
    return self;
}

} // namespace unitdb

EI_REWRITTEN(0x0053EE70, unitdb::DestroyModelRecord);
EI_REWRITTEN(0x0053F010, unitdb::DestroyModelSlot);
EI_REPLACE(0x0053A050, unitdb::DestroyUnitDatabase);
EI_REWRITTEN(0x00539CE0, unitdb::DestroyNpcTable);
EI_REPLACE(0x0051FB50, unitdb::DestroySkillDatabase);
EI_REWRITTEN(0x0053EBC0, unitdb::DestroyPrototypeRecord);
EI_REWRITTEN(0x0053ED30, unitdb::DestroyNpcRecord);
EI_REWRITTEN(0x00539B20, unitdb::DestroyPrototypeTable);
EI_REWRITTEN(0x00539E70, unitdb::DestroyModelTable);
EI_REPLACE(0x00539A10, unitdb::ConstructUnitDatabase);
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
    ConstructUnitDatabase(reinterpret_cast<unitdb::UnitDatabase*>(&g_unitDatabase.Get()));
    GameAtExit(ei::Code<AtExitFunction>(0x0053A040));
}
void __cdecl DestroyUnitDatabaseGlobal() {   // 0053A040
    DestroyUnitDatabase(reinterpret_cast<unitdb::UnitDatabase*>(&g_unitDatabase.Get()));
}
void __cdecl ConstructSkillDatabaseGlobal() {   // 0051FAF0
    g_skillDatabase->Construct();
    GameAtExit(ei::Code<AtExitFunction>(0x0051FB40));
}
void __cdecl DestroySkillDatabaseGlobal() {   // 0051FB40
    DestroySkillDatabase(reinterpret_cast<unitdb::SkillDatabase*>(&g_skillDatabase.Get()));
}
} // namespace

} // namespace unitdb

EI_REPLACE(0x005399F0, unitdb::ConstructUnitDatabaseGlobal);
EI_REPLACE(0x0053A040, unitdb::DestroyUnitDatabaseGlobal);
EI_REPLACE(0x0051FAF0, unitdb::ConstructSkillDatabaseGlobal);
EI_REPLACE(0x0051FB40, unitdb::DestroySkillDatabaseGlobal);
