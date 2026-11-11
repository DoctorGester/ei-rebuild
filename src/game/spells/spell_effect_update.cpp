// A spell effect's later states: the per-tick dispatch and the lasting effects (state 2), plus the release helpers. See docs/notes/items_spells.md.
#include "game/ai/ai_map.h"
#include "game/objects/map_object.h"
#include "game/script/script.h"
#include "game/script/script_spell_host.h"
#include "game/spells/spell.h"
#include "game/spells/spell_effect_internal.h"
#include "game/combat/combat.h"
#include "game/players/player.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/effects/world_effect.h"
#include "lib/float_select.h"


using namespace spellfx;

namespace {

constexpr int kEndedTimer = -80;                       // the fireworks' light lingers this long after they end

// The timer counts one tick down; the value it had.
int Tick(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    return ticks;
}

// --- State 2 -------------------------------------------------------------------------------------

// The common count-down: ends the effect once the timer was already below zero.
void CountDown(SpellEffect* e) {
    if (Tick(e) < 0) e->state = kStateEnded;
}

void RiseLight(SpellEffect* e) {
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    if (e->light) e->light->SetVelocity(rise);
}

// Campfire: hits every 4th tick, counting up, forever (until something else ends it).
void CountingCloud(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks + 1;
    if ((ticks & 3) == 0) e->HitUnitsInRadius();
}

// Fireworks wake the units around and frighten those in the radius (their AI memory notices
// the point at a level falling from 300 to 50 over the duration); at -1 they end.
void Fireworks(SpellEffect* e) {
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks >= 0) {
        float wake = e->HeldSpell()->radius + kImpactWakeMargin;
        e->world->WakeUnitsAround(e->position, wake);
        for (Unit* unit : e->world->aiMap.sectorMap.FindUnitsInRadius(e->position, e->HeldSpell()->radius)) {
            int duration = static_cast<int>(e->HeldSpell()->duration);
            int level = FrightLevel(e->timer, duration);
            ControllerOf(unit)->brain.memory.Notice(e->position, level, -1);
        }
        return;
    }
    if (ticks == -1) {
        e->CreateSound("magic\\Firework\\end.wav", e->position, kZeroVector, kVolume, kHearing);
        e->ReleaseVisual();
        e->ReleaseSound();
        RiseLight(e);
    }
    if (e->timer < kEndedTimer) e->state = kStateEnded;
}

