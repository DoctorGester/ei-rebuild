// Whether a spell the server world casts can reach its target (0054BBC0; A-U7). See combat.h and
// docs/notes/combat.md.
#include "combat.h"

#include "core_maths.h"
#include "originals_ai.h"
#include "spell.h"
#include "world.h"

#include "ei/hook.h"
#include "ei/x87.h"

#include <bit>

namespace {
constexpr float kAboveSurface = 1.0f;                               // 0073EF24
constexpr float kMinVisibility = std::bit_cast<float>(0x3DCCCCCDu); // 0073EEC0 (0.1)
constexpr int kNeedsSight = 0x40;                                   // spell prototype +0x40
} // namespace

bool __fastcall SpellLineOfSight(CWorldServer* world, CSpell* spell, CUnit* fromUnit, const Vector3* from,
                                 CUnit* toUnit, const Vector3* to) {
    const char* prototype = spell->GetPrototype();
    if (*reinterpret_cast<const int*>(prototype + kNeedsSight) == 0) return true;
    Vector3 target;
    if (!toUnit) {
        target = *to;
        target.z = static_cast<float>(world->SurfaceHeightAt(target.x, target.y) + kAboveSurface);
    } else {
        target = Vector3{toUnit->positionX, toUnit->positionY, toUnit->positionZ};
        float ground = StoreFloat(static_cast<float>(toUnit->GroundHeight(target.x, target.y)));
        target.z = static_cast<float>(toUnit->ModeSpeed() + ground);
    }
    Vector3 source = *from;
    if (fromUnit) {
        float ground = StoreFloat(static_cast<float>(fromUnit->GroundHeight(source.x, source.y)));
        source.z = static_cast<float>(fromUnit->BaseSpeed() + ground);
    }
    // FCOMP; TEST AH,1: below 0.1 or NaN fails.
    double visibility = LineOfSightOriginal(world->AIMap(), nullptr, &source, &target);
    return !x87::IsNaN(static_cast<float>(visibility)) && visibility >= kMinVisibility;
}
EI_REPLACE(0x0054BBC0, SpellLineOfSight);
