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

#include <cstddef>
#include <cstdint>
#include "originals_library.h"
#include "originals_units.h"

namespace unitdb {

// An STLport vector, its element type left open (sizes are passed explicitly).
using RawVector = stlp::vector<char>;
static_assert(sizeof(RawVector) == 0xC);

// A bucket chain node of the name tables: next, then the value (a CString key and the index).
struct HashNode {
    HashNode* next;
    std::uint8_t value[8];
};

// An STLport hash_map<CString, int> (0x14 bytes): the functors (empty, one byte), the bucket
// vector and the element count; the node allocator (empty) shares the count's address.
struct NameTable {
    std::uint32_t functors;   // +0x00
    HashNode** buckets;       // +0x04
    HashNode** bucketsEnd;    // +0x08
    HashNode** bucketsEos;    // +0x0C
    int count;                // +0x10
};
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

// The tables' template code, not rewritten (STLport instantiations, config/library.csv, and the
// records' own destructors). Each table: hashtable::clear and the bucket vector's destructor,
// then each record's destructor, then the record vector's storage.
__declspec(naked) void __fastcall LevelNamesClear(NameTable*) EI_ORIGINAL(0x0053C5E0)
__declspec(naked) void __fastcall LevelBucketsFree(HashNode***) EI_ORIGINAL(0x0053BF00)
__declspec(naked) void __fastcall LevelRecordDestroy(char*, void*, int /*delete*/) EI_ORIGINAL(0x00540840)
// 0053BEB0: deallocate(p, n) of 0x44-byte records, `this` the allocator proxy (the vector's eos).
__declspec(naked) void __fastcall LevelsDeallocate(char**, void*, char* /*p*/, int /*n*/) EI_ORIGINAL(0x0053BEB0)
__declspec(naked) void __fastcall ModelNamesClear(NameTable*) EI_ORIGINAL(0x0053C430)
__declspec(naked) void __fastcall ModelBucketsFree(HashNode***) EI_ORIGINAL(0x0053B930)
void __fastcall ModelRecordDestroy(char* record) { UnitModelDestroyOriginal(record); }
__declspec(naked) void __fastcall ModelsFree(RawVector*) EI_ORIGINAL(0x0053B7A0)
__declspec(naked) void __fastcall NpcNamesClear(NameTable*) EI_ORIGINAL(0x0053C280)
__declspec(naked) void __fastcall NpcBucketsFree(HashNode***) EI_ORIGINAL(0x005150D0)
void __fastcall NpcRecordDestroy(char* record) { DestroyNpcRecord(reinterpret_cast<UnitNpcRecord*>(record)); }
__declspec(naked) void __fastcall NpcsFree(RawVector*) EI_ORIGINAL(0x0053B600)
__declspec(naked) void __fastcall PrototypeNamesClear(NameTable*) EI_ORIGINAL(0x0053C0D0)
__declspec(naked) void __fastcall PrototypeBucketsFree(HashNode***) EI_ORIGINAL(0x0053BE70)
__declspec(naked) void __fastcall PrototypeRecordDestroy(char*, void*, int /*delete*/) EI_ORIGINAL(0x005407E0)
__declspec(naked) void __fastcall PrototypesFree(RawVector*) EI_ORIGINAL(0x0053B460)

// The NPC table's pieces: a node value's destructor (0053D990, RET 4, flag 0), the node
// allocator's deallocate(node, 1) (0053CAA0, RET 8, `this` the allocator at the count),
// _Destroy(first, last) of CStrings (005214B0, ECX / EDX), a vector<CString>'s storage
// (00520EB0) and a whole vector<CString>'s destructor (0053F020).
__declspec(naked) void __fastcall NameValueDestroy(void*, void*, int) EI_ORIGINAL(0x0053D990)
__declspec(naked) void __fastcall NameNodeDeallocate(int*, void*, HashNode*, int) EI_ORIGINAL(0x0053CAA0)
// The same two for the prototype table (0053D970 / 0053CA60) and the model table (0053D9B0 / 0053CAE0).
__declspec(naked) void __fastcall PrototypeNameValueDestroy(void*, void*, int) EI_ORIGINAL(0x0053D970)
__declspec(naked) void __fastcall PrototypeNameNodeDeallocate(int*, void*, HashNode*, int) EI_ORIGINAL(0x0053CA60)
__declspec(naked) void __fastcall ModelNameValueDestroy(void*, void*, int) EI_ORIGINAL(0x0053D9B0)
__declspec(naked) void __fastcall ModelNameNodeDeallocate(int*, void*, HashNode*, int) EI_ORIGINAL(0x0053CAE0)

// The tables' constructors (STLport instantiations): each record vector's base (RET 4, the
// allocator by reference) and each name map's hashtable(n, hash, equal, allocator) (RET 0x10).
__declspec(naked) void* __fastcall PrototypesConstruct(RawVector*, void*, const void*) EI_ORIGINAL(0x0053B440)
__declspec(naked) void* __fastcall PrototypeNamesConstruct(NameTable*, void*, int, const void*, const void*, const void*)
    EI_ORIGINAL(0x0053B4B0)
__declspec(naked) void* __fastcall NpcsConstruct(RawVector*, void*, const void*) EI_ORIGINAL(0x0053B5E0)
__declspec(naked) void* __fastcall NpcNamesConstruct(NameTable*, void*, int, const void*, const void*, const void*)
    EI_ORIGINAL(0x0053B650)
__declspec(naked) void* __fastcall ModelsConstruct(RawVector*, void*, const void*) EI_ORIGINAL(0x0053B780)
__declspec(naked) void* __fastcall ModelNamesConstruct(NameTable*, void*, int, const void*, const void*, const void*)
    EI_ORIGINAL(0x0053B800)
__declspec(naked) void* __fastcall LevelsConstruct(RawVector*, void*, const void*) EI_ORIGINAL(0x0053B980)
__declspec(naked) void* __fastcall LevelNamesConstruct(NameTable*, void*, int, const void*, const void*, const void*)
    EI_ORIGINAL(0x0053BA00)
constexpr int kNameBuckets = 100;   // the bucket count each name map asks for

using NameValueDestroyFn = void(__fastcall*)(void*, void*, int);
using NameNodeDeallocateFn = void(__fastcall*)(int*, void*, HashNode*, int);

// A name map's clear as the table destructors inline it: each chain's values destroyed and nodes
// freed, each bucket emptied (the bucket count read again every pass), the count 0; then the
// buckets freed.
void DestroyNames(NameTable& names, NameValueDestroyFn destroyValue, NameNodeDeallocateFn deallocateNode) {
    for (std::uint32_t b = 0; b < static_cast<std::uint32_t>(names.bucketsEnd - names.buckets); ++b) {
        for (HashNode* node = names.buckets[b]; node;) {
            HashNode* next = node->next;
            destroyValue(node->value, nullptr, 0);
            deallocateNode(&names.count, nullptr, node, 1);
            node = next;
        }
        names.buckets[b] = nullptr;
    }
    names.count = 0;
    if (names.buckets)
        stlp::deallocate(names.buckets, static_cast<std::size_t>(names.bucketsEos - names.buckets) * sizeof(HashNode*));
}
__declspec(naked) void __fastcall DestroyStrings(mfc::CString* /*first*/, mfc::CString* /*last*/) EI_ORIGINAL(0x005214B0)
__declspec(naked) void __fastcall StringVectorFree(CStringVector*) EI_ORIGINAL(0x00520EB0)
__declspec(naked) void __fastcall StringVectorDestroy(CStringVector*) EI_ORIGINAL(0x0053F020)

void DestroyString(mfc::CString* s) { s->~CString(); }

// The storage of `v` (elements of `size` bytes) back to the node pool, the byte count rounded
// down to whole elements (signed division, as the original's IMUL).
void FreeStorage(RawVector* v, int size) {
    if (!v->begin()) return;
    int count = static_cast<int>(v->EndOfStorage() - v->begin()) / size;
    stlp::deallocate(v->begin(), static_cast<std::size_t>(count * size));
}

// The inline ~vector<CString> of the records: _Destroy, then the storage.
void DestroyStringVector(CStringVector* v) {
    DestroyStrings(v->begin(), v->end());
    StringVectorFree(v);
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
    LevelNamesClear(&levels.names);
    LevelBucketsFree(&levels.names.buckets);
    for (char *p = levels.records.begin(), *end = levels.records.end(); p != end; p += 0x44)
        LevelRecordDestroy(p, nullptr, 0);
    LevelsDeallocate(&levels.records.EndOfStorage(), nullptr, levels.records.begin(),
                     static_cast<int>(levels.records.EndOfStorage() - levels.records.begin()) / 0x44);

    RecordTable& models = self->models;
    ModelNamesClear(&models.names);
    ModelBucketsFree(&models.names.buckets);
    for (char *p = models.records.begin(), *end = models.records.end(); p != end; p += 0x168)
        ModelRecordDestroy(p);
    ModelsFree(&models.records);

    RecordTable& npcs = self->npcs;
    NpcNamesClear(&npcs.names);
    NpcBucketsFree(&npcs.names.buckets);
    for (char *p = npcs.records.begin(), *end = npcs.records.end(); p != end; p += 0x60)
        NpcRecordDestroy(p);
    NpcsFree(&npcs.records);

    RecordTable& prototypes = self->prototypes;
    PrototypeNamesClear(&prototypes.names);
    PrototypeBucketsFree(&prototypes.names.buckets);
    for (char *p = prototypes.records.begin(), *end = prototypes.records.end(); p != end; p += 0x120)
        PrototypeRecordDestroy(p, nullptr, 0);
    PrototypesFree(&prototypes.records);
}

// 00539CE0. The NPC table: the name map's clear inlined (each chain's values destroyed and nodes
// freed, each bucket emptied, the bucket count read again every pass; then the count 0), the
// buckets freed, then each record (0x60 bytes, UnitNpcRecord) destroyed in reverse member order
// and the storage freed. The original leaves junk in EAX; nothing reads it.
void __fastcall DestroyNpcTable(RecordTable* self) {
    DestroyNames(self->names, NameValueDestroy, NameNodeDeallocate);
    for (char *p = self->records.begin(), *end = self->records.end(); p != end; p += sizeof(UnitNpcRecord)) {
        auto* record = reinterpret_cast<UnitNpcRecord*>(p);
        DestroyStringVector(&record->startSpells);
        DestroyStringVector(&record->startQuickItems);
        StringVectorDestroy(&record->startWeapons);
        StringVectorDestroy(&record->skillNames);
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
void __stdcall VectorDestructor(void* a0, unsigned a1, int a2, std::uintptr_t a3) { EhVectorDestructorIterator(a0, a1, a2, (void*)a3); }
constexpr std::uintptr_t kModelSlotDestructor = 0x0053F010;

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

UnitLevelRecord* __fastcall DeleteLevelRecord(UnitLevelRecord* self, void*, unsigned flags) {
    DestroyLevelRecord(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

UnitPrototypeRecord* __fastcall DeletePrototypeRecord(UnitPrototypeRecord* self, void*, unsigned flags) {
    DestroyPrototypeRecord(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}

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
    VectorDestructor(record->slots, sizeof(UnitModelSlot), 6, kModelSlotDestructor);
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
    DestroyNames(self->names, PrototypeNameValueDestroy, PrototypeNameNodeDeallocate);
    for (char *p = self->records.begin(), *end = self->records.end(); p != end; p += sizeof(UnitPrototypeRecord))
        DestroyPrototypeRecord(reinterpret_cast<UnitPrototypeRecord*>(p));
    FreeStorage(&self->records, sizeof(UnitPrototypeRecord));
}
void __fastcall DestroyModelTable(RecordTable* self) {
    DestroyNames(self->names, ModelNameValueDestroy, ModelNameNodeDeallocate);
    for (char *p = self->records.begin(), *end = self->records.end(); p != end; p += sizeof(UnitModelRecord))
        DestroyModelRecord(reinterpret_cast<UnitModelRecord*>(p));
    FreeStorage(&self->records, sizeof(UnitModelRecord));
}

// 00539A10 (the dynamic initialiser of 007AF358 and 0057DEE0's reload). The four tables in order:
// each record vector empty, each name map with 100 buckets asked for. Returns the database.
UnitDatabase* __fastcall ConstructUnitDatabase(UnitDatabase* self) {
    std::uint8_t functors[3] = {};   // the empty hash / equal / allocator objects passed by reference
    PrototypesConstruct(&self->prototypes.records, nullptr, &functors[0]);
    PrototypeNamesConstruct(&self->prototypes.names, nullptr, kNameBuckets, &functors[0], &functors[1], &functors[2]);
    NpcsConstruct(&self->npcs.records, nullptr, &functors[0]);
    NpcNamesConstruct(&self->npcs.names, nullptr, kNameBuckets, &functors[0], &functors[1], &functors[2]);
    ModelsConstruct(&self->models.records, nullptr, &functors[0]);
    ModelNamesConstruct(&self->models.names, nullptr, kNameBuckets, &functors[0], &functors[1], &functors[2]);
    LevelsConstruct(&self->levels.records, nullptr, &functors[0]);
    LevelNamesConstruct(&self->levels.names, nullptr, kNameBuckets, &functors[0], &functors[1], &functors[2]);
    return self;
}

} // namespace unitdb

EI_REPLACE(0x0053EE70, unitdb::DestroyModelRecord);
EI_REPLACE(0x0053F010, unitdb::DestroyModelSlot);
EI_REPLACE(0x0053A050, unitdb::DestroyUnitDatabase);
EI_REPLACE(0x00539CE0, unitdb::DestroyNpcTable);
EI_REPLACE(0x0051FB50, unitdb::DestroySkillDatabase);
EI_REPLACE(0x0053EBC0, unitdb::DestroyPrototypeRecord);
EI_REPLACE(0x0053ED30, unitdb::DestroyNpcRecord);
EI_REPLACE(0x00539B20, unitdb::DestroyPrototypeTable);
EI_REPLACE(0x00539E70, unitdb::DestroyModelTable);
EI_REPLACE(0x00539A10, unitdb::ConstructUnitDatabase);
EI_REPLACE(0x0053EFF0, unitdb::DestroyLevelRecord);
EI_REPLACE(0x00540840, unitdb::DeleteLevelRecord);
EI_REPLACE(0x005407E0, unitdb::DeletePrototypeRecord);
EI_REPLACE(0x005214B0, unitdb::DestroyStringRange);
