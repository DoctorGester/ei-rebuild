// A spell effect's later states: the per-tick dispatch, the missiles' flight and impact (state 1) and the
// lasting effects (state 2), plus the release helpers. See docs/notes/items_spells.md.
#include "ai_map.h"
#include "map_object.h"
#include "script.h"
#include "spell.h"
#include "spell_effect_internal.h"
#include "combat.h"
#include "player_messages.h"
#include "unit.h"
#include "unit_ai.h"
#include "world_effect.h"

#include <list>

using namespace spellfx;

namespace {

constexpr float kExplosionLoudness = 5.0f;             // immediate
constexpr float kStrikeWidthImmediate = -7.0f;         // immediate in the lasting strike
constexpr int kEndedTimer = -80;                       // the effects linger this long after ending

// --- State 1 -------------------------------------------------------------------------------------

// FireArrow / RickMagic (the fire arrow) and AcidRay: the effect follows its missile; when the missile
// reports a hit the target takes the damage, the missile, sound and light go, and a
// splash, an end sound and a rising light appear at the target's height. The two spells compute
// the light's rise differently (the arrow rounds the quotient before scaling it).
void MissileImpact(SpellEffect* e, int particleType, const char* sound, bool arrow) {
    auto* missile = e->Visual<EffectMissile>();
    if (!missile) return;
    e->position = missile->localPosition;
    if (missile->arrived == 0) return;

    DamageInfo hit;
    e->GetDamage(&hit);
    if (IsUnit(e->target)) e->HitUnit(&hit, e->target);
    e->ReleaseVisual();
    e->ReleaseSound();
    e->ReleaseLight();

    CUnit* target = TargetUnit(e);   // no null check
    e->targetPoint = target->localPosition;
    Vector3 at;
    at = target->localPosition;
    at.z = static_cast<float>(target->ModeSpeed());
    at.z = GroundPlus(e, kGridHeight, e->targetPoint, &at.z);
    float size = PowerScaled(e, worlddata::SplashScale());
    e->CreateParticle(0, particleType, kZeroVector, size, 1.0f, kZeroVector, e->target);
    e->CreateSound(sound, at, kZeroVector, kVolume, kHearing);

    Vector3 velocity;
    if (arrow) {
        Vector3 rise{0.0f, 0.0f, LightRise(e)};
        velocity.x = rise.x * ConstAt(worlddata::RiseScale());
        velocity.y = rise.y * ConstAt(worlddata::RiseScale());
        velocity.z = rise.z * ConstAt(worlddata::RiseScale());
    } else {
        velocity.x = 0.0f;
        velocity.y = 0.0f;
        velocity.z = LightRiseScaled(e, worlddata::RiseScale());
    }
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(at, color, range * ConstAt(worlddata::RiseScale()), velocity);
    e->timer = 0x23;
    e->state = 2;
}

// Fireball: the ball explodes when its timer runs out: everything in the radius is hit, a blast
// (the larger of the area curve and the power), an end sound, a rising light and, with a unit
// caster, a noise the AI hears. The missile's particle goes once the timer is below zero.
void FireballFlight(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0) {
        e->position = e->targetPoint;
        e->HitUnitsInRadius();
        e->ReleaseVisual();
        e->ReleaseSound();
        e->ReleaseLight();
        Vector3 at = e->targetPoint;
        float power = Power(e);
        float curve;
        AreaCurve(&e->Spell()->radius, &Prototype(e)->area, &curve);
        float size = curve >= power ? curve : power;
        CEffectParticle* blast = e->CreateParticle(0, static_cast<int>(ParticleType::FireBlast), at, size, 1.0f, kZeroVector, nullptr);
        float radius = e->Spell()->radius;
        float alpha = Power(e);
        blast->SetColors(alpha, radius);
        e->CreateSound("magic\\fireball\\end.wav", at, kZeroVector, kVolume, kHearing);
        Vector3 velocity{0.0f, 0.0f, LightRiseScaled(e, worlddata::RiseScale())};
        float range = PrototypeRange(e);
        Vector3 color = PrototypeColor(e);
        e->CreatePointLight(at, color, range * ConstAt(worlddata::RiseScale()), velocity);
        if (IsUnit(e->caster)) {
            e->world->aiMap.events.Noise(ControllerOf(e->caster), kExplosionLoudness, FtolOf(ConstPtr(worlddata::NoiseKind())), e->targetPoint);
        }
        e->timer = 0x23;
        e->state = 2;
    }
    if (e->timer < 0) e->ReleaseVisual();
}

