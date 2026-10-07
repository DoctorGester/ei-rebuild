// Small CUnitServer slots and the order / step constructors (A-U6): the deleting destructor, class
// name and id, the motion getter, the empty slot 17, slot 53, UnitAction's and AnimationStep's
// constructors.
#include "combat.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
const char* const kServerName = reinterpret_cast<const char*>(0x0078FCC4);   // "CUnitServer"
constexpr int kClassServer = 0x3E;
constexpr int kOrderNone = 9;
} // namespace

// 005460C0 / 005460D0 (slots 11 / 12).
const char* __fastcall UnitServerClassName(CUnitServer*) { return kServerName; }
EI_REPLACE(0x005460C0, UnitServerClassName);
int __fastcall UnitServerClassId(CUnitServer*) { return kClassServer; }
EI_REPLACE(0x005460D0, UnitServerClassId);

CUnitServer* CUnitServer::DeletingDestruct(int flags) {
    DestructBody();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x005460E0, CUnitServer::DeletingDestruct);

AIPath* CUnitServer::MotionSlot() { return MotionPath(); }
EI_REPLACE_METHOD(0x00546090, CUnitServer::MotionSlot);

void CUnitServer::EmptySlot17() {}
EI_REPLACE_METHOD(0x005460B0, CUnitServer::EmptySlot17);

void CUnitServer::UpdateMoveFactorsSlot53() { UpdateMoveFactors(); }
EI_REPLACE_METHOD(0x00552F30, CUnitServer::UpdateMoveFactorsSlot53);

UnitAction* UnitAction::Construct() {
    target = nullptr;
    kind = kOrderNone;
    flag18 = 0;
    return this;
}
EI_REPLACE_METHOD(0x00545F70, UnitAction::Construct);

AnimationStep* AnimationStep::Construct() {
    unknown_01 = 0xFF;
    animation = 0xFF;
    duration = -1;
    unknown_08 = 0;
    unknown_02 = 0;
    return this;
}
EI_REPLACE_METHOD(0x00550A60, AnimationStep::Construct);
