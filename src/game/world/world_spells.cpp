// WorldServer: casting spells into the world (CastSpell) and the missile that carries one (CreateMissile). See
// world.h and docs/notes/world.md.
#include "game/world/diplomacy.h"
#include "game/world/world.h"
#include "game/combat/combat.h"

#include "engine/core/core_maths.h"
#include "game/objects/map_object.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/script/script.h"
#include "game/spells/spell.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"

#include <algorithm>
#include <cstring>

namespace {

constexpr float kSpellSearchStart = 1e38f;

} // namespace

void WorldServer::CreateMissile(Spell* spell, Unit* caster, Vector3 from, MapObject* target,
                                 const Vector3& to, int extra) {
    const Vector3& aim = target ? target->localPosition : to;
    Vector3 end;
    end = aim;
    if (spell->targetFlags & SpellTargetFlag::AtPoint) target = nullptr;
    end.z = RawSurfaceHeightAt(end.x, end.y) + 0.3f;
    if (caster) from = caster->localPosition;

    auto* missile = new SpellEffect();
    missile->Init(this, spell, caster, from, target, end, static_cast<std::uint8_t>(extra));
    if (missile->Update()) {
        projectiles.emplace_back(missile);
    }
    Unit* victim = UnitOrNull(target);
    if (victim && caster) {
        if (spell->Use().turnsVictim) {
            victim->ServerController()->brain.SetSeen(caster);
            diplomacy.OnAttack(caster, victim);
            target->flags |= MapObjectFlag::RelationsChanged;
            caster->flags |= MapObjectFlag::RelationsChanged;
        }
    }
}

void WorldServer::CastSpell(Spell* spell, Unit* caster, const Vector3& from, MapObject* target,
                             const Vector3& to, int extra) {
    if (!spell) return;
    if (zoneMode != ZoneMode::Battle && caster) return;
    SpellType spellType = spell->Behaviour();
    if (spellType == SpellType::Possession || spellType == SpellType::Link || spellType == SpellType::Charm) return;
    // A scripted spell is one cast whatever its target count and filters (its script picks the targets), unless its
    // record says per_target.
    if (spellType == SpellType::Scripted && !spell->GetPrototype()->perTarget) {
        CreateMissile(spell, caster, from, target, to, extra);
        return;
    }
    std::uint32_t targetFlags = spell->targetFlags;
    int targets = static_cast<int>(spell->targetCount);
    if (!(targetFlags & kSpellTargetRestrictions) && targets <= 1) {
        CreateMissile(spell, caster, from, target, to, extra);
        return;
    }

    // Several targets: the units around the caster (or `from`), the requested target first.
    PlayerServer* heroes = nullptr;   // the caster's player state: its party's units are friends
    if (caster) {
        if (Player* ownerObject = caster->player) heroes = ownerObject->Server();
    }
    if ((targetFlags & SpellTargetFlag::AtPoint) && !(targetFlags & SpellTargetFlag::ItemContinuous)) {
        --targets;
        CreateMissile(spell, caster, from, target, to, extra);
    }
    Vector3 center;
    center = caster ? caster->localPosition : from;
    float radius = spell->range;
    std::vector<Unit*> units = aiMap.sectorMap.FindUnitsInRadius(center, radius);
    if (Unit* targetUnit = UnitOrNull(target)) {
        if (auto found = std::ranges::find(units, target); found != units.end()) units.erase(found);
        units.insert(units.begin(), targetUnit);
    }

    while (targets > 0) {
        if (units.empty()) break;
        Unit* best = nullptr;
        std::ptrdiff_t bestIndex = 0;
        float bestDistance = kSpellSearchStart;
        for (std::ptrdiff_t i = 0; i < std::ssize(units);) {
            Unit* unit = units[i];
            if (unit->IsDead()) {
                units.erase(units.begin() + i);
                continue;
            } else if (unit == target) {
                best = unit;
                bestIndex = i;
                break;
            } else {
                float distance = LengthSquared(to - unit->localPosition);
                if (distance < bestDistance) {
                    bestDistance = distance;
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