// Clairvoyance shows the caster's player every unit in the radius and wakes them; at timer
// 10 a closing particle and sound. Ends with the timer or without a caster.
void Clairvoyance(SpellEffect* e) {
    if (IsUnit(e->caster)) {
        auto* player = e->caster->player;
        if (player) {
            ScriptGroup* group = &player->Server()->possessedGroup;
            for (Unit* unit : e->world->aiMap.sectorMap.FindUnitsInRadius(e->position, e->HeldSpell()->radius))
                group->Insert(unit);
        }
        float wake = e->HeldSpell()->radius + kImpactWakeMargin;
        e->world->WakeUnitsAround(e->position, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0 || !e->caster) e->state = kStateEnded;
    if (ticks == 10) {
        e->CreateParticle(0, ParticleType::ClayRing, e->targetPoint, -e->HeldSpell()->radius, 1.0f, kZeroVector,
                          nullptr);
        e->CreateSound("magic\\Clairvoyence\\end.wav", e->position, kZeroVector, kVolume, kHearing);
    }
}

// Possession: the caster's player sees the target, its controller's two lists and its id;
// the target is flagged (0x2000000) and the units around it wake.
// Unreached in the shipped game: CastSpell refuses possession, only a saved projectile of that type gets here.
void Possession(SpellEffect* e) {
    if (!IsUnit(e->caster)) {
        e->state = kStateEnded;
        return;
    }
    auto* player = e->caster->player;
    if (player && IsUnit(e->target)) {
        MapObject* target = e->target;
        UnitServer* server = ControllerOf(UnitOrNull(target));
        ScriptGroup* group = &player->Server()->possessedGroup;
        group->Insert(target);
        group->InsertAll(server->brain.memory.visible);
        group->InsertAll(server->brain.memory.seenCorpses);
        player->Server()->possessedId = target->id;
        target->flags |= MapObjectFlag::SeeAll;
        float sight = server->moveFactors[MoveFactor::Sight] * server->stats.perception[Sense::Sight].current;
        float floor = server->stats.perception[Sense::Near].current;
        float reach = MaxKeepB(sight, floor);
        float wake = reach * kPossessWake + kPossessWakeBase;
        e->world->WakeUnitsAround(target->localPosition, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0) e->state = kStateEnded;
}

// VisionFog lasts while its target is free.
void VisionFog(SpellEffect* e) {
    CountDown(e);
    if (!e->target || e->target->IsDead() != 0) e->state = kStateEnded;
}

// Link: the link lasts while both units are free; it ends with a sound (and a complaint from the
// caster when a unit got busy).
// Unreached in the shipped game: CastSpell refuses the link, only a saved projectile of that type gets here.
void Link(SpellEffect* e) {
    bool free = e->target && e->caster && e->target->IsLargeUnit() == 0 &&
                e->caster->IsLargeUnit() == 0 && e->target->IsDead() == 0 &&
                e->caster->IsDead() == 0;
    if (free) {
        int ticks = e->timer - 1;
        e->timer = ticks;
        if (ticks >= 0) return;
        e->CreateSound("magic\\Link\\end.wav", e->position, kZeroVector, kVolume, kHearing);
    } else {
        e->CreateSound("magic\\Link\\end.wav", e->position, kZeroVector, kVolume, kHearing);
        if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(AckKind::LinkEnded);
    }
    e->state = kStateEnded;
}

// Teleport: when the timer comes down to the spell's duration (at least 1) the caster moves
// to the target point at its own height, if it fits there (else it complains, CantTeleport); then it gets
// the spell's buff record and its controller re-places it.
void Teleport(SpellEffect* e) {
    if (Tick(e) < 0 || !e->caster || e->caster->IsDead() != 0) {
        e->state = kStateEnded;
        return;
    }
    int arrival = static_cast<int>(e->HeldSpell()->duration);
    if (arrival < 1) arrival = 1;
    if (e->timer != arrival) return;
    e->targetPoint.z = e->caster->localPosition.z;
    if (!e->MoveUnit(e->caster, e->targetPoint, true)) {
        if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(AckKind::CantTeleport);
        return;
    }
    e->ReleaseVisual();
    e->ReleaseSound();
    if (!IsUnit(e->caster)) return;
    MapEffect record = e->HeldSpell()->EffectRecord();
    e->caster->AddEffect(&record);
    ControllerOf(e->caster)->ResyncWithUnit();
    RiseLight(e);
}

} // namespace

int SpellEffect::Update() {
    if (!world) return 0;
    SpellType type = HeldSpell()->Behaviour();
    switch (state) {
    case kStateCasting: Start(type); return 1;
    case kStateMissile: state = kStateEnded; return 1;   // a save's cast of a spell that runs on its script now
    case kStateLasting: UpdateLasting(type); return 1;
    case kStateScripted:
        if (!host || !host->Tick(*this)) state = kStateEnded;
        return 1;
    default:
        if (footprintStamped) Unblock();
        ReleaseOwned();
        ReleaseVisual();
        ReleaseSound();
        ReleaseLight();
        return 0;
    }
}

void SpellEffect::UpdateLasting(SpellType type) {
    switch (type) {
    case SpellType::Healing: CountDown(this); break;
    case SpellType::Fireworks: Fireworks(this); break;
    case SpellType::Clairvoyance: Clairvoyance(this); break;
    case SpellType::Possession: Possession(this); break;
    case SpellType::VisionFog: VisionFog(this); break;
    case SpellType::Link: Link(this); break;
    case SpellType::Teleport: Teleport(this); break;
    case SpellType::Campfire: CountingCloud(this); break;
    default: state = kStateEnded; break;   // a save's cast of a spell that runs on its script now
    }
}

void SpellEffect::Own(Effect* object, int ticks, int payload) {
    world->AddTimedObject(object);
    SpellOwned& entry = owned.emplace_back();
    entry.object.Assign(object);
    entry.ticks = ticks;
    entry.payload = payload;
}

void SpellEffect::TickOwned(std::vector<SpellArrival>* arrived) {
    for (auto entry = owned.begin(); entry != owned.end();) {
        Effect* object = entry->object;
        bool goes = false;
        if (entry->payload) {
            const auto* missile = static_cast<EffectMissile*>(object);
            if (missile->arrived) {
                arrived->push_back({entry->payload, missile->targetId, missile->localPosition});
                goes = true;
            }
        } else if (entry->ticks > 0) {
            goes = --entry->ticks == 0;
        }
        if (goes) {
            world->RemoveTimedObject(static_cast<unsigned>(object->id));
            entry = owned.erase(entry);
        } else if (!world->timedObjects.map.contains(static_cast<unsigned>(object->id))) {
            entry = owned.erase(entry);   // it ended on its own (a one-shot particle)
        } else {
            ++entry;
        }
    }
}

bool SpellEffect::OwnsPending() const {
    for (const SpellOwned& entry : owned)
        if (entry.ticks > 0 || entry.payload) return true;
    return false;
}

void SpellEffect::ReleaseOwned() {
    for (const SpellOwned& entry : owned) world->RemoveTimedObject(static_cast<unsigned>(entry.object->id));
    owned.clear();
}

void SpellEffect::ReleaseVisual() { Hold(this, visual, nullptr); }

void SpellEffect::ReleaseSound() { Hold(this, sound, nullptr); }

void SpellEffect::ReleaseLight() { Hold(this, light, nullptr); }
