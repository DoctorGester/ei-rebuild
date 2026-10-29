// CWorldServer: casting spells into the world (CastSpell) and the missile that carries one (CreateMissile). See
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
#include <list>
#include "lib/nan_audit.h"

namespace {

constexpr int kKindSinglePlayer = 1;
constexpr std::uint32_t kSpellAreaFlag = 0x20000000;     // targetFlags_: also hits the target itself
constexpr std::uint32_t kSpellNoTargetFlag = 0x20000;
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

void CWorldServer::CreateMissile(CSpell* spell, CUnit* caster, Vector3 from, CMapObject* target,
                                 const Vector3* to, int extra) {
    const Vector3* aim = target ? &target->localPosition_ : to;
    Vector3 end;
    end = *aim;
    if (spell->targetFlags_ & kSpellAreaFlag) target = nullptr;
    end.z = static_cast<float>(RawSurfaceHeightAt(end.x, end.y) + 0.3f);
    if (caster) from = caster->localPosition_;

    auto* missile = new CSpellEffect();
    missile->refs_ = 0;   // the count and flags are cleared before the null test
    missile->flags6_ = 0;
    missile->flags7_ = 0;
    missile->Init(this, spell, caster, &from, target, &end, static_cast<std::uint8_t>(extra));
    if (missile->Update()) {
        projectiles_.emplace_back(missile);
    }
    CUnit* victim = UnitOrNull(target);
    if (victim && caster) {
        if (IsHostileType(spell->Type())) {
            victim->ServerController()->brain.SetSeen(caster);
            diplomacy_.OnAttack(caster, victim);
            target->flags_ |= MapObjectFlag::RelationsChanged;
            caster->flags_ |= MapObjectFlag::RelationsChanged;
        }
    }
}

void CWorldServer::CastSpell(CSpell* spell, CUnit* caster, const Vector3* from, CMapObject* target,
                             const Vector3* to, int extra) {
    if (!spell) return;
    if (sessionKind_ != kKindSinglePlayer && caster) return;
    SpellType type = spell->Type();
    if (type == SpellType::Possession || type == SpellType::Link || type == SpellType::Charm) return;
    std::uint32_t flags = spell->targetFlags_;
    int targets = static_cast<int>(spell->targetCount_);
    if (!(flags & 0xFFFF) && targets <= 1) {
        CreateMissile(spell, caster, *from, target, to, extra);
        return;
    }

    // Several targets: the units around the caster (or `from`), the requested target first.
    CPlayerServer* heroes = nullptr;   // the caster's player state: its party's units are friends
    std::list<CUnit*> units;
    if (caster) {
        if (CPlayer* owner = caster->player) heroes = owner->Server();
    }
    if ((flags & kSpellAreaFlag) && !(flags & kSpellNoTargetFlag)) {
        --targets;
        CreateMissile(spell, caster, *from, target, to, extra);
    }
    Vector3 center;
    center = *(caster ? &caster->localPosition_ : from);
    float radius = spell->range_;
    aiMap_.sectorMap.FindUnitsInRadius(&center, &units, radius);
    if (CUnit* targetUnit = UnitOrNull(target)) {
        if (auto found = std::ranges::find(units, target); found != units.end()) units.erase(found);
        units.push_front(targetUnit);
    }

    while (targets > 0) {
        if (units.empty()) break;
        CUnit* best = nullptr;
        std::list<CUnit*>::iterator bestNode{};
        std::uint32_t bestBits = kFarAway;
        for (auto node = units.begin(); node != units.end();) {
            auto next = std::next(node);
            CUnit* unit = *node;
            if (unit->IsDead()) {
                units.erase(node);
            } else if (unit == target) {
                best = unit;
                bestNode = node;
                break;
            } else {
                double dz = static_cast<double>(to->z) - unit->localPosition_.z;
                double dy = static_cast<double>(to->y) - unit->localPosition_.y;
                double dx = static_cast<double>(to->x) - unit->localPosition_.x;
                double distance = LengthSquaredUnrounded<Sum3::XYZ>(dx, dy, dz);
                float bestDistance;
                bestDistance = std::bit_cast<float>(bestBits);
                EI_NAN_AUDIT(distance, bestDistance);
                if (distance < bestDistance) {
                    float stored = static_cast<float>(distance);
                    bestBits = std::bit_cast<std::uint32_t>(stored);
                    best = unit;
                    bestNode = node;
                }
            }
            node = next;
        }
        if (!best) break;
        units.erase(bestNode);

        bool hit;
        if (!caster) {
            hit = true;
        } else if (heroes) {
            hit = heroes->partyUnits_.Contains(best) != 0;
        } else {
            // The units the caster's AI sees (its brain's visible list), else diplomacy says.
            if (lib::Contains(caster->ServerController()->brain.memory.visible, best)) {
                hit = true;
            } else {
                int casterSide = caster->side;
                int bestSide = best->side;
                hit = diplomacy_.IsAllied(bestSide, casterSide);
            }
        }
        if (!hit) continue;
        Vector3* at = &best->localPosition_;
        if (!SpellHasLineOfSight(this, spell, caster, from, best, at))
            continue;
        if (!spell->CanAffect(caster, best)) continue;
        CreateMissile(spell, caster, *from, best, at, extra);
        --targets;
    }

    units.clear();
}
