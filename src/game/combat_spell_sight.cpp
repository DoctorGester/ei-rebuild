// The spell line-of-sight test the world's spell casting asks per target.
#include "game/combat.h"

#include "game/ai_map.h"
#include "game/core/core_maths.h"
#include "game/item_db.h"
#include "game/world.h"


#include <bit>

namespace {
constexpr float kAboveSurface = 1.0f;
constexpr float kMinSight = 0.1f;
} // namespace

bool SpellHasLineOfSight(WorldServer* world, Spell* spell, Unit* caster, const Vector3& from,
                                    Unit* target, const Vector3& at) {
    const auto* record = spell->GetPrototype();
    if (!record->needsSight) return true;
    Vector3 to;
    if (!target) {
        to = at;
        to.z = world->SurfaceHeightAt(to.x, to.y) + kAboveSurface;
    } else {
        to = {target->localPosition.x, target->localPosition.y, target->localPosition.z};
        float ground = target->GroundHeight(to.x, to.y);
        to.z = target->ModeSpeed() + ground;
    }
    Vector3 eye = from;
    if (caster) {
        float ground = caster->GroundHeight(eye.x, eye.y);
        eye.z = caster->BaseSpeed() + ground;
    }
    // Below the minimum, or NaN, fails.
    return world->aiMap.LineOfSight(eye, to) >= kMinSight;
}
