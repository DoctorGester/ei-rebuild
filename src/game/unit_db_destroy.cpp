// Destructors of the unit databases (docs/notes/units.md, "Ninth round"): the unit database at
// 007AF358 (0053A050, its four tables), the NPC table alone (00539CE0) and the skill database at
// 007AF268 (0051FB50). All three are thiscall with no arguments; they run at exit (0053A050 and
// 0051FB50 from the dynamic atexit functions 0053A040 / 0051FB40) and when the database file
// changes (0057DEE0 destroys each table, constructs the database again and loads it; 00539CE0 is
// its NPC table's step).
#include "unit_db.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>
#include "originals_library.h"

namespace unitdb {

// An STLport vector, its element type left open (sizes are passed explicitly).
struct RawVector {
    char* begin;
    char* end;
    char* eos;
};
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

void __fastcall DestroyUnitDatabase(UnitDatabase* self);
void __fastcall DestroyNpcTable(RecordTable* self);
void __fastcall DestroySkillDatabase(SkillDatabase* self);
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
__declspec(naked) void __fastcall ModelRecordDestroy(char*) EI_ORIGINAL(0x0053EE70)
__declspec(naked) void __fastcall ModelsFree(RawVector*) EI_ORIGINAL(0x0053B7A0)
__declspec(naked) void __fastcall NpcNamesClear(NameTable*) EI_ORIGINAL(0x0053C280)
__declspec(naked) void __fastcall NpcBucketsFree(HashNode***) EI_ORIGINAL(0x005150D0)
__declspec(naked) void __fastcall NpcRecordDestroy(char*) EI_ORIGINAL(0x0053ED30)
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
__declspec(naked) void __fastcall DestroyStrings(mfc::CString* /*first*/, mfc::CString* /*last*/) EI_ORIGINAL(0x005214B0)
__declspec(naked) void __fastcall StringVectorFree(CStringVector*) EI_ORIGINAL(0x00520EB0)
__declspec(naked) void __fastcall StringVectorDestroy(CStringVector*) EI_ORIGINAL(0x0053F020)

void DestroyString(mfc::CString* s) { s->~CString(); }

// The storage of `v` (elements of `size` bytes) back to the node pool, the byte count rounded
// down to whole elements (signed division, as the original's IMUL).
void FreeStorage(RawVector* v, int size) {
    if (!v->begin) return;
    int count = static_cast<int>(v->eos - v->begin) / size;
    stlp::deallocate(v->begin, static_cast<std::size_t>(count * size));
}

// The inline ~vector<CString> of the records: _Destroy, then the storage.
void DestroyStringVector(CStringVector* v) {
    DestroyStrings(v->begin, v->end);
    StringVectorFree(v);
}

// A skill level record of the skill database (0x18 bytes).
struct SkillLevelRecord {
    mfc::CString name;          // +0x00
    mfc::CString unknown_04;
    std::uint32_t unknown_08;
    CStringVector unknown_0C;
};
static_assert(sizeof(SkillLevelRecord) == 0x18);

// A skill record of the skill database (0x44 bytes): four CStrings among plain fields.
struct SkillRecord {
    mfc::CString name;          // +0x00
    mfc::CString unknown_04;
    std::uint32_t unknown_08;
    mfc::CString unknown_0C;
    std::uint32_t unknown_10;
    mfc::CString unknown_14;
    std::uint8_t unknown_18[0x2C];
};
static_assert(sizeof(SkillRecord) == 0x44);

} // namespace

// 0053A050. The tables are destroyed last to first; in each the name map is cleared and its
// buckets freed, then every record is destroyed (the end read once) and the storage freed.
void __fastcall DestroyUnitDatabase(UnitDatabase* self) {
    RecordTable& levels = self->levels;
    LevelNamesClear(&levels.names);
    LevelBucketsFree(&levels.names.buckets);
    for (char *p = levels.records.begin, *end = levels.records.end; p != end; p += 0x44)
        LevelRecordDestroy(p, nullptr, 0);
    LevelsDeallocate(&levels.records.eos, nullptr, levels.records.begin,
                     static_cast<int>(levels.records.eos - levels.records.begin) / 0x44);

    RecordTable& models = self->models;
    ModelNamesClear(&models.names);
    ModelBucketsFree(&models.names.buckets);
    for (char *p = models.records.begin, *end = models.records.end; p != end; p += 0x168)
        ModelRecordDestroy(p);
    ModelsFree(&models.records);

    RecordTable& npcs = self->npcs;
    NpcNamesClear(&npcs.names);
    NpcBucketsFree(&npcs.names.buckets);
    for (char *p = npcs.records.begin, *end = npcs.records.end; p != end; p += 0x60)
        NpcRecordDestroy(p);
    NpcsFree(&npcs.records);

    RecordTable& prototypes = self->prototypes;
    PrototypeNamesClear(&prototypes.names);
    PrototypeBucketsFree(&prototypes.names.buckets);
    for (char *p = prototypes.records.begin, *end = prototypes.records.end; p != end; p += 0x120)
        PrototypeRecordDestroy(p, nullptr, 0);
    PrototypesFree(&prototypes.records);
}

