// Spell effects hitting units: the helpers the effect state machines (casting, missiles, lasting effects)
// call every tick to deliver a spell's damage.
#include "spell.h"

#include "map_object.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include <bit>
#include <cstdint>
#include <cstring>

#include "lib/nan_audit.h"
#include "lib/float_select.h"

namespace {
constexpr float kTargetThreshold = 0.5f;
constexpr float kWallHalfWidth = 1.0f;

// The test of IsSpellTarget, inlined in the area loops.
bool AcceptsAreaSpells(Unit* unit) {
    if (unit->sizeClass != 0) return true;
    float value = unit->GetPrototype()->height;
    EI_NAN_AUDIT(value);
    return value < kTargetThreshold;
}

// Delivers `hit` to `unit` unless the spell may not affect it (the loops' common tail).
void Deliver(SpellEffect* effect, DamageInfo* hit, MapObject* unit) {
    if (effect->caster && !effect->HeldSpell()->CanAffect(effect->caster, unit)) return;
    Unit* attacker = (effect->flags & 1) ? nullptr : effect->caster.Get();
    unit->ReceiveHit(hit, attacker, effect->caster);
}
} // namespace

bool IsSpellTarget(Unit* unit) { return AcceptsAreaSpells(unit); }

void SpellEffect::HitUnit(DamageInfo* hit, MapObject* unit) { Deliver(this, hit, unit); }

void SpellEffect::HitUnitsInRadius() {
    DamageInfo hit;
    GetDamage(&hit);
    for (Unit* unit : world->aiMap.sectorMap.FindUnitsInRadius(position, HeldSpell()->radius)) {
        if (!AcceptsAreaSpells(unit)) continue;
        Deliver(this, &hit, unit);
    }
}

void SpellEffect::HitUnitsAlong(const Vector3& direction) {
    DamageInfo hit;
    GetDamage(&hit);

    // The wall's normal in the ground plane: s = 2 / sqrt(x^2 + y^2) (rounded to float), normal = (s * -y,
    // s * x, 0). The normal has length 2, so the test against 1 accepts the units within 0.5 of the plane.
    float x = direction.x;
    float minusY = -direction.y;
    float scale = 2.0f / std::sqrt(x * x + minusY * minusY);
    float normalX = scale * minusY;
    float normalY = scale * x;
    float normalZ = 0.0f;

    for (Unit* unit : world->aiMap.sectorMap.FindUnitsInRadius(position, HeldSpell()->radius)) {
        const Unit* at = unit;
        float dz = at->localPosition.z - position.z;
        float dy = at->localPosition.y - position.y;
        float dx = at->localPosition.x - position.x;
        float distance = DotUnrounded<Sum3::ZYX>(dx, dy, dz, normalX, normalY, normalZ);
        distance = AbsBits(distance);
        if (distance > kWallHalfWidth) continue;
        if (!AcceptsAreaSpells(unit)) continue;
        Deliver(this, &hit, unit);
    }
}
