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

// A player's server connection (CPlayer::connection_ on the server is a CPlayerServer).
CPlayerServer* ServerOf(CPlayer* player) { return player->Server(); }

// --- State 1 -------------------------------------------------------------------------------------

// 0 / 0x28 (the fire arrow) and 2 (the acid ray): the effect follows its missile; when the missile
// reports a hit the target takes the damage, the missile, sound and light go, and a
// splash, an end sound and a rising light appear at the target's height. The two spells compute
// the light's rise differently (the arrow rounds the quotient before scaling it).
void MissileImpact(CSpellEffect* e, int particleType, const char* sound, bool arrow) {
    auto* missile = e->Visual<CEffectMissile>();
    if (!missile) return;
    std::memcpy(&e->position, &missile->localPosition_, 12);
    if (missile->arrived_ == 0) return;

    DamageInfo hit;
    e->GetDamage(&hit);
    if (IsUnit(e->target)) e->HitUnit(&hit, e->target);
    e->ReleaseVisual();
    e->ReleaseSound();
    e->ReleaseLight();

    CUnit* target = TargetUnit(e);   // no null check
    std::memcpy(&e->targetPoint, &target->localPosition_, 12);
    Vector3 at;
    std::memcpy(&at, &target->localPosition_, 12);
    at.z = static_cast<float>(target->ModeSpeed());
    at.z = GroundPlus(e, kGridHeight, e->targetPoint, &at.z);
    float size = PowerScaled(e, worlddata::SplashScale());
    e->CreateParticle(0, particleType, &kZeroVector, size, 1.0f, &kZeroVector, e->target);
    e->CreateSound(0, sound, &at, &kZeroVector, kVolume, kHearing);

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
    e->CreatePointLight(&at, &color, range * ConstAt(worlddata::RiseScale()), &velocity);
    e->timer = 0x23;
    e->state = 2;
}

// 3: the fireball explodes when its timer runs out: everything in the radius is hit, a blast
// (the larger of the area curve and the power), an end sound, a rising light and, with a unit
// caster, a noise the AI hears. The missile's particle goes once the timer is below zero.
void FireballFlight(CSpellEffect* e) {
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
        AreaCurve(&e->Spell()->radius_, &Prototype(e)->area, &curve);
        float size = curve >= power ? curve : power;
        CEffectParticle* blast = e->CreateParticle(0, 0x2002, &at, size, 1.0f, &kZeroVector, nullptr);
        float radius = e->Spell()->radius_;
        float alpha = Power(e);
        blast->SetColors(alpha, radius);
        e->CreateSound(0, "magic\\fireball\\end.wav", &at, &kZeroVector, kVolume, kHearing);
        Vector3 velocity{0.0f, 0.0f, LightRiseScaled(e, worlddata::RiseScale())};
        float range = PrototypeRange(e);
        Vector3 color = PrototypeColor(e);
        e->CreatePointLight(&at, &color, range * ConstAt(worlddata::RiseScale()), &velocity);
        if (IsUnit(e->caster)) {
            e->world->AIEvents()->Noise(ControllerOf(e->caster), kExplosionLoudness, FtolOf(ConstPtr(worlddata::NoiseKind())), &e->targetPoint);
        }
        e->timer = 0x23;
        e->state = 2;
    }
    if (e->timer < 0) e->ReleaseVisual();
}

// --- State 2 -------------------------------------------------------------------------------------

// The common count-down: ends the effect once the timer was already below zero.
void CountDown(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0) e->state = 3;
}

// After the walls and clouds have ended: the timer keeps running down and the effect is removed
// 80 ticks later.
void Linger(CSpellEffect* e) {
    int ticks = e->timer;
    if (ticks < kEndedTimer) e->state = 3;
    e->timer = ticks - 1;
}

void RiseLight(CSpellEffect* e) {
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    e->light->SetVelocity(&rise);   // no null check
}

// 1 / 0x29: the lightning's bolt and sound go when the timer reaches 0, the light rises away.
void LightningFade(CSpellEffect* e) {
    if (e->timer == 0) {
        e->ReleaseVisual();
        e->ReleaseSound();
        RiseLight(e);
    }
    Linger(e);
}

// 4: the light strike's bolt is dropped every tick; at timer 5 a second bolt strikes from 30 above.
void LightStrikeEcho(CSpellEffect* e) {
    e->ReleaseVisual();
    if (e->timer == 5) {
        Vector3 top = e->position;
        top.z = top.z + ConstAt(worlddata::StrikeHeight());
        auto* bolt = CreateObject<CEffectLightning>(e, kClassLightning);
        bolt->SetEnds(&top, &e->position, kStrikeWidthImmediate);
        e->SetVisual(bolt);
    }
    CountDown(e);
}

// 5: the acid column hits once, at timer 15.
void AcidColumn(CSpellEffect* e) {
    CountDown(e);
    if (e->timer == 15) HitAreaTargets(e);
}

