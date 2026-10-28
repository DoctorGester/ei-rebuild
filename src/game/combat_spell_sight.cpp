// The spell line-of-sight test the world's spell casting asks per target.
#include "combat.h"

#include "ai_map.h"
#include "core_maths.h"
#include "item_db.h"
#include "world.h"

#include <bit>

namespace {
constexpr float kAboveSurface = std::bit_cast<float>(0x3F800000u);   // 1.0
constexpr float kMinSight = std::bit_cast<float>(0x3DCCCCCDu);       // 0.1
} // namespace

bool SpellHasLineOfSight(CWorldServer* world, CSpell* spell, CUnit* caster, const Vector3* from,
                                    CUnit* target, const Vector3* at) {
    const auto* record = spell->GetPrototype();
    if (!record->needsSight) return true;
    Vector3 to;
    if (!target) {
        to = *at;
        to.z = static_cast<float>(world->SurfaceHeightAt(to.x, to.y) + kAboveSurface);
    } else {
        to = {target->localPosition_.x, target->localPosition_.y, target->localPosition_.z};
        float ground = static_cast<float>(target->GroundHeight(to.x, to.y));
        to.z = static_cast<float>(target->ModeSpeed() + ground);
    }
    Vector3 eye = *from;
    if (caster) {
        float ground = static_cast<float>(caster->GroundHeight(eye.x, eye.y));
        eye.z = static_cast<float>(caster->BaseSpeed() + ground);
    }
    // The unrounded ST0 result is compared (TEST AH,1: below or unordered fails).
    return world->AIMap()->LineOfSight(&eye, &to) >= kMinSight;
}
