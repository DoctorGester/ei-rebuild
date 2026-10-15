// Checks for unit_db_destroy.cpp. All of them free memory, so ours only.
//  - DestroyUnitDatabase (0053A050): afterwards every table's name map holds no elements (each
//    clear sets its count to 0). Runs at exit (atexit 0053A040), if the hooks still run then.
//  - DestroyNpcTable (00539CE0): afterwards the name map holds no elements. Reached when the
//    database file changes (0057DEE0: database.res <-> databaseLMP.res, from the multiplayer hero
//    screens; probably no scenario).
//  - DestroySkillDatabase (0051FB50): nothing it writes is left to read (the storage is freed);
//    reported when it runs. Runs at exit (atexit 0051FB40).
#include "verify.h"

#include <cstdint>

struct UnitModelRecord;
struct UnitModelSlot;
struct UnitLevelRecord;
struct UnitPrototypeRecord;
namespace mfc {
class CString;
} // namespace mfc

namespace unitdb {
void __fastcall DestroyModelRecord(UnitModelRecord* record);
void __fastcall DestroyModelSlot(UnitModelSlot* slot);
void __fastcall DestroyLevelRecord(UnitLevelRecord* record);
UnitLevelRecord* __fastcall DeleteLevelRecord(UnitLevelRecord* self, void*, unsigned flags);
UnitPrototypeRecord* __fastcall DeletePrototypeRecord(UnitPrototypeRecord* self, void*, unsigned flags);
void __fastcall DestroyStringRange(mfc::CString* first, mfc::CString* last);
struct UnitDatabase;
struct RecordTable;
struct SkillDatabase;
void __fastcall DestroyUnitDatabase(UnitDatabase* self);
void __fastcall DestroyNpcTable(RecordTable* self);
void __fastcall DestroySkillDatabase(SkillDatabase* self);
} // namespace unitdb

namespace {
void* s_unused;

// A table's name map element count (+0x1C of each 0x20-byte table).
int NameCount(const void* table) { return *reinterpret_cast<const int*>(static_cast<const std::uint8_t*>(table) + 0x1C); }
} // namespace

static void __fastcall DestroyUnitDatabaseVerify(unitdb::UnitDatabase* self) {
    unitdb::DestroyUnitDatabase(self);
    const auto* base = reinterpret_cast<const std::uint8_t*>(self);
    for (int t = 0; t < 4; ++t)
        ei::verify::Check("DestroyUnitDatabase", 0, NameCount(base + 0x20 * t), "table %d names", t);
}
EI_VERIFY(0x0053A050, DestroyUnitDatabaseVerify, 0, s_unused);

static void __fastcall DestroyNpcTableVerify(unitdb::RecordTable* self) {
    unitdb::DestroyNpcTable(self);
    ei::verify::Check("DestroyNpcTable", 0, NameCount(self), "names");
}
EI_VERIFY(0x00539CE0, DestroyNpcTableVerify, 0, s_unused);

static void __fastcall DestroySkillDatabaseVerify(unitdb::SkillDatabase* self) {
    unitdb::DestroySkillDatabase(self);
    ei::verify::Check("DestroySkillDatabase", true, true, "");
}
EI_VERIFY(0x0051FB50, DestroySkillDatabaseVerify, 0, s_unused);

// DestroyModelRecord (0053EE70, ticket U8): frees memory and destroys CStrings, ours only; nothing
// it writes is left to read, so it is reported when it runs. Reached from DestroyUnitDatabase at
// exit, from the model vector's erase on a database reload (0053DF50 inside 0053A200) and from its
// insert when it reallocates a non-empty vector (0053FCC0); no scenario is known to reach it.
static void __fastcall DestroyModelRecordVerify(UnitModelRecord* record) {
    unitdb::DestroyModelRecord(record);
    ei::verify::Check("DestroyModelRecord", true, true, "");
}
EI_VERIFY(0x0053EE70, DestroyModelRecordVerify, 0, s_unused);

// A-U8. The record destructors (0053EBC0 prototype, 0053ED30 NPC): ours only, reported when they
// run (the database load's erase 0053DC10 / 0053DD00 inside 0053A200 at startup, the tables'
// destructors at exit). The prototype / model tables (00539B20 / 00539E70, from 0057DEE0 only):
// ours only, the name map empty afterwards. The constructor (00539A10, 007AF358's dynamic
// initialiser and 0057DEE0): ours only; afterwards every table's records empty and its name map
// with buckets, all empty, and no elements.
struct UnitPrototypeRecord;
struct UnitNpcRecord;
namespace unitdb {
void __fastcall DestroyPrototypeRecord(UnitPrototypeRecord* record);
void __fastcall DestroyNpcRecord(UnitNpcRecord* record);
void __fastcall DestroyPrototypeTable(RecordTable* self);
void __fastcall DestroyModelTable(RecordTable* self);
UnitDatabase* __fastcall ConstructUnitDatabase(UnitDatabase* self);
} // namespace unitdb

