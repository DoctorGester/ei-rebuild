// Checks for CUnitServer's Load / Save (src/game/combat_save.cpp).
//
// Save: the original and ours write into capture streams; bytes and write sizes are compared and
// the original's writes go on to the real stream. Load resolves references and loads the brain,
// so only ours runs, followed by consistency checks.
#include "verify.h"

#include "unit_capture_stream.h"
#include "../../src/game/combat.h"

namespace {
using unitverify::CaptureStream;
using SaveFn = void(__fastcall*)(CUnitServer*, void*, UnitStream*);
SaveFn s_originalSave;
void* s_unused;
} // namespace

static void __fastcall SaveServerVerify(CUnitServer* self, void*, UnitStream* stream) {
    CaptureStream expected(stream), actual(stream);
    s_originalSave(self, nullptr, &expected);
    self->SaveServer(&actual);
    ei::verify::Check("CUnitServer::SaveServer", true, expected.Same(actual), "%p: %u vs %u bytes, first difference at %d",
                      self, static_cast<unsigned>(expected.bytes.size()), static_cast<unsigned>(actual.bytes.size()),
                      expected.FirstDifference(actual));
    expected.Forward(stream);
}
EI_VERIFY(0x005561C0, SaveServerVerify, 5, s_originalSave);

// Load: ours only. +0x918 is cleared, the effect object (if any) is referenced, and the attackers
// list's count matches its nodes.
static void __fastcall LoadServerVerify(CUnitServer* self, void*, UnitStream* stream) {
    self->LoadServer(stream);
    auto* base = reinterpret_cast<char*>(self);
    int nodes = 0;
    for (void* node = *reinterpret_cast<void**>(base + 0x920); node; node = *static_cast<void**>(node)) ++nodes;
    bool ok = *reinterpret_cast<int*>(base + 0x918) == 0 && (!self->effectObject || self->effectObject->refCount_ >= 1) &&
              nodes == *reinterpret_cast<int*>(base + 0x928);
    ei::verify::Check("CUnitServer::LoadServer", true, ok, "%p version %d: %d attacker nodes", self, stream->version, nodes);
}
EI_VERIFY(0x00555D10, LoadServerVerify, 0, s_unused);
