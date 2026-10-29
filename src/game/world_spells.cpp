// WorldServer: casting spells into the world (CastSpell) and the missile that carries one (CreateMissile). See
// world.h and docs/notes/world.md.
#include "object_diplomacy.h"
#include "world.h"
#include "combat.h"

#include "core_maths.h"
#include "map_object.h"
#include "player_messages.h"
#include "script.h"
#include "spell.h"
#include "unit.h"
#include "unit_ai.h"

#include <bit>
#include <cstring>
#include "lib/nan_audit.h"

namespace {

constexpr int kKindSinglePlayer = 1;
constexpr std::uint32_t kFarAway = 0x7E967699;           // 1e38f

// Whether the spell type turns the target against the caster.
bool IsHostileType(SpellType type) {
    switch (type) {
    case SpellType::Stench: case SpellType::Stun: case SpellType::Weaken: case SpellType::Feeblemind:
    case SpellType::Slow: case SpellType::Shrink:
        return true;
    default:
        return false;
    }
}

} // namespace

void WorldServer::CreateMissile(CSpell* spell, CUnit* caster, Vector3 from, CMapObject* target,
                                 const Vector3& to, int extra) {
    const Vector3& aim = target ? target->localPosition : to;
    Vector3 end;
    end = aim;
    if (spell->targetFlags & SpellTargetFlag::AtPoint) target = nullptr;
    end.z = static_cast<float>(RawSurfaceHeightAt(end.x, end.y) + 0.3f);
    if (caster) from = caster->localPosition;

    auto* missile = new SpellEffect();
    missile->refs = 0;    // the count and flags are cleared before the null test
    missile->flags6 = 0;
    missile->flags7 = 0;
    missile->Init(this, spell, caster, from, target, end, static_cast<std::uint8_t>(extra));
    if (missile->Update()) {
        projectiles.emplace_back(missile);
    }
    CUnit* victim = UnitOrNull(target);
    if (victim && caster) {
        if (IsHostileType(spell->Type())) {
            victim->ServerController()->brain.SetSeen(caster);
            diplomacy.OnAttack(caster, victim);
            target->flags |= MapObjectFlag::RelationsChanged;
            caster->flags |= MapObjectFlag::RelationsChanged;
        }
    }
}

void WorldServer::CastSpell(CSpell* spell, CUnit* caster, const Vector3& from, CMapObject* target,
                             const Vector3& to, int extra) {
    if (!spell) return;
    if (sessionKind != kKindSinglePlayer && caster) return;
    SpellType spellType = spell->Type();
    if (spellType == SpellType::Possession || spellType == SpellType::Link || spellType == SpellType::Charm) return;
    std::uint32_t targetFlags = spell->targetFlags;
    int targets = static_cast<int>(spell->targetCount);
    if (!(targetFlags & kSpellTargetRestrictions) && targets <= 1) {
        CreateMissile(spell, caster, from, target, to, extra);
        return;
    }

    // Several targets: the units around the caster (or `from`), the requested target first.
    PlayerServer* heroes = nullptr;   // the caster's player state: its party's units are friends
    if (caster) {
        if (CPlayer* ownerObject = caster->player) heroes = ownerObject->Server();
    }
    if ((targetFlags & SpellTargetFlag::AtPoint) && !(targetFlags & SpellTargetFlag::ItemContinuous)) {
        --targets;
        CreateMissile(spell, caster, from, target, to, extra);
    }
    Vector3 center;
    center = caster ? caster->localPosition : from;
    float radius = spell->range;
    std::vector<CUnit*> units = aiMap.sectorMap.FindUnitsInRadius(center, radius);
    if (CUnit* targetUnit = UnitOrNull(target)) {
        if (auto found = std::ranges::find(units, target); found != units.end()) units.erase(found);
        units.insert(units.begin(), targetUnit);
    }

    while (targets > 0) {
        if (units.empty()) break;
        CUnit* best = nullptr;
        std::ptrdiff_t bestIndex = 0;
        std::uint32_t bestBits = kFarAway;
        for (std::ptrdiff_t i = 0; i < std::ssize(units);) {
            CUnit* unit = units[i];
            if (unit->IsDead()) {
                units.erase(units.begin() + i);
                continue;
            } else if (unit == target) {
                best = unit;
                bestIndex = i;
                break;
            } else {
                double dz = static_cast<double>(to.z) - unit->localPosition.z;
                double dy = static_cast<double>(to.y) - unit->localPosition.y;
                double dx = static_cast<double>(to.x) - unit->localPosition.x;
                double distance = LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz);
                float bestDistance;
                bestDistance = std::bit_cast<float>(bestBits);
                EI_NAN_AUDIT(distance, bestDistance);
                if (distance < bestDistance) {
                    float stored = static_cast<float>(distance);
                    bestBits = std::bit_cast<std::uint32_t>(stored);
                    best = unit;
                    bestIndex = i;
                }
            }
            ++i;
        }
        if (!best) break;
        units.erase(units.begin() + bestIndex);

        bool hit;
        if (!caster) {
            hit = true;
        } else if (heroes) {
            hit = heroes->partyUnits.Contains(best) != 0;
        } else {
            // The units the caster's AI sees (its brain's visible list), else diplomacy says.
            if (lib::Contains(caster->ServerController()->brain.memory.visible, best)) {
                hit = true;
            } else {
                int casterSide = caster->side;
                int bestSide = best->side;
                hit = diplomacy.IsAllied(bestSide, casterSide);
            }
        }
        if (!hit) continue;
        Vector3* at = &best->localPosition;
        if (!SpellHasLineOfSight(this, spell, caster, from, best, *at))
            continue;
        if (!spell->CanAffect(caster, best)) continue;
        CreateMissile(spell, caster, from, best, *at, extra);
        --targets;
    }

    units.clear();
}