// 00539CE0. The NPC table: the name map's clear inlined (each chain's values destroyed and nodes
// freed, each bucket emptied, the bucket count read again every pass; then the count 0), the
// buckets freed, then each record (0x60 bytes, UnitNpcRecord) destroyed in reverse member order
// and the storage freed. The original leaves junk in EAX; nothing reads it.
void __fastcall DestroyNpcTable(RecordTable* self) {
    NameTable& names = self->names;
    for (std::uint32_t b = 0; b < static_cast<std::uint32_t>(names.bucketsEnd - names.buckets); ++b) {
        for (HashNode* node = names.buckets[b]; node;) {
            HashNode* next = node->next;
            NameValueDestroy(node->value, nullptr, 0);
            NameNodeDeallocate(&names.count, nullptr, node, 1);
            node = next;
        }
        names.buckets[b] = nullptr;
    }
    names.count = 0;
    if (names.buckets)
        stlp::deallocate(names.buckets, static_cast<std::size_t>(names.bucketsEos - names.buckets) * sizeof(HashNode*));

    for (char *p = self->records.begin, *end = self->records.end; p != end; p += sizeof(UnitNpcRecord)) {
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
    for (char *p = self->skills.begin, *end = self->skills.end; p != end; p += sizeof(SkillRecord)) {
        auto* record = reinterpret_cast<SkillRecord*>(p);
        DestroyString(&record->unknown_14);
        DestroyString(&record->unknown_0C);
        DestroyString(&record->unknown_04);
        DestroyString(&record->name);
    }
    FreeStorage(&self->skills, sizeof(SkillRecord));
    for (char *p = self->levels.begin, *end = self->levels.end; p != end; p += sizeof(SkillLevelRecord)) {
        auto* record = reinterpret_cast<SkillLevelRecord*>(p);
        DestroyStringVector(&record->unknown_0C);
        DestroyString(&record->unknown_04);
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
    for (mfc::CString *p = v->begin, *end = v->end; p != end; ++p) DestroyString(p);
    if (v->begin)
        stlp::deallocate(v->begin, static_cast<std::size_t>((static_cast<int>(reinterpret_cast<char*>(v->endOfStorage) -
                                                                              reinterpret_cast<char*>(v->begin)) >> 2) * 4));
}
} // namespace

// 0053EE70. A unit model record's destructor, members in reverse order: the vector of sound
// counts (+0x144, storage only), the sound names, the CStrings +0x124 / +0x118, the string
// vectors +0xF8 / +0xEC, the CString +0xE8, the six slots (their level names, through the CRT's
// vector destructor iterator) and the name.
void __fastcall DestroyModelRecord(UnitModelRecord* record) {
    auto* counts = reinterpret_cast<RawVector*>(&record->soundCounts);
    if (counts->begin) stlp::deallocate(counts->begin, static_cast<std::size_t>((static_cast<int>(counts->eos - counts->begin) >> 2) * 4));
    DestroyStringsAndFree(&record->soundNames);
    DestroyString(&record->soundFolder);
    DestroyString(&record->hitMaterial);
    DestroyStringsAndFree(&record->unknown_F8);
    DestroyStringsAndFree(&record->unknown_EC);
    DestroyString(&record->unknown_E8);
    VectorDestructor(record->slots, sizeof(UnitModelSlot), 6, kModelSlotDestructor);
    DestroyString(&record->name);
}

} // namespace unitdb

EI_REPLACE(0x0053EE70, unitdb::DestroyModelRecord);
EI_REPLACE(0x0053A050, unitdb::DestroyUnitDatabase);
EI_REPLACE(0x00539CE0, unitdb::DestroyNpcTable);
EI_REPLACE(0x0051FB50, unitdb::DestroySkillDatabase);
