// A spell effect's later states: the per-tick dispatch, the missiles' flight and impact (state 1) and the
// lasting effects (state 2), plus the release helpers. See docs/notes/items_spells.md.
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

constexpr float kExplosionLoudness = 5.0f;             // the fireball's noise
constexpr int kEndedTimer = -80;                       // the effects linger this long after ending

// The timer counts one tick down; the value it had.
int Tick(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    return ticks;
}

// --- State 1 -------------------------------------------------------------------------------------

// FireArrow / RickMagic (the fire arrow) and AcidRay: the effect follows its missile; when the missile
// reports a hit the target takes the damage, the missile, sound and light go, and a
// splash, an end sound and a rising light appear at the target's height.
void MissileImpact(SpellEffect* e, ParticleType particleType, const char* sound) {
    auto* missile = e->Visual<EffectMissile>();
    if (!missile) return;
    e->position = missile->localPosition;
    if (missile->arrived == 0) return;

    e->HitTarget();
    e->ReleaseVisual();
    e->ReleaseSound();
    e->ReleaseLight();

    // The splash, the sound and the light at the target, a unit (the fire arrow and the acid ray are cast at
    // units); without one the effect ends.
    Unit* target = UnitOrNull(e->target);
    if (!target) {
        e->state = kStateEnded;
        return;
    }
    e->targetPoint = target->localPosition;
    Vector3 at;
    at = target->localPosition;
    at.z = target->ModeSpeed();
    at.z = e->world->GridHeightAt(e->targetPoint.x, e->targetPoint.y) + at.z;
    float size = Power(e) * kSplashScale;
    e->CreateParticle(0, particleType, kZeroVector, size, 1.0f, kZeroVector, e->target);
    e->CreateSound(sound, at, kZeroVector, kVolume, kHearing);

    Vector3 velocity{0.0f, 0.0f, LightRise(e) * kRiseScale};
    float range = Prototype(e)->lightRange;
    Vector3 color = Prototype(e)->lightColor;
    e->CreatePointLight(at, color, range * kRiseScale, velocity);
    e->timer = 35;
    e->state = kStateLasting;
}

// Fireball: the ball explodes when its timer runs out: everything in the radius is hit, a blast
// (the larger of the area curve and the power), an end sound, a rising light and, with a unit
// caster, a noise the AI hears. The missile's particle goes once the timer is below zero.
void FireballFlight(SpellEffect* e) {
    if (Tick(e) < 0) {
        e->position = e->targetPoint;
        e->HitUnitsInRadius();
        e->ReleaseVisual();
        e->ReleaseSound();
        e->ReleaseLight();
        Vector3 at = e->targetPoint;
        float power = Power(e);
        float curve = AreaCurve(e->HeldSpell()->radius, Prototype(e)->area);
        float size = MaxKeepB(curve, power);
        EffectParticle* blast =
            e->CreateParticle(0, ParticleType::FireBlast, at, size, 1.0f, kZeroVector, nullptr);
        float radius = e->HeldSpell()->radius;
        float alpha = Power(e);
        blast->SetColors(alpha, radius);
        e->CreateSound("magic\\fireball\\end.wav", at, kZeroVector, kVolume, kHearing);
        Vector3 velocity{0.0f, 0.0f, LightRise(e) * kRiseScale};
        float range = Prototype(e)->lightRange;
        Vector3 color = Prototype(e)->lightColor;
        e->CreatePointLight(at, color, range * kRiseScale, velocity);
        if (IsUnit(e->caster)) {
            e->world->aiMap.events.Noise(ControllerOf(e->caster), kExplosionLoudness, kExplosionNoiseDelay,
                                         e->targetPoint);
        }
        e->timer = 35;
        e->state = kStateLasting;
    }
    if (e->timer < 0) e->ReleaseVisual();
}

// --- State 2 -------------------------------------------------------------------------------------

// The common count-down: ends the effect once the timer was already below zero.
void CountDown(SpellEffect* e) {
    if (Tick(e) < 0) e->state = kStateEnded;
}

// After the walls and clouds have ended: the timer keeps running down and the effect is removed
// 80 ticks later.
void Linger(SpellEffect* e) {
    if (Tick(e) < kEndedTimer) e->state = kStateEnded;
}

void RiseLight(SpellEffect* e) {
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    if (e->light) e->light->SetVelocity(rise);
}

// Lightning / CurseMagic: the bolt and sound go when the timer reaches 0, the light rises away.
void LightningFade(SpellEffect* e) {
    if (e->timer == 0) {
        e->ReleaseVisual();
        e->ReleaseSound();
        RiseLight(e);
    }
    Linger(e);
}

// InvokeLightning: the light strike's bolt is dropped every tick; at timer 5 a second bolt strikes from 30 above.
void LightStrikeEcho(SpellEffect* e) {
    e->ReleaseVisual();
    if (e->timer == 5) {
        Vector3 top = e->position;
        top.z += kStrikeHeight;
        auto* bolt = CreateObject<EffectLightning>(e, ClassId::EffectLightning);
        bolt->SetEnds(top, e->position, kStrikeWidth);
        e->SetVisual(bolt);
    }
    CountDown(e);
}

// AcidColumn: the column hits once, at timer 15.
void AcidColumn(SpellEffect* e) {
    CountDown(e);
    if (e->timer == 15) e->HitUnitsInRadius();
}

