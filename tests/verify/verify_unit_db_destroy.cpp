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

namespace unitdb {
void __fastcall DestroyModelRecord(UnitModelRecord* record);
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
