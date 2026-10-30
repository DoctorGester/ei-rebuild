// The spell line-of-sight test the world's spell casting asks per target.
#include "combat.h"

#include "ai_map.h"
#include "core_maths.h"
#include "item_db.h"
#include "world.h"

#include "lib/pc24.h"

#include <bit>

namespace {
constexpr float kAboveSurface = 1.0f;
static_assert(std::bit_cast<std::uint32_t>(kAboveSurface) == 0x3F800000u);
constexpr float kMinSight = 0.1f;
static_assert(std::bit_cast<std::uint32_t>(kMinSight) == 0x3DCCCCCDu);
} // namespace

bool SpellHasLineOfSight(WorldServer* world, CSpell* spell, CUnit* caster, const Vector3& from,
                                    CUnit* target, const Vector3& at) {
    const auto* record = spell->GetPrototype();
    if (!record->needsSight) return true;
    Vector3 to;
    if (!target) {
        to = at;
        to.z = pc24::Add(world->SurfaceHeightAt(to.x, to.y), kAboveSurface);   // the world's double plus a float
    } else {
        to = {target->localPosition.x, target->localPosition.y, target->localPosition.z};
        float ground = static_cast<float>(target->GroundHeight(to.x, to.y));
        to.z = target->ModeSpeed() + ground;
    }
    Vector3 eye = from;
    if (caster) {
        float ground = static_cast<float>(caster->GroundHeight(eye.x, eye.y));
        eye.z = caster->BaseSpeed() + ground;
    }
    // The unrounded result is compared (below the minimum, or NaN, fails).
    return world->aiMap.LineOfSight(eye, to) >= kMinSight;
}
