// 0054BBC0: the spell line-of-sight test the world's spell casting (0058E850) asks per target.
#include "combat.h"

#include "ai_map.h"
#include "core_maths.h"
#include "item_db.h"
#include "world.h"

#include "ei/hook.h"
#include "originals_ai.h"

#include <bit>

namespace {
constexpr float kAboveSurface = std::bit_cast<float>(0x3F800000u);   // 0073EF24 (1.0)
constexpr float kMinSight = std::bit_cast<float>(0x3DCCCCCDu);       // 0073EEC0 (0.1)
} // namespace

bool __fastcall SpellHasLineOfSight(CWorldServer* world, CSpell* spell, CUnit* caster, const Vector3* from,
                                    CUnit* target, const Vector3* at) {
    const auto* record = reinterpret_cast<const itemdb::SpellRecord*>(spell->GetPrototype());
    if (!record->needsSight) return true;
    Vector3 to;
    if (!target) {
        to = *at;
        to.z = static_cast<float>(world->SurfaceHeightAt(to.x, to.y) + kAboveSurface);
    } else {
        to = {target->position.x, target->position.y, target->position.z};
        float ground = static_cast<float>(target->GroundHeight(to.x, to.y));
        to.z = static_cast<float>(target->ModeSpeed() + ground);
    }
    Vector3 eye = *from;
    if (caster) {
        float ground = static_cast<float>(caster->GroundHeight(eye.x, eye.y));
        eye.z = static_cast<float>(caster->BaseSpeed() + ground);
    }
    // The original compares the unrounded ST0 result (TEST AH,1: below or unordered fails).
    return world->AIMap()->LineOfSight(&eye, &to) >= kMinSight;
}
EI_REWRITTEN(0x0054BBC0, SpellHasLineOfSight);
