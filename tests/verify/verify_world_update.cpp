// Checks for the world update (src/game/world_update.cpp). It runs the whole world tick, so only
// ours runs; it returns 1 and counts the update once.
#include "verify.h"

#include "../../src/game/world.h"

namespace {
void* s_unused;
}

// 0058DFF0 (vtable slot 6).
static int __fastcall UpdateVerify(CWorldServer* self, void*) {
    int before = self->updateCount_;
    int result = self->Update();
    ei::verify::Check("CWorldServer::Update", true, result == 1 && self->updateCount_ == before + 1,
                      "result %d, count %d -> %d", result, before, self->updateCount_);
    return result;
}
EI_VERIFY(0x0058DFF0, UpdateVerify, 0, s_unused);