// --- State 2 -------------------------------------------------------------------------------------

// The common count-down: ends the effect once the timer was already below zero.
void CountDown(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0) e->state = 3;
}

// After the walls and clouds have ended: the timer keeps running down and the effect is removed
// 80 ticks later.
void Linger(SpellEffect* e) {
    int ticks = e->timer;
    if (ticks < kEndedTimer) e->state = 3;
    e->timer = ticks - 1;
}

void RiseLight(SpellEffect* e) {
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    e->light->SetVelocity(rise);   // no null check
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
        top.z = top.z + ConstAt(worlddata::StrikeHeight());
        auto* bolt = CreateObject<EffectLightning>(e, ClassId::EffectLightning);
        bolt->SetEnds(top, e->position, kStrikeWidthImmediate);
        e->SetVisual(bolt);
    }
    CountDown(e);
}

// AcidColumn: the column hits once, at timer 15.
void AcidColumn(SpellEffect* e) {
    CountDown(e);
    if (e->timer == 15) HitAreaTargets(e);
}

// FireWall: the wall burns along its direction every 4th tick; at -1 it goes out.
void FireWall(SpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0 && e->visual) {
            e->HitUnitsAlong(e->Visual<EffectWall>()->direction);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        Vector3 at = e->position;
        if (e->sound) at = e->sound->localPosition;
        e->UpdateFootprint(SpellType::FireWall, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
        e->CreateSound("magic\\Firewall\\end.wav", at, kZeroVector, kVolume, kHearing);
        RiseLight(e);
    }
    Linger(e);
}

// LightningWall: the light wall hits along its bolt every 4th tick; at -1 it goes out.
void LightWall(SpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0 && e->visual) {
            auto* bolt = e->Visual<EffectLightning>();
            Vector3 along;
            along.x = bolt->to.x - bolt->from.x;
            along.y = bolt->to.y - bolt->from.y;
            along.z = bolt->to.z - bolt->from.z;
            e->HitUnitsAlong(along);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        RiseLight(e);
        e->UpdateFootprint(SpellType::LightningWall, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
    }
    Linger(e);
}

// AcidFog: the poison cloud hits every 4th tick; at -1 it goes.
void PoisonCloud(SpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0) {
            HitAreaTargets(e);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        e->CreateSound("magic\\poison\\end.wav", e->targetPoint, kZeroVector, kVolume, kHearing);
        RiseLight(e);
        e->UpdateFootprint(SpellType::AcidFog, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
    }
    Linger(e);
}

// Campfire: hits every 4th tick, counting up, forever (until something else ends it).
void CountingCloud(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks + 1;
    if ((ticks & 3) == 0) HitAreaTargets(e);
}