// 6: the fire wall burns along its direction every 4th tick; at -1 it goes out.
void FireWall(CSpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0 && e->visual) {
            e->HitUnitsAlong(&e->Visual<CEffectWall>()->direction_);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        Vector3 at = e->position;
        if (e->sound) std::memcpy(&at, &e->sound->localPosition_, 12);
        e->UpdateFootprint(6, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
        e->CreateSound(0, "magic\\Firewall\\end.wav", &at, &kZeroVector, kVolume, kHearing);
        RiseLight(e);
    }
    Linger(e);
}

// 7: the light wall hits along its bolt every 4th tick; at -1 it goes out.
void LightWall(CSpellEffect* e) {
    int ticks = e->timer;
    if (ticks >= 0) {
        if ((ticks & 3) == 0 && e->visual) {
            auto* bolt = e->Visual<CEffectLightning>();
            Vector3 along;
            along.x = bolt->to_.x - bolt->from_.x;
            along.y = bolt->to_.y - bolt->from_.y;
            along.z = bolt->to_.z - bolt->from_.z;
            e->HitUnitsAlong(&along);
            e->flags |= 1;
        }
        e->timer = e->timer - 1;
        return;
    }
    if (ticks == -1) {
        RiseLight(e);
        e->UpdateFootprint(7, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
    }
    Linger(e);
}

// 8: the poison cloud hits every 4th tick; at -1 it goes.
void PoisonCloud(CSpellEffect* e) {
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
        e->CreateSound(0, "magic\\poison\\end.wav", &e->targetPoint, &kZeroVector, kVolume, kHearing);
        RiseLight(e);
        e->UpdateFootprint(8, 0);
        e->ReleaseSound();
        e->ReleaseVisual();
    }
    Linger(e);
}

// 0x27: hits every 4th tick, counting up, forever (until something else ends it).
void CountingCloud(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks + 1;
    if ((ticks & 3) == 0) HitAreaTargets(e);
}

// 0x12: fireworks wake the units around and frighten those in the radius (their AI memory notices
// the point at a level falling from 300 to 50 over the duration); at -1 they end.
void Fireworks(CSpellEffect* e, std::list<CUnit*>& units) {
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks >= 0) {
        float wake = e->Spell()->radius_ + ConstAt(worlddata::ImpactWakeMargin());
        e->world->WakeUnitsAround(&e->position, wake);
        e->world->Sectors()->FindUnitsInRadius(&e->position, &units, e->Spell()->radius_);
        for (auto node = units.begin(); node != units.end();) {
            CUnit* unit = *node;
            ++node;
            int duration = static_cast<int>(e->Spell()->duration_);
            int level = FrightLevel(&e->timer, &duration);
            ControllerOf(unit)->Brain()->memory.Notice(&e->position, level, -1);
        }
        return;
    }
    if (ticks == -1) {
        e->CreateSound(0, "magic\\Firework\\end.wav", &e->position, &kZeroVector, kVolume, kHearing);
        e->ReleaseVisual();
        e->ReleaseSound();
        RiseLight(e);
    }
    if (e->timer < kEndedTimer) e->state = 3;
}

// 0x14: clairvoyance shows the caster's player every unit in the radius and wakes them; at timer
// 10 a closing particle and sound. Ends with the timer or without a caster.
void Clairvoyance(CSpellEffect* e, std::list<CUnit*>& units) {
    if (IsUnit(e->caster)) {
        auto* player = e->caster->player;
        if (player) {
            ScriptGroup* group = &ServerOf(player)->group50F8_;
            e->world->Sectors()->FindUnitsInRadius(&e->position, &units, e->Spell()->radius_);
            for (auto node = units.begin(); node != units.end();) {
                CUnit* unit = *node;
                ++node;
                group->Insert(unit);
            }
        }
        float wake = e->Spell()->radius_ + ConstAt(worlddata::ImpactWakeMargin());
        e->world->WakeUnitsAround(&e->position, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0 || !e->caster) e->state = 3;
    if (ticks == 10) {
        e->CreateParticle(0, 0x2018, &e->targetPoint, -e->Spell()->radius_, 1.0f, &kZeroVector, nullptr);
        e->CreateSound(0, "magic\\Clairvoyence\\end.wav", &e->position, &kZeroVector, kVolume, kHearing);
    }
}

// 0x15: possession: the caster's player sees the target, its controller's two lists and its id;
// the target is flagged (0x2000000) and the units around it wake.
void Possession(CSpellEffect* e) {
    if (!IsUnit(e->caster)) {
        e->state = 3;
        return;
    }
    auto* player = e->caster->player;
    if (player && e->target->GetClassId() == spellfx::kClassUnit) {   // no null check on the target
        CMapObject* target = e->target;
        CUnitServer* server = ControllerOf(TargetUnit(e));
        ScriptGroup* group = &ServerOf(player)->group50F8_;
        group->Insert(target);
        group->InsertAll(&server->Brain()->memory.visible);
        group->InsertAll(&server->Brain()->memory.seenCorpses);
        ServerOf(player)->possessedId_ = target->id_;
        target->flags_ |= 0x2000000;
        float sight = server->moveFactors[3] * server->stats.valuesB0[0].current;
        float floor = server->stats.valuesB0[2].current;
        float reach = sight >= floor ? sight : floor;
        float wake;
        MultiplyAdd(&reach, ConstPtr(worlddata::PossessWake()), ConstPtr(worlddata::PossessWakeBase()), &wake);
        e->world->WakeUnitsAround(&target->localPosition_, wake);
    }
    int ticks = e->timer - 1;
    e->timer = ticks;
    if (ticks < 0) e->state = 3;
}

// 0x17: vision fog lasts while its target is free.
void VisionFog(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0 || !e->target || e->target->IsDead() != 0) e->state = 3;
}

