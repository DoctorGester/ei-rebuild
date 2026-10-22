// Small CUnitServer slots and the order / step constructors (A-U6): the deleting destructor, class
// name and id, the motion getter, the empty slot 17, slot 53, UnitAction's and AnimationStep's
// constructors.
#include "combat.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
const char* const kServerName = "CUnitServer";
constexpr int kClassServer = 0x3E;
constexpr int kOrderNone = 9;
} // namespace

// 005460C0 / 005460D0 (slots 11 / 12).
const char* CUnitServer::GetClassName() { return kServerName; }
EI_REWRITTEN(0x005460C0, CUnitServer::GetClassName);
int CUnitServer::GetClassId() { return kClassServer; }
EI_REWRITTEN(0x005460D0, CUnitServer::GetClassId);

AIPath* CUnitServer::MotionSlot() { return MotionPath(); }
EI_REPLACE_METHOD(0x00546090, CUnitServer::MotionSlot);

void CUnitServer::EmptySlot17() {}
EI_REPLACE_METHOD(0x005460B0, CUnitServer::EmptySlot17);

void CUnitServer::MoveFactorsChanged() { UpdateMoveFactors(); }
EI_REWRITTEN(0x00552F30, CUnitServer::MoveFactorsChanged);

UnitAction* UnitAction::Construct() {
    target = nullptr;
    kind = kOrderNone;
    flag18 = 0;
    return this;
}
EI_REWRITTEN(0x00545F70, UnitAction::Construct);

AnimationStep* __fastcall ConstructAnimationStep(AnimationStep* step) {
    mfc::impl::InitAnimationStep12(step);
    step->padding = 0;   // SV7b2a: the original leaves it (a stack byte); the harness zeroes it there too
    return step;
}
EI_REWRITTEN(0x00550A60, ConstructAnimationStep);
