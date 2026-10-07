// Checks for party_manager.cpp. Saving a game writes the party manager (004A8560); loading one reads
// it back (004A8310). Scenarios: ui_saveload (save, then load), party_records.
// - PartyManager::Save: the original into a scratch memory stream first, then ours into the real
//   one; the appended bytes compared. Every party saved draws one ISAAC key (CParty::SaveParty):
//   the generator is put back to its state before ours runs.
// - PartyManager::Load: ours only (the archive's object keys are the saving game's pointers, so the
//   bytes cannot be saved back); the block was consumed, every template is a class 1..5 with a
//   reference, every record is non-null with a reference.
// The archive chunks (004ACEC0, 004AD0C0, 004ACDC0, 004AD2C0, 004A64B0) are called only by these two
// and directly, so the Save comparison covers them.
#include "verify.h"

#include "../../src/game/core_streams.h"
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

} // namespace

EI_VERIFY(0x004A8560, SaveVerify, 7, s_save);
EI_VERIFY(0x004A8310, LoadVerify, 0, s_unused);
