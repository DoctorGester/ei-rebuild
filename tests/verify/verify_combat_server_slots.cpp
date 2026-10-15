// Checks for the small CUnitServer slots and the order / step constructors
// (src/game/combat_server_slots.cpp, A-U6). Class name / id: pure. AnimationStep::Construct: the
// original into a copy of the step, ours into the step, compared. The rest: ours only (the
// destructor frees, the order constructor may release, slot 53 is UpdateMoveFactors, whose own
// wrapper compares). Every scenario with units reaches the class slots, the step constructor and
// slot 53 (a unit's speed changes); the deleting destructor when a unit is removed (map_objects.txt).
#include "verify.h"

#include "../../src/game/combat.h"

#include <cstring>

namespace {
void* s_unused;
const char*(__fastcall* s_name)(CUnitServer*);
int(__fastcall* s_id)(CUnitServer*);
AnimationStep*(__fastcall* s_step)(AnimationStep*);
} // namespace

// 005460C0 / 005460D0: MOV EAX,imm32 (5 bytes).
static const char* __fastcall ServerNameVerify(CUnitServer* self) {
    const char* e = s_name(self);
    ei::verify::Check("UnitServerClassName", e, UnitServerClassName(self), "");
    return e;
}
EI_VERIFY(0x005460C0, ServerNameVerify, 5, s_name);
static int __fastcall ServerIdVerify(CUnitServer* self) {
    int e = s_id(self);
    ei::verify::Check("UnitServerClassId", e, UnitServerClassId(self), "");
    return e;
}
EI_VERIFY(0x005460D0, ServerIdVerify, 5, s_id);

// 00550A60: MOV EAX,ECX; XOR ECX,ECX; MOV byte ptr [EAX+1],0xFF (8 bytes). Byte 3 is compared
// without the original's (it leaves it; ours zeroes it, SV7b2a).
static AnimationStep* __fastcall StepConstructVerify(AnimationStep* self) {
    AnimationStep copy;
    std::memcpy(&copy, self, sizeof(copy));
    AnimationStep* expected = s_step(&copy);
    AnimationStep* actual = self->Construct();
    copy.padding = 0;
    ei::verify::Check("AnimationStep::Construct", true,
                      expected == &copy && actual == self && std::memcmp(&copy, self, sizeof(copy)) == 0, "");
    return actual;
}
EI_VERIFY(0x00550A60, StepConstructVerify, 8, s_step);

static CUnitServer* __fastcall ServerDeleteVerify(CUnitServer* self, void*, int flags) {
    CUnitServer* result = self->DeletingDestruct(flags);
    ei::verify::Check("CUnitServer::DeletingDestruct", static_cast<void*>(self), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x005460E0, ServerDeleteVerify, 0, s_unused);

static AIPath* __fastcall MotionVerify(CUnitServer* self) {
    AIPath* result = self->MotionSlot();
    ei::verify::Check("CUnitServer::MotionSlot", static_cast<void*>(self->MotionPath()), static_cast<void*>(result), "");
    return result;
}
EI_VERIFY(0x00546090, MotionVerify, 0, s_unused);

static void __fastcall Slot17Verify(CUnitServer* self) {
    self->EmptySlot17();
    ei::verify::Check("CUnitServer::EmptySlot17", true, true, "");
}
EI_VERIFY(0x005460B0, Slot17Verify, 0, s_unused);

static void __fastcall Slot53Verify(CUnitServer* self) {
    self->UpdateMoveFactorsSlot53();
    ei::verify::Check("CUnitServer::UpdateMoveFactorsSlot53", true, self->speedClass >= 0, "speed class %d",
                      self->speedClass);
}
EI_VERIFY(0x00552F30, Slot53Verify, 0, s_unused);

static UnitAction* __fastcall ActionConstructVerify(UnitAction* self) {
    UnitAction* result = self->Construct();
    ei::verify::Check("UnitAction::Construct", true,
                      result == self && self->kind == 9 && !self->target && self->flag18 == 0, "");
    return result;
}
EI_VERIFY(0x00545F70, ActionConstructVerify, 0, s_unused);
