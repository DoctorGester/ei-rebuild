// Small CUnitServer virtuals and the order / step constructors: the deleting destructor, class name and id,
// the motion getter, the empty virtual, UnitAction's and AnimationStep's constructors.
#include "combat.h"

namespace {
const char* const kServerName = "CUnitServer";
} // namespace

// Class name and id.
const char* CUnitServer::ClassName() { return kServerName; }
ClassId CUnitServer::GetClassId() { return kClassUnitServer; }

void CUnitServer::MoveFactorsChanged() { UpdateMoveFactors(); }

