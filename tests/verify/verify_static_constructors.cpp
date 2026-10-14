// Checks for the constructors of three global objects (A-E1): the map database (0045E820, at
// 0079BF00), the lever table (004632A0, at 0079C1A0) and the party manager (004A5870, at 007AA5A0).
// Each runs once from its static initialiser (0045E6C0, 004630F0, 004A56D0), before the hooks are
// installed, so these wrappers are never reached in a run; they are ours only (a second
// construction of a live object would drop its contents) and check what the constructor leaves.
#include "verify.h"

#include "../../src/game/lever.h"
#include "../../src/game/object_tables.h"
#include "../../src/game/party_manager.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

CMapDatabase* __fastcall MapDatabaseConstructVerify(CMapDatabase* self) {
    CMapDatabase* result = self->Construct();
    bool good = result == self && !self->allods.data && self->allods.size == 0 && self->allods.maxSize == 0 &&
                self->allods.growBy == 0 && !self->maps.data && self->maps.size == 0 && self->maps.maxSize == 0 &&
                self->maps.growBy == 0 &&
                self->allods.vtable == reinterpret_cast<void*>(CMapDatabase::kAllodArrayVtable) &&
                self->maps.vtable == reinterpret_cast<void*>(CMapDatabase::kMapArrayVtable);
    ei::verify::Check("CMapDatabase::Construct", true, good, "");
    return result;
}

LeverRecordTable* __fastcall LeverTableConstructVerify(LeverRecordTable* self) {
    LeverRecordTable* result = self->Construct();
    bool good = result == self && !self->begin && !self->end && !self->endOfStorage && self->byName.Size() == 0 &&
                self->byName.BucketCount() == stlp::NextPrime(100);
    ei::verify::Check("LeverRecordTable::Construct", true, good, "");
    return result;
}

PartyManager* __fastcall PartyManagerConstructVerify(PartyManager* self) {
    PartyManager* result = self->Construct();
    bool good = result == self && self->parties.Size() == 0 && self->parties.BucketCount() == stlp::NextPrime(100) &&
                self->entries.begin() == self->entries.end() && self->records.records.size() == 0 &&
                std::strcmp(self->records.text0C, "") == 0 && std::strcmp(self->records.text10, "") == 0 &&
                !self->templates.begin() && !self->templates.end() && !self->templates.EndOfStorage();
    ei::verify::Check("PartyManager::Construct", true, good, "");
    return result;
}
} // namespace

EI_VERIFY(0x0045E820, MapDatabaseConstructVerify, 0, s_unused);
EI_VERIFY(0x004632A0, LeverTableConstructVerify, 0, s_unused);
EI_VERIFY(0x004A5870, PartyManagerConstructVerify, 0, s_unused);