// 0x1B: the link lasts while both units are free; it ends with a sound (and a complaint from the
// caster when a unit got busy).
void Link(CSpellEffect* e) {
    bool free = e->target && e->caster && e->target->IsLargeUnit() == 0 &&
                e->caster->IsLargeUnit() == 0 && e->target->IsDead() == 0 &&
                e->caster->IsDead() == 0;
    if (free) {
        int ticks = e->timer - 1;
        e->timer = ticks;
        if (ticks >= 0) return;
        e->CreateSound(0, "magic\\Link\\end.wav", &e->position, &kZeroVector, kVolume, kHearing);
    } else {
        e->CreateSound(0, "magic\\Link\\end.wav", &e->position, &kZeroVector, kVolume, kHearing);
        if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(0x19);
    }
    e->state = 3;
}

// 0x1C: teleport: when the timer comes down to the spell's duration (at least 1) the caster moves
// to the target point at its own height, if it fits there (else it complains, 0x10); then it gets
// the spell's buff record and its controller re-places it.
void Teleport(CSpellEffect* e) {
    int ticks = e->timer;
    e->timer = ticks - 1;
    if (ticks < 0 || !e->caster || e->caster->IsDead() != 0) {
        e->state = 3;
        return;
    }
    int arrival = static_cast<int>(e->Spell()->duration_);
    if (arrival < 1) arrival = 1;
    if (e->timer != arrival) return;
    std::memcpy(&e->targetPoint.z, &e->caster->localPosition_.z, 4);
    if (!e->world->AIMap()->IsPassableWithoutSelf(e->caster, &e->targetPoint)) {
        if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(0x10);
        return;
    }
    e->world->Sectors()->Move(e->caster, &e->targetPoint);
    e->ReleaseVisual();
    e->ReleaseSound();
    if (!IsUnit(e->caster)) return;
    MapEffect record = e->Spell()->Effect();
    e->caster->AddEffect(&record);
    ControllerOf(e->caster)->ResyncWithUnit();
    RiseLight(e);
}

} // namespace

int CSpellEffect::Update() {
    if (!world) return 0;
    int type = static_cast<int>(Spell()->prototypeId_ >> 16);
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

void CSpellEffect::UpdateMissile(int type) {
    switch (type) {
    case 0x00:
    case 0x28:
        MissileImpact(this, 0x2041, "magic\\Firearrow\\end.wav", true);
        CountDown(this);
        break;
    case 0x02:
        MissileImpact(this, 0x2042, "magic\\Acidray\\end.wav", false);
        CountDown(this);
        break;
    case 0x03: FireballFlight(this); break;
    default: break;
    }
}

void CSpellEffect::UpdateLasting(int type) {
    std::list<CUnit*> units;   // the inline CPtrList the fireworks and clairvoyance fill

    switch (type) {
    case 0x00:
    case 0x02:
    case 0x03:
    case 0x18:
    case 0x28: CountDown(this); break;
    case 0x01:
    case 0x29: LightningFade(this); break;
    case 0x04: LightStrikeEcho(this); break;
    case 0x05: AcidColumn(this); break;
    case 0x06: FireWall(this); break;
    case 0x07: LightWall(this); break;
    case 0x08: PoisonCloud(this); break;
    case 0x12: Fireworks(this, units); break;
    case 0x14: Clairvoyance(this, units); break;
    case 0x15: Possession(this); break;
    case 0x17: VisionFog(this); break;
    case 0x1B: Link(this); break;
    case 0x1C: Teleport(this); break;
    case 0x27: CountingCloud(this); break;
    default: break;
    }

    units.clear();
}

void CSpellEffect::ReleaseVisual() { Assign<CEffect>(this, &visual, nullptr); }

void CSpellEffect::ReleaseSound() { Assign<CEffect>(this, &sound, nullptr); }

void CSpellEffect::ReleaseLight() { Assign<CEffectPointLight>(this, &light, nullptr); }
