// Checks for party_manager.cpp. Saving a game writes the party manager (004A8560); loading one reads
// it back (004A8310). Scenarios: every new game (autosave), world_load (load savea), ui_saveload.
// - PartyManager::Save: the original into a scratch memory stream first, then ours into the real
//   one; the appended bytes compared. Every party saved draws one ISAAC key (CParty::SaveParty):
//   the generator is put back to its state before ours runs.
// - PartyManager::Load: ours only (the archive's object keys are the saving game's pointers, so the
//   bytes cannot be saved back); the block was consumed, every template is a class 1..5 with a
//   reference, every record is non-null with a reference.
// - NamedPartyRecords::BeginRedeploy / EndRedeploy (004A6050 / 004A6360): ours only (see the wrappers);
//   every save reaches EndRedeploy (SaveGame), the script's RedeployParty both (zone_* scenarios).
// The archive chunks (004ACEC0, 004AD0C0, 004ACDC0, 004AD2C0, 004A64B0) are called only by Save / Load
// and directly, so the Save comparison covers them.
#include "verify.h"

#include "../../src/game/core_streams.h"
#include "../../src/game/party.h"
#include "../../src/game/party_manager.h"
#include "../../src/game/random.h"
#include "../../src/lib/crt.h"

#include <cstdint>
#include <cstring>

namespace {

void* s_unused;

CMemoryStream* NewScratchStream(const CStream* like) {
    auto* scratch = static_cast<CMemoryStream*>(gamecrt::operator_new(sizeof(CMemoryStream)));
    scratch->Construct();
    scratch->version_ = like->version_;
    return scratch;
}

void DeleteScratchStream(CMemoryStream* scratch) {
    scratch->Destruct();
    gamecrt::operator_delete(scratch);
}

// 004A8560: PUSH -1; PUSH 0x7175F9 (7).
void(__fastcall* s_save)(PartyManager*, void*, CMemoryStream*);
void __fastcall SaveVerify(PartyManager* self, void*, CMemoryStream* to) {
    auto* random = static_cast<IsaacRandom*>(gamecrt::operator_new(sizeof(IsaacRandom)));
    std::memcpy(random, &IsaacRandom::Global(), sizeof(IsaacRandom));
    CMemoryStream* scratch = NewScratchStream(to);
    s_save(self, nullptr, scratch);
    std::memcpy(&IsaacRandom::Global(), random, sizeof(IsaacRandom));
    std::uint32_t start = to->position_;
    self->Save(to);
    std::uint32_t written = to->position_ - start;
    bool same = written == scratch->size_ && (written == 0 || std::memcmp(to->data_ + start, scratch->data_, written) == 0);
    ei::verify::Check("PartyManager::Save", true, same, "%u bytes, original %u", written, scratch->size_);
    DeleteScratchStream(scratch);
    gamecrt::operator_delete(random);
}

void __fastcall LoadVerify(PartyManager* self, void*, CMemoryStream* from) {
    std::uint32_t start = from->position_;
    self->Load(from);
    bool ok = from->position_ > start;
    int templates = static_cast<int>(self->templates.finish - self->templates.start);
    for (int i = 0; ok && i < templates; ++i) {
        const ClassTemplate* t = self->templates.start[i];
        ok = t && t->type >= 1 && t->type <= 5 && t->refCount >= 1;
    }
    int records = static_cast<int>(self->records.records.size());
    for (int i = 0; ok && i < records; ++i) {
        const PartyRecord* r = self->records.records[i];
        ok = r && r->refCount >= 1;
    }
    ei::verify::Check("PartyManager::Load", true, ok, "%d templates, %d records, %u bytes read", templates, records,
                      from->position_ - start);
}

bool SameText(const mfc::CString& a, const mfc::CString& b) { return std::strcmp(a, b) == 0; }

// 004A6050: ours only. The named record's party has `party`'s zone and variable count, and every
// record has its zone, +0x6C and variable count.
void __fastcall BeginRedeployVerify(NamedPartyRecords* self, void*, CParty* party) {
    self->BeginRedeploy(party);
    bool ok = true;
    for (PartyRecord* record : self->records) {
        auto* p = reinterpret_cast<CParty*>(record->party);
        ok = ok && SameText(p->zone_, party->zone_) && p->unknown_6C == party->unknown_6C &&
             p->variables_.Size() == party->variables_.Size();
    }
    ei::verify::Check("NamedPartyRecords::BeginRedeploy", true, ok, "%d records", static_cast<int>(self->records.size()));
}

// 004A6360: ours only. `party` has the named record's zone and +0x50 (or is cleared: empty zone,
// +0x50 zero), and text10 equals text0C.
void __fastcall EndRedeployVerify(NamedPartyRecords* self, void*, CParty* party) {
    auto* found = static_cast<CParty*>(PartyRecordsFind(self, nullptr, self->text0C));
    self->EndRedeploy(party);
    bool ok = SameText(self->text10, self->text0C);
    if (found) ok = ok && SameText(party->zone_, found->zone_) && party->unknown_50 == found->unknown_50;
    else ok = ok && party->zone_.GetLength() == 0 && party->unknown_50 == 0;
    ei::verify::Check("NamedPartyRecords::EndRedeploy", true, ok, "record found: %d", found != nullptr);
}

} // namespace

EI_VERIFY(0x004A6050, BeginRedeployVerify, 0, s_unused);
EI_VERIFY(0x004A6360, EndRedeployVerify, 0, s_unused);
EI_VERIFY(0x004A8560, SaveVerify, 7, s_save);
EI_VERIFY(0x004A8310, LoadVerify, 0, s_unused);
