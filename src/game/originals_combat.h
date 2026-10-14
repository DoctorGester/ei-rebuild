#pragma once

// Original functions of the unit controller (CUnitServer) that take a combat.h type by value, which a declaration with a
// body needs complete (the other controller forwarders are in originals_units.h; types there are forward declared).

#include "combat.h"

#include "ei/hook.h"

// C3n. 00548DE0 (rewritten as CUnitServer::PlayAnimationOrder; thiscall, RET 0x18): the controller plays the animation list
// `steps` as an order (state 3). The list is passed by value (six dwords) and destroyed by the callee. Through its address
// so the wrapper runs.
__declspec(naked) inline void __fastcall CtlPlayAnimationOrder(CUnitServer* /*controller*/, void*, AnimationSteps /*steps*/)
    EI_ORIGINAL(0x00548DE0)
