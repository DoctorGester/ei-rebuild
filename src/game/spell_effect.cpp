// Spell effects hitting units: the helpers the effect state machines (casting, missiles, lasting effects)
// call every tick to deliver a spell's damage.
#include "spell.h"

#include "map_object.h"
#include "sector_map.h"
#include "unit.h"
#include "unit_db.h"
#include "world.h"

#include "ei/x87.h"
#include "lib/mfc.h"

#include <cstring>
#include <list>
#include "game_data.h"
#include "lib/nan_audit.h"

namespace {
constexpr float kTargetThreshold = 0.5f;
constexpr float kWallHalfWidth = 1.0f;

using UnitList = std::list<CUnit*>;

// CSectorMap::FindUnitsInRadius (sector_map.cpp).
CSectorMap* SectorMapOf(const CSpellEffect* effect) { return effect->world->Sectors(); }

// The test of IsSpellTarget, inlined in the area loops.
bool AcceptsAreaSpells(CUnit* unit) {
    if (unit->sizeClass != 0) return true;
    float value = std::bit_cast<float>(unit->GetPrototype()->height);
    EI_NAN_AUDIT(value);
    return value < kTargetThreshold;
}

// Delivers `hit` to `unit` unless the spell may not affect it (the loops' common tail).
void Deliver(CSpellEffect* effect, DamageInfo* hit, CMapObject* unit) {
    if (effect->caster && !effect->Spell()->CanAffect(effect->caster, unit)) return;
    CUnit* attacker = (effect->flags & 1) ? nullptr : effect->caster.object;
    unit->ReceiveHit(hit, attacker, effect->caster);
}
} // namespace

int IsSpellTarget(CUnit* unit) { return AcceptsAreaSpells(unit) ? 1 : 0; }

void CSpellEffect::HitUnit(DamageInfo* hit, CMapObject* unit) { Deliver(this, hit, unit); }

void CSpellEffect::HitUnitsInRadius() {
    UnitList list;
    DamageInfo hit;
    GetDamage(&hit);
    SectorMapOf(this)->FindUnitsInRadius(&position, &list, Spell()->radius_);
    for (auto node = list.begin(); node != list.end();) {
        CUnit* unit = *node;
        ++node;
        if (!AcceptsAreaSpells(unit)) continue;
        Deliver(this, &hit, unit);
    }
}

void CSpellEffect::HitUnitsAlong(const Vector3* direction) {
    UnitList list;
    DamageInfo hit;
    GetDamage(&hit);

    // The wall's normal in the ground plane: s = 2 / sqrt(x^2 + y^2) (rounded to float), normal = (s * -y,
    // s * x, 0). The normal has length 2, so the test against 1 accepts the units within 0.5 of the plane.
    float x = direction->x;
    double minusY = -direction->y;
    float scale = static_cast<float>(2.0f / std::sqrt(x * x + minusY * minusY));
    float normalX = static_cast<float>(scale * minusY);
    float normalY = scale * x;
    float normalZ = 0.0f;

    SectorMapOf(this)->FindUnitsInRadius(&position, &list, Spell()->radius_);
    for (auto node = list.begin(); node != list.end();) {
        CUnit* unit = *node;
        ++node;
        const CUnit* at = unit;
        double dz = at->localPosition_.z - position.z;
        double dy = at->localPosition_.y - position.y;
        double dx = at->localPosition_.x - position.x;
        float distance = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(dx, dy, dz, normalX, normalY, normalZ));
        std::uint32_t bits;
        std::memcpy(&bits, &distance, sizeof(bits));
        bits &= 0x7FFFFFFF;
        std::memcpy(&distance, &bits, sizeof(bits));
        if (distance > kWallHalfWidth) continue;
        if (!AcceptsAreaSpells(unit)) continue;
        Deliver(this, &hit, unit);
    }
}