// Fireworks wake the units around and frighten those in the radius (their AI memory notices
// the point at a level falling from 300 to 50 over the duration); at -1 they end.
void Fireworks(SpellEffect* e) {
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks >= 0) {
        float wake = e->Spell()->radius + ConstAt(worlddata::ImpactWakeMargin());
        e->world->WakeUnitsAround(e->position, wake);
        std::list<CUnit*> units = e->world->aiMap.sectorMap.FindUnitsInRadius(e->position, e->Spell()->radius);
        for (auto node = units.begin(); node != units.end();) {
            CUnit* unit = *node;
            ++node;
            int duration = static_cast<int>(e->Spell()->duration);
            int level = FrightLevel(&e->timer, &duration);
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
    if (e->timer < kEndedTimer) e->state = 3;
}

// Clairvoyance shows the caster's player every unit in the radius and wakes them; at timer
// 10 a closing particle and sound. Ends with the timer or without a caster.
void Clairvoyance(SpellEffect* e) {
    if (IsUnit(e->caster)) {
        auto* player = e->caster->player;
        if (player) {
            ScriptGroup* group = &player->Server()->possessedGroup;
            std::list<CUnit*> units = e->world->aiMap.sectorMap.FindUnitsInRadius(e->position, e->Spell()->radius);
            for (auto node = units.begin(); node != units.end();) {
                CUnit* unit = *node;
                ++node;
                group->Insert(unit);
            }
        }
        float wake = e->Spell()->radius + ConstAt(worlddata::ImpactWakeMargin());
        e->world->WakeUnitsAround(e->position, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0 || !e->caster) e->state = 3;
    if (ticks == 10) {
        e->CreateParticle(0, static_cast<int>(ParticleType::ClayRing), e->targetPoint, -e->Spell()->radius, 1.0f, kZeroVector, nullptr);
        e->CreateSound("magic\\Clairvoyence\\end.wav", e->position, kZeroVector, kVolume, kHearing);
    }
}

// Possession: the caster's player sees the target, its controller's two lists and its id;
// the target is flagged (0x2000000) and the units around it wake.
void Possession(SpellEffect* e) {
    if (!IsUnit(e->caster)) {
        e->state = 3;
        return;
    }
    auto* player = e->caster->player;
    if (player && e->target->GetClassId() == ClassId::Unit) {   // no null check on the target
        CMapObject* target = e->target;
        UnitServer* server = ControllerOf(TargetUnit(e));
        ScriptGroup* group = &player->Server()->possessedGroup;
        group->Insert(target);
        group->InsertAll(&server->brain.memory.visible);
        group->InsertAll(&server->brain.memory.seenCorpses);
        player->Server()->possessedId = target->id;
        target->flags |= MapObjectFlag::SeeAll;
        float sight = server->moveFactors[3] * server->stats.perception[0].current;
        float floor = server->stats.perception[2].current;
        float reach = sight >= floor ? sight : floor;
        float wake;
        MultiplyAdd(&reach, ConstPtr(worlddata::PossessWake()), ConstPtr(worlddata::PossessWakeBase()), &wake);
        e->world->WakeUnitsAround(target->localPosition, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0) e->state = 3;
}

// VisionFog lasts while its target is free.
void VisionFog(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0 || !e->target || e->target->IsDead() != 0) e->state = 3;
}

// Link: the link lasts while both units are free; it ends with a sound (and a complaint from the
// caster when a unit got busy).
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
    e->state = 3;
}

// Teleport: when the timer comes down to the spell's duration (at least 1) the caster moves
// to the target point at its own height, if it fits there (else it complains, 0x10); then it gets
// the spell's buff record and its controller re-places it.
void Teleport(SpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0 || !e->caster || e->caster->IsDead() != 0) {
        e->state = 3;
        return;
    }
    int arrival = static_cast<int>(e->Spell()->duration);
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
    MapEffect record = e->Spell()->Effect();
    e->caster->AddEffect(&record);
    ControllerOf(e->caster)->ResyncWithUnit();
    RiseLight(e);
}

} // namespace

int SpellEffect::Update() {
    if (!world) return 0;
    SpellType type = Spell()->Type();
    switch (state) {
    case 0: Start(type); return 1;
    case 1: UpdateMissile(type); return 1;
    case 2: UpdateLasting(type); return 1;
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
        MissileImpact(this, static_cast<int>(ParticleType::FireStar), "magic\\Firearrow\\end.wav", true);
        CountDown(this);
        break;
    case SpellType::AcidRay:
        MissileImpact(this, static_cast<int>(ParticleType::AcidStar), "magic\\Acidray\\end.wav", false);
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

void SpellEffect::ReleaseVisual() { Assign<CEffect>(this, &visual, nullptr); }

void SpellEffect::ReleaseSound() { Assign<CEffect>(this, &sound, nullptr); }

void SpellEffect::ReleaseLight() { Assign<EffectPointLight>(this, &light, nullptr); }
