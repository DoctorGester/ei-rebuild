// Checks for the modes' lifetime and constant slots (src/game/unit_ai_mode_lifetime.cpp). Scenarios:
// unit_ai.txt (UMPatrol builds a patrol mode, UMClear destroys it; party units get player modes at
// the new game), script_exec.txt (UMFear / UMAg build and clear fear modes). The abstract base's
// Execute runs for modes without their own (none in the scenarios is known to use it).
#include "verify.h"

#include "../../src/game/unit_ai.h"

#include <cstring>

namespace {
void* s_unused;
}

// 005D0A90 LookAroundExecute: draws a random number and may attack, so ours only: an attack has
// a target afterwards.
static void __fastcall LookAroundExecuteVerify(AIMode* self, void*, AIBrain* brain) {
    aimode::LookAroundExecute(self, nullptr, brain);
    bool ok = !(brain->action == AIBrain::kActionAttack && brain->target == nullptr);
    ei::verify::Check("aimode::LookAroundExecute", true, ok, "brain %p action %d", brain, brain->action);
}
EI_VERIFY(0x005D0A90, LookAroundExecuteVerify, 0, s_unused);

// 005D1210 / 005D2920 GetType: MOV EAX,imm32 (5 bytes). Pure.
static int(__fastcall* s_patrolType)(AIMode*);
static int __fastcall PatrolTypeVerify(AIMode* self) {
    int expected = s_patrolType(self);
    ei::verify::Check("aimode::PatrolType", expected, aimode::PatrolType(self), "mode %p", self);
    return expected;
}
EI_VERIFY(0x005D1210, PatrolTypeVerify, 5, s_patrolType);

static int(__fastcall* s_fearType)(AIMode*);
static int __fastcall FearTypeVerify(AIMode* self) {
    int expected = s_fearType(self);
    ei::verify::Check("aimode::FearType", expected, aimode::FearType(self), "mode %p", self);
    return expected;
}
EI_VERIFY(0x005D2920, FearTypeVerify, 5, s_fearType);

// 005D3760 PlayerMode::Construct: SUB ESP,0xC; MOV EAX,ECX; PUSH ESI (6 bytes). Output: the
// original constructs a scratch copy, ours the real mode; the fields both write compared.
static PlayerMode*(__fastcall* s_playerConstruct)(PlayerMode*);
static PlayerMode* __fastcall PlayerConstructVerify(PlayerMode* self) {
    PlayerMode theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    PlayerMode* expected = s_playerConstruct(&theirs);
    PlayerMode* actual = self->Construct();
    bool same = expected == &theirs && actual == self && theirs.vtable == self->vtable && theirs.target == self->target &&
                theirs.leader == self->leader && std::memcmp(&theirs.point, &self->point, sizeof(Vector3)) == 0 &&
                theirs.state == self->state && theirs.run == self->run;
    ei::verify::Check("PlayerMode::Construct", true, same, "mode %p", self);
    return actual;
}
EI_VERIFY(0x005D3760, PlayerConstructVerify, 6, s_playerConstruct);

// 005D0F60 PatrolMode::Construct: MOV EAX,ECX; XOR ECX,ECX; MOV [EAX+0xC],ECX (7 bytes). Output,
// as above.
static PatrolMode*(__fastcall* s_patrolConstruct)(PatrolMode*);
static PatrolMode* __fastcall PatrolConstructVerify(PatrolMode* self) {
    PatrolMode theirs;
    std::memcpy(&theirs, self, sizeof(theirs));
    PatrolMode* expected = s_patrolConstruct(&theirs);
    PatrolMode* actual = self->Construct();
    bool same = expected == &theirs && actual == self && theirs.vtable == self->vtable && theirs.points.vtable == self->points.vtable &&
                theirs.points.data == self->points.data && theirs.points.size == self->points.size && theirs.points.maxSize == self->points.maxSize &&
                theirs.points.growBy == self->points.growBy && theirs.current == self->current;
    ei::verify::Check("PatrolMode::Construct", true, same, "mode %p", self);
    return actual;
}
EI_VERIFY(0x005D0F60, PatrolConstructVerify, 7, s_patrolConstruct);

// 005D1240 / 005D2950 destructors: free memory, so ours only: the root vtable afterwards.
static void __fastcall PatrolDestructVerify(PatrolMode* self) {
    self->Destruct();
    ei::verify::Check("PatrolMode::Destruct", aimode::kRootVtable, self->vtable, "mode %p", self);
}
EI_VERIFY(0x005D1240, PatrolDestructVerify, 0, s_unused);

static void __fastcall FearDestructVerify(FearMode* self) {
    self->Destruct();
    ei::verify::Check("FearMode::Destruct", aimode::kRootVtable, self->vtable, "mode %p", self);
}
EI_VERIFY(0x005D2950, FearDestructVerify, 0, s_unused);

// 005D1220 / 005D2930 deleting destructors: ours only: `this` returned (the object may be gone).
static PatrolMode* __fastcall PatrolDeleteVerify(PatrolMode* self, void*, int flags) {
    PatrolMode* result = self->DeletingDestruct(flags);
    ei::verify::Check("PatrolMode::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "flags %d", flags);
    return result;
}
EI_VERIFY(0x005D1220, PatrolDeleteVerify, 0, s_unused);

static FearMode* __fastcall FearDeleteVerify(FearMode* self, void*, int flags) {
    FearMode* result = self->DeletingDestruct(flags);
    ei::verify::Check("FearMode::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "flags %d", flags);
    return result;
}
EI_VERIFY(0x005D2930, FearDeleteVerify, 0, s_unused);
