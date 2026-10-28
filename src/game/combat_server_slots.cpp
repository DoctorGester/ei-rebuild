// Small CUnitServer virtuals and the order / step constructors: the deleting destructor, class name and id,
// the motion getter, the empty virtual, UnitAction's and AnimationStep's constructors.
#include "combat.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace {
const char* const kServerName = "CUnitServer";
constexpr int kClassServer = 0x3E;
constexpr int kOrderNone = 9;
} // namespace

// Class name and id.
const char* CUnitServer::GetClassName() { return kServerName; }
EI_REWRITTEN(0x005460C0, CUnitServer::GetClassName);
int CUnitServer::GetClassId() { return kClassServer; }
EI_REWRITTEN(0x005460D0, CUnitServer::GetClassId);

void CUnitServer::MoveFactorsChanged() { UpdateMoveFactors(); }
EI_REWRITTEN(0x00552F30, CUnitServer::MoveFactorsChanged);

AnimationStep* __fastcall ConstructAnimationStep(AnimationStep* step) {
    mfc::impl::InitAnimationStep12(step);
    step->padding = 0;   // the original leaves it (a stack byte); the harness zeroes it there too
    return step;
}
EI_REWRITTEN(0x00550A60, ConstructAnimationStep);
