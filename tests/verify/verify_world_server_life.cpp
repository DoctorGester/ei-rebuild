// Checks for CWorldServer::Setup / Teardown (src/game/world_server_life.cpp). Both make or free
// the world's managers, so only ours runs; reached by every scenario (WinMain calls Setup at
// start-up and Teardown at exit).
#include "verify.h"

#include "../../src/game/object.h"
#include "../../src/game/registry.h"
#include "../../src/game/world.h"

namespace {
void* s_unused;
} // namespace

// 0058AE10: returns 1 with the side-specific manager and the CHoover made and counted, the exits
// empty, the zone name empty and the messages registry mapped.
static int __fastcall SetupVerify(CWorldServer* self, void*) {
    int result = self->Setup();
    bool ok = result == 1 && self->manager44_ && self->manager44_->refCount_ >= 1 && self->hoover_ &&
              self->hoover_->refCount_ == 1 && !self->exits_ && self->exitCount_ == 0 && self->zoneName_ &&
              self->zoneName_[0] == 0 && self->mobFiles_.size == 0 && self->Messages()->data_ != nullptr &&
              self->players_ != nullptr;
    ei::verify::Check("CWorldServer::Setup", true, ok, "result %d", result);
    return result;
}
EI_VERIFY(0x0058AE10, SetupVerify, 0, s_unused);

// 0058B020: afterwards the managers, the CHoover and the exits are gone and the registry is closed.
static void __fastcall TeardownVerify(CWorldServer* self, void*) {
    self->Teardown();
    bool ok = !self->hoover_ && !self->exits_ && self->exitCount_ == 0 && self->exitCapacity_ == 0 && !self->players_ &&
              !self->manager44_ && !self->manager40_ && self->Messages()->data_ == nullptr;
    ei::verify::Check("CWorldServer::Teardown", true, ok, "");
}
EI_VERIFY(0x0058B020, TeardownVerify, 0, s_unused);