// The walls' and clouds' lasting tick: every 4th tick while the timer is not below zero `pulse` hits, then the
// timer runs down; at -1 `end` takes the effect's objects away (each spell in its own order: the timed objects'
// order is state); then Linger.
void PulseUntilEnd(SpellEffect* e, void (*pulse)(SpellEffect*), void (*end)(SpellEffect*)) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0) pulse(e);
        e->timer -= 1;
        return;
    }
    if (ticks == -1) end(e);
    Linger(e);
}

// FireWall: the wall burns along its direction every 4th tick; at -1 it goes out.
void FireWallPulse(SpellEffect* e) {
    if (!e->visual) return;
    e->HitUnitsAlong(e->Visual<EffectWall>()->direction);
    e->flags |= 1;
}

void FireWallEnd(SpellEffect* e) {
    Vector3 at = e->position;
    if (e->sound) at = e->sound->localPosition;
    e->UpdateFootprint(SpellType::FireWall, 0);
    e->ReleaseSound();
    e->ReleaseVisual();
    e->CreateSound("magic\\Firewall\\end.wav", at, kZeroVector, kVolume, kHearing);
    RiseLight(e);
}

void FireWall(SpellEffect* e) { PulseUntilEnd(e, FireWallPulse, FireWallEnd); }

// LightningWall: the light wall hits along its bolt every 4th tick; at -1 it goes out.
void LightWallPulse(SpellEffect* e) {
    if (!e->visual) return;
    auto* bolt = e->Visual<EffectLightning>();
    Vector3 along = bolt->to - bolt->from;
    e->HitUnitsAlong(along);
    e->flags |= 1;
}

void LightWallEnd(SpellEffect* e) {
    RiseLight(e);
    e->UpdateFootprint(SpellType::LightningWall, 0);
    e->ReleaseSound();
    e->ReleaseVisual();
}

void LightWall(SpellEffect* e) { PulseUntilEnd(e, LightWallPulse, LightWallEnd); }

// AcidFog: the poison cloud hits every 4th tick; at -1 it goes.
void PoisonCloudPulse(SpellEffect* e) {
    e->HitUnitsInRadius();
    e->flags |= 1;
}

void PoisonCloudEnd(SpellEffect* e) {
    e->CreateSound("magic\\poison\\end.wav", e->targetPoint, kZeroVector, kVolume, kHearing);
    RiseLight(e);
    e->UpdateFootprint(SpellType::AcidFog, 0);
    e->ReleaseSound();
    e->ReleaseVisual();
}

void PoisonCloud(SpellEffect* e) { PulseUntilEnd(e, PoisonCloudPulse, PoisonCloudEnd); }

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
    if (!e->world->aiMap.IsPassableWithoutSelf(e->caster, e->targetPoint)) {
        if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(AckKind::CantTeleport);
        return;
    }
    e->world->aiMap.sectorMap.Move(e->caster, e->targetPoint);
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
    case kStateMissile: UpdateMissile(type); return 1;
    case kStateLasting: UpdateLasting(type); return 1;
    case kStateScripted:
        if (!host || !host->Tick(*this)) state = kStateEnded;
        return 1;
    default:
        UpdateFootprint(type, 0);
        ReleaseVisual();
        ReleaseSound();
        ReleaseLight();
        return 0;
    }
}

void SpellEffect::UpdateMissile(SpellType type) {
    switch (type) {
    case SpellType::FireArrow:
    case SpellType::RickMagic:
        MissileImpact(this, ParticleType::FireStar, "magic\\Firearrow\\end.wav");
        CountDown(this);
        break;
    case SpellType::AcidRay:
        MissileImpact(this, ParticleType::AcidStar, "magic\\Acidray\\end.wav");
        CountDown(this);
        break;
    case SpellType::Fireball: FireballFlight(this); break;
    default: break;
    }
}

void SpellEffect::UpdateLasting(SpellType type) {
    switch (type) {
    case SpellType::FireArrow:
    case SpellType::AcidRay:
    case SpellType::Fireball:
    case SpellType::Healing:
    case SpellType::RickMagic: CountDown(this); break;
    case SpellType::Lightning:
    case SpellType::CurseMagic: LightningFade(this); break;
    case SpellType::InvokeLightning: LightStrikeEcho(this); break;
    case SpellType::AcidColumn: AcidColumn(this); break;
    case SpellType::FireWall: FireWall(this); break;
    case SpellType::LightningWall: LightWall(this); break;
    case SpellType::AcidFog: PoisonCloud(this); break;
    case SpellType::Fireworks: Fireworks(this); break;
    case SpellType::Clairvoyance: Clairvoyance(this); break;
    case SpellType::Possession: Possession(this); break;
    case SpellType::VisionFog: VisionFog(this); break;
    case SpellType::Link: Link(this); break;
    case SpellType::Teleport: Teleport(this); break;
    case SpellType::Campfire: CountingCloud(this); break;
    default: break;
    }
}

void SpellEffect::ReleaseVisual() { Hold(this, visual, nullptr); }

void SpellEffect::ReleaseSound() { Hold(this, sound, nullptr); }

void SpellEffect::ReleaseLight() { Hold(this, light, nullptr); }