static void __fastcall DestroyPrototypeRecordVerify(UnitPrototypeRecord* record) {
    unitdb::DestroyPrototypeRecord(record);
    ei::verify::Check("DestroyPrototypeRecord", true, true, "");
}
EI_VERIFY(0x0053EBC0, DestroyPrototypeRecordVerify, 0, s_unused);

static void __fastcall DestroyNpcRecordVerify(UnitNpcRecord* record) {
    unitdb::DestroyNpcRecord(record);
    ei::verify::Check("DestroyNpcRecord", true, true, "");
}
EI_VERIFY(0x0053ED30, DestroyNpcRecordVerify, 0, s_unused);

static void __fastcall DestroyPrototypeTableVerify(unitdb::RecordTable* self) {
    unitdb::DestroyPrototypeTable(self);
    ei::verify::Check("DestroyPrototypeTable", 0, NameCount(self), "names");
}
EI_VERIFY(0x00539B20, DestroyPrototypeTableVerify, 0, s_unused);

static void __fastcall DestroyModelTableVerify(unitdb::RecordTable* self) {
    unitdb::DestroyModelTable(self);
    ei::verify::Check("DestroyModelTable", 0, NameCount(self), "names");
}
EI_VERIFY(0x00539E70, DestroyModelTableVerify, 0, s_unused);

namespace {
// A fresh table (0x20 bytes): the records' begin / end / end of storage null, the name map's
// buckets (+0x10 .. +0x14) allocated and null, its element count (+0x1C) 0.
bool FreshTable(const std::uint8_t* table) {
    const auto* words = reinterpret_cast<void* const*>(table);
    if (words[0] || words[1] || words[2]) return false;
    auto* const* begin = static_cast<void* const*>(words[4]);
    auto* const* end = static_cast<void* const*>(words[5]);
    if (!begin || end <= begin) return false;
    for (auto* const* p = begin; p != end; ++p)
        if (*p) return false;
    return NameCount(table) == 0;
}
} // namespace

static unitdb::UnitDatabase* __fastcall ConstructUnitDatabaseVerify(unitdb::UnitDatabase* self) {
    unitdb::UnitDatabase* result = unitdb::ConstructUnitDatabase(self);
    const auto* base = reinterpret_cast<const std::uint8_t*>(self);
    bool ok = result == self;
    for (int t = 0; ok && t < 4; ++t) ok = FreshTable(base + 0x20 * t);
    ei::verify::Check("ConstructUnitDatabase", true, ok, "");
    return result;
}
EI_VERIFY(0x00539A10, ConstructUnitDatabaseVerify, 0, s_unused);

// DestroyModelSlot (0053F010, ticket S16): destroys the level name, ours only (counted). Reached
// through the vector destructor iterator in DestroyModelRecord.
static void __fastcall DestroyModelSlotVerify(UnitModelSlot* slot) {
    unitdb::DestroyModelSlot(slot);
    ei::verify::Check("DestroyModelSlot", true, true, "");
}
EI_VERIFY(0x0053F010, DestroyModelSlotVerify, 0, s_unused);

// S16a, all ours only (counted; they free): the level record's destructor, the level and prototype
// records' scalar deleting destructors (this back), and the CString range destructor.
static void __fastcall DestroyLevelRecordVerify(UnitLevelRecord* record) {
    unitdb::DestroyLevelRecord(record);
    ei::verify::Check("DestroyLevelRecord", true, true, "");
}
EI_VERIFY(0x0053EFF0, DestroyLevelRecordVerify, 0, s_unused);
static UnitLevelRecord* __fastcall DeleteLevelRecordVerify(UnitLevelRecord* self, void*, unsigned flags) {
    UnitLevelRecord* result = unitdb::DeleteLevelRecord(self, nullptr, flags);
    ei::verify::Check("DeleteLevelRecord", true, result == self, "flags %u", flags);
    return result;
}
EI_VERIFY(0x00540840, DeleteLevelRecordVerify, 0, s_unused);
static UnitPrototypeRecord* __fastcall DeletePrototypeRecordVerify(UnitPrototypeRecord* self, void*, unsigned flags) {
    UnitPrototypeRecord* result = unitdb::DeletePrototypeRecord(self, nullptr, flags);
    ei::verify::Check("DeletePrototypeRecord", true, result == self, "flags %u", flags);
    return result;
}
EI_VERIFY(0x005407E0, DeletePrototypeRecordVerify, 0, s_unused);
static void __fastcall DestroyStringRangeVerify(mfc::CString* first, mfc::CString* last) {
    unitdb::DestroyStringRange(first, last);
    ei::verify::Check("DestroyStringRange", true, first <= last, "");
}
EI_VERIFY(0x005214B0, DestroyStringRangeVerify, 0, s_unused);
