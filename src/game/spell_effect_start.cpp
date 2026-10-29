// A spell effect's casting state (CSpellEffect::Update state 0) and the helpers that create its effect
// objects. Every spell type starts differently: missiles get a CEffectArrow or a particle source and go to
// state 1, the lasting spells get particles, lights and sound sequences and go to state 2, the instant ones
// (lightning, the light strike, buffs, charm) deal or apply their effect here. See
// docs/notes/items_spells.md.
#include "ai_map.h"
#include "map_object.h"
#include "spell.h"
#include "spell_effect_internal.h"
#include "combat.h"
#include "game_strings.h"
#include "unit.h"
#include "world.h"
#include "world_effect.h"
#include "lib/nan_audit.h"
#include "lib/str.h"

#include <string>


using namespace spellfx;

namespace {


// --- The spell types ----------------------------------------------------------------------------

// FireArrow (particle FireArrow) and RickMagic (RikArrowSmoke): the fire arrow, a missile at a unit target.
Next StartFireArrow(CSpellEffect* e, int type, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = Power(e);
    e->CreateArrow(&from, e->target, SpellTypeOf(type) == SpellType::FireArrow ? static_cast<int>(ParticleType::FireArrow) : static_cast<int>(ParticleType::RikArrowSmoke), power, "magic\\Firearrow\\circle.wav", &color, range);
    e->CreateSound(0, "magic\\Firearrow\\start.wav", &e->position, &kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// Lightning and CurseMagic: lightning from the caster to the target, hitting it at once.
Next StartLightning(CSpellEffect* e, int type, const Vector3& from) {
    float power = Power(e);
    e->CreateLightning(&from, e->target, e->caster, power, SpellTypeOf(type) == SpellType::CurseMagic ? 1 : 0);
    e->CreateSound(0, "magic\\lightning\\start.wav", &e->position, &kZeroVector, kVolume, kHearing);
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    Vector3 scratch;
    e->CreatePointLight(EyePointOf(e->target, &scratch), &color, range, &kZeroVector);   // no null check
    DamageInfo hit;
    e->GetDamage(&hit);
    if (IsUnit(e->target)) e->HitUnit(&hit, e->target);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// AcidRay: a missile (power x 0.4).
Next StartAcidRay(CSpellEffect* e, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = PowerScaled(e, worlddata::AcidRayScale());
    e->CreateArrow(&from, e->target, static_cast<int>(ParticleType::AcidRay), power, "magic\\Acidray\\circle.wav", &color, range);
    e->CreateSound(0, "magic\\Acidray\\start.wav", &e->position, &kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// Fireball: a particle source flying at the target point; it explodes in state 1.
Next StartFireball(CSpellEffect* e, const Vector3& from) {
    float speed = e->Spell()->speed_;
    Vector3 velocity;
    velocity.x = e->targetPoint.x - e->position.x;
    velocity.y = e->targetPoint.y - e->position.y;
    velocity.z = e->targetPoint.z - e->position.z;
    velocity.z = 0.0f;
    float distance;
    PlanarLength(&velocity, &distance);
    speed = speed > 0.0f ? speed : kDefaultSpeed;
    e->timer = FlightTicks(&distance, &speed) - 2;
    ScaleToSpeed(&velocity, &speed);

    float size = PowerScaled(e, worlddata::FireballScale());
    CEffectParticle* particle = e->CreateParticle(1, static_cast<int>(ParticleType::FireBall), &from, size, 1.0f, &velocity, nullptr);   // the visual
    if (e->caster) particle->unitId_ = e->caster->id_;   // the particle's unit (no null check)
    e->targetPoint.z = GroundPlus(e, kRawSurfaceHeight, e->targetPoint, ConstPtr(worlddata::OneRef()));
    particle->SetTarget(&e->targetPoint);
    if (speed * ConstAt(worlddata::FireballNear()) > distance) {
        e->CreateSound(0, "magic\\FireBall\\start.wav", &e->position, &velocity, kVolume, kHearing);
    } else {
        e->CreateSoundSequence(&e->position, &velocity, "magic\\Fireball\\start.wav", "magic\\Fireball\\circle.wav", -1, kVolume,
                               kHearing);
    }
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(&from, &color, range, &velocity);
    e->state = 1;
    return Next::Set;
}

// InvokeLightning: a lightning strike from 30 above the target point, hitting every unit in the radius.
Next StartLightStrike(CSpellEffect* e) {
    e->targetPoint.z = GroundMinus(e, kRawSurfaceHeight, e->targetPoint, ConstPtr(worlddata::StrikeDepth()));
    e->position = e->targetPoint;
    Vector3 top = e->targetPoint;
    top.z = top.z + ConstAt(worlddata::StrikeHeight());
    float power = Power(e);
    auto* bolt = CreateObject<CEffectLightning>(e, ClassId::EffectLightning);
    bolt->SetEnds(&top, &e->position, power * ConstAt(worlddata::StrikeWidth()));
    e->SetVisual(bolt);

    float alpha = Power(e);
    e->CreateParticle(0, static_cast<int>(ParticleType::LightningBlast), &e->targetPoint, e->Spell()->radius_, alpha, &kZeroVector, nullptr);
    e->CreateSound(0, "magic\\InvokeLight\\start.wav", &e->position, &kZeroVector, kLoud, kLoud);
    e->targetPoint.z = e->targetPoint.z + ConstAt(worlddata::LightAbove());
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, &e->targetPoint, PrototypeRange(e), &rise);

    HitAreaTargets(e);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// AcidColumn: the column at the target point.
Next StartAcidColumn(CSpellEffect* e) {
    e->position = e->targetPoint;
    e->CreateParticle(0, static_cast<int>(ParticleType::Nuke), &e->targetPoint, 1.0f, 1.0f, &kZeroVector, nullptr);
    e->CreateSound(0, "magic\\Acidcolumn\\start.wav", &e->targetPoint, &kZeroVector, kVolume, kHearing);
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, &e->targetPoint, PrototypeRange(e), &rise);
    e->timer = 0x1E;
    e->state = 2;
    return Next::Set;
}

// The direction from the caster to the wall's centre; straight along x when it has no ground part
// (both 0).
Vector3 WallDirection(const Vector3& centre, const Vector3& from) {
    Vector3 d;
    d.x = centre.x - from.x;
    d.y = centre.y - from.y;
    d.z = centre.z - from.z;
    EI_NAN_AUDIT(d.x, d.y);
    if (d.x == 0.0f && d.y == 0.0f) d.x = 1.0f;
    return d;
}

// FireWall: the wall across the line from the caster to the target point.
Next StartFireWall(CSpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    float alpha = Power(e);
    float size = Power(e);
    e->CreateParticle(1, static_cast<int>(ParticleType::Tendril), &centre, size, alpha, &kZeroVector, nullptr);
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(&direction, &e->Spell()->radius_, &span);
    if (e->visual) e->Visual<CEffectWall>()->SetDirection(&span);
    e->position = centre;
    e->timer = static_cast<int>(e->Spell()->duration_);
    e->state = 2;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float radius = e->Spell()->radius_;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, "magic\\Firewall\\start.wav", "magic\\Firewall\\circle.wav", -1, kVolume, kHearing);
    return Next::Set;
}

// LightningWall: the light wall, a lightning between the two ends of the same span.
Next StartLightWall(CSpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(&direction, &e->Spell()->radius_, &span);
    Vector3 a, b;
    a.x = e->targetPoint.x - span.x;
    a.y = e->targetPoint.y - span.y;
    a.z = e->targetPoint.z - span.z;
    b.x = span.x + e->targetPoint.x;
    b.y = span.y + e->targetPoint.y;
    b.z = span.z + e->targetPoint.z;
    a.z = GroundPlus(e, kRawSurfaceHeight, a, ConstPtr(worlddata::OneRef()));
    b.z = GroundPlus(e, kRawSurfaceHeight, b, ConstPtr(worlddata::OneRef()));
    auto* bolt = CreateObject<CEffectLightning>(e, ClassId::EffectLightning);
    bolt->SetEnds(&a, &b, 0.0f);
    e->SetVisual(bolt);
    e->position = centre;
    e->timer = static_cast<int>(e->Spell()->duration_);
    e->state = 2;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float radius = e->Spell()->radius_;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, game::kEmptyText, "magic\\Lightwall\\circle.wav", -1, kVolume, kHearing);
    return Next::Set;
}

// AcidFog: the poison cloud at the target point.
Next StartPoisonCloud(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float alpha = Power(e);
    e->CreateParticle(1, static_cast<int>(ParticleType::PoisonFog), &centre, e->Spell()->radius_, alpha, &kZeroVector, nullptr);
    e->CreateSoundSequence(&centre, &kZeroVector, "magic\\poison\\start.wav", "magic\\poison\\circle.wav", -1, kVolume, kHearing);
    float radius = e->Spell()->radius_;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->position = centre;
    return Next::Set;   // state 2, timer = duration (the shared tail)
}

// The buffs (Start lists them). The target gets a record of the spell
// with the strength trunc(power x 16); the effect itself ends.
Next ApplyBuff(CSpellEffect* e) {
    if (!e->target) return Next::Done;
    MapEffect record = e->Spell()->Effect();
    record.power = static_cast<std::uint8_t>(
        PowerScaledToInt(&e->Spell()->damage_, &Prototype(e)->damage, ConstPtr(worlddata::BuffScale())));
    record.ending = e->silentEnd;
    e->target->AddEffect(&record);
    return Next::Done;
}

// Fireworks (five numbered sounds).
Next StartFirework(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float size = Power(e);
    e->CreateParticle(1, static_cast<int>(ParticleType::Geyser), &centre, size, 1.0f, &kZeroVector, nullptr);
    PrototypeLight(e, &centre, PrototypeRange(e), &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, game::kEmptyText, "magic\\Firework\\%i.wav", 5, kVolume, kHearing);
    e->position = centre;
    return Next::Set;
}

// Clairvoyance. The particle uses the target point as it was; the light, the ground point.
Next StartClairvoyance(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    e->position = centre;
    e->CreateParticle(0, static_cast<int>(ParticleType::ClayRing), &e->targetPoint, e->Spell()->radius_, 1.0f, &kZeroVector, nullptr);
    PrototypeLight(e, &centre, e->Spell()->radius_, &kZeroVector);
    e->CreateSound(0, "magic\\Clairvoyence\\start.wav", &e->position, &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// Possession (only the sound here).
Next StartPossession(CSpellEffect* e) {
    if (!e->target) return Next::Done;
    Vector3 scratch;
    e->CreateSound(0, "magic\\Possession\\start.wav", EyePointOf(e->target, &scratch), &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// VisionFog: a CEffectShowVisibility linking the caster and the target.
Next StartVisionFog(CSpellEffect* e) {
    if (!e->caster || !e->target) return Next::Done;
    int targetId = e->target->id_;
    int casterId = e->caster->id_;
    auto* shown = CreateObject<CEffectShowVisibility>(e, ClassId::EffectShowVisibility);
    shown->SetUnits(casterId, targetId);
    SetPosition(shown, &kZeroVector);
    e->SetVisual(shown);
    Vector3 scratch;
    e->CreateSound(0, "magic\\VisionFog\\start.wav", EyePointOf(e->target, &scratch), &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// Healing. Heals the target's controller by the effect value; the sound and the light only
// when the target is not at full health (or +0xA4 is clear).
Next StartHealing(CSpellEffect* e) {
    if (e->target->GetClassId() != ClassId::Unit) return Next::Done;   // no null check
    // Controller Heal (the amount's bits are pushed, MOV/PUSH).
    CUnit* unit = TargetUnit(e);
    unit->Controller()->Heal(e->Spell()->damage_);
    float size = Power(e);
    e->CreateParticle(0, static_cast<int>(ParticleType::Healing), &kZeroVector, size, 1.0f, &kZeroVector, unit);
    bool full = unit->packedVitals.hitPoints == unit->packedVitals.maxHitPoints;
    if (e->silentEnd == 0 || !full) {
        e->CreateSound(0, "magic\\Healing\\start.wav", &unit->localPosition_, &kZeroVector, kVolume, kHearing);
        Vector3 rise{0.0f, 0.0f, LightRise(e)};
        float range = PrototypeRange(e);
        Vector3 color = PrototypeColor(e);
        e->CreatePointLight(&e->targetPoint, &color, -range, &rise);
    }
    e->timer = 0x1E;
    e->state = 2;
    return Next::Set;
}

// Link between two different units that are both free; otherwise the caster complains (0x18).
Next StartLink(CSpellEffect* e) {
    if (!IsUnit(e->target) || !IsUnit(e->caster)) return Next::Done;
    bool free = e->target->IsLargeUnit() == 0 && e->caster->IsLargeUnit() == 0 &&
                e->target->IsDead() == 0 && e->target != e->caster;
    if (!free) {
        if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(AckKind::LinkImpossible);
        return Next::Done;
    }
    Vector3 scratch;
    const Vector3* casterAt = EyePointOf(e->caster, &scratch);
    Vector3 from = *casterAt;
    e->position = *casterAt;
    e->CreateParticle(1, static_cast<int>(ParticleType::Link), &from, 1.0f, 1.0f, &kZeroVector, nullptr);
    if (e->visual) {
        const Vector3* to = EyePointOf(e->target, &scratch);
        Vector3 direction;
        direction.x = to->x - from.x;
        direction.y = to->y - from.y;
        direction.z = to->z - from.z;
        e->Visual<CEffectWall>()->SetDirection(&direction);
    }
    e->CreateSoundSequence(&from, &kZeroVector, "magic\\Link\\start.wav", "magic\\Link\\circle.wav", -1, kVolume, kHearing);
    return Next::Set;
}

// Teleport: a particle where the caster stands and an inverted one at the target point.
Next StartTeleport(CSpellEffect* e) {
    const Vector3* source = e->caster ? &e->caster->localPosition_ : &e->position;
    Vector3 point = *source;
    point.z = GroundPlus(e, kGridHeight, point, &point.z);
    e->CreateParticle(1, static_cast<int>(ParticleType::Teleport), &point, 1.0f, 1.0f, &kZeroVector, nullptr);
    point = e->targetPoint;
    point.z = GroundMinus(e, kGridHeight, point, ConstPtr(worlddata::TeleportDepth()));
    e->CreateParticle(0, static_cast<int>(ParticleType::Teleport), &point, -1.0f, 1.0f, &kZeroVector, nullptr);
    e->CreateSound(0, "magic\\Teleport\\start.wav", &point, &kZeroVector, kVolume, kHearing);
    PrototypeLight(e, &point, PrototypeRange(e), &kZeroVector);
    int duration = static_cast<int>(e->Spell()->duration_);
    if (duration < 1) duration = 1;
    e->state = 2;
    e->timer = duration + 0x1E;
    return Next::Set;
}

// Charm. The target's controller is tamed by the caster until their sides (controller
// +0x1C, +0x24C) agree; a failed attempt makes the caster complain (0x1A).
Next StartCharm(CSpellEffect* e) {
    if (e->target->GetClassId() != ClassId::Unit) return Next::Leave;   // no null check
    if (!IsUnit(e->caster)) return Next::Leave;
    CUnitServer* tamed = ControllerOf(TargetUnit(e));
    CUnitServer* tamer = ControllerOf(e->caster);
    float skill = tamer->stats.intelligence + e->Spell()->damage_;
    auto side = [](CUnitServer* controller) { return controller->unit->player; };
    while (side(tamed) != side(tamer)) {
        if (tamed->TryTame(e->caster, skill) == 0) {
            if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(AckKind::LinkTameFailed);
            return Next::Done;
        }
    }
    e->CreateSound(0, "magic\\Charm\\start.wav", &e->target->localPosition_, &kZeroVector, kVolume, kHearing);
    return Next::Done;
}

// Campfire: the effect only moves to the ground point.
Next StartAtGround(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    e->position = centre;
    return Next::Set;
}

} // namespace


void CSpellEffect::Start(int type) {
    flags &= ~1u;
    Vector3 from;
    if (caster) {
        Vector3 scratch;
        from = *EyePointOf(caster, &scratch);
    } else {
        from = position;
    }

    // The cases that end in the shared tail (state 2, timer = the spell's duration) return Set
    // without touching the state; the others set it themselves.
    Next next = Next::Done;
    bool durationTail = false;
    switch (SpellTypeOf(type)) {
    case SpellType::FireArrow:
    case SpellType::RickMagic: next = StartFireArrow(this, type, from); break;
    case SpellType::Lightning:
    case SpellType::CurseMagic: next = StartLightning(this, type, from); break;
    case SpellType::AcidRay: next = StartAcidRay(this, from); break;
    case SpellType::Fireball: next = StartFireball(this, from); break;
    case SpellType::InvokeLightning: next = StartLightStrike(this); break;
    case SpellType::AcidColumn: next = StartAcidColumn(this); break;
    case SpellType::FireWall: next = StartFireWall(this, from); break;
    case SpellType::LightningWall: next = StartLightWall(this, from); break;
    case SpellType::AcidFog: next = StartPoisonCloud(this); durationTail = true; break;
    case SpellType::ProtectionFromFire: case SpellType::ProtectionFromLightning: case SpellType::ProtectionFromAcid:
    case SpellType::EagleSight: case SpellType::Infravision: case SpellType::DetectLife: case SpellType::Invisibility:
    case SpellType::Silence: case SpellType::Lichdom: case SpellType::Stench: case SpellType::Stun:
    case SpellType::Antimagic: case SpellType::Strengthen: case SpellType::Weaken: case SpellType::Regeneration:
    case SpellType::Feeblemind: case SpellType::Speed: case SpellType::Slow: case SpellType::Enlarge:
    case SpellType::Shrink: next = ApplyBuff(this); break;
    case SpellType::Fireworks: next = StartFirework(this); durationTail = true; break;
    case SpellType::Clairvoyance: next = StartClairvoyance(this); durationTail = true; break;
    case SpellType::Possession: next = StartPossession(this); durationTail = true; break;
    case SpellType::VisionFog: next = StartVisionFog(this); durationTail = true; break;
    case SpellType::Healing: next = StartHealing(this); break;
    case SpellType::Link: next = StartLink(this); durationTail = true; break;
    case SpellType::Teleport: next = StartTeleport(this); break;
    case SpellType::Charm: next = StartCharm(this); break;
    case SpellType::Campfire: next = StartAtGround(this); durationTail = true; break;
    default: break;   // Shapechange, Comprehend and above CurseMagic
    }
    if (next == Next::Done) {
        state = 3;
    } else if (next == Next::Set && durationTail) {
        state = 2;
        timer = static_cast<int>(Spell()->duration_);
    }
    UpdateFootprint(type, 1);
}

void CSpellEffect::SetVisual(CEffect* object) {
    Assign(this, &visual, object);
    world->AddTimedObject(visual);
}

void CSpellEffect::UpdateFootprint(int type, int stamp) {
    if (!visual) return;
    CAIMap* aiMap = &world->aiMap_;
    switch (SpellTypeOf(type)) {
    case SpellType::FireWall: {
        Vector3 direction;
        direction = (Visual<CEffectWall>()->direction_);
        g_spellFootprint.AlongDirection(aiMap, &position, kHalfBits, &direction);
        break;
    }
    case SpellType::LightningWall: {
        Vector3 half;
        HalfSpan(Visual<CEffectLightning>(), &half);
        g_spellFootprint.AlongDirection(aiMap, &position, kHalfBits, &half);
        break;
    }
    case SpellType::AcidFog:
    case SpellType::Campfire: g_spellFootprint.DiscAt(aiMap, &position, Spell()->radius_); break;
    default: return;
    }
    if (stamp) {
        aiMap->StampFootprint(world->tick_, &g_spellFootprint);
        footprintStamped = 1;
    } else {
        aiMap->ClearFootprint(&g_spellFootprint);
        footprintStamped = 0;
    }
}

void CSpellEffect::CreateArrow(const Vector3* from, CEngineObject* missileTarget, int particleType, float power,
                               const char* soundName, const Vector3* color, float range) {
    auto* arrow = CreateObject<CEffectArrow>(this, ClassId::EffectArrow);
    if (!arrow) return;
    arrow->lightColor_ = *color;
    arrow->lightRange_ = range;
    SetPosition(arrow, from);
    arrow->SetTarget(missileTarget->id_);
    arrow->particleType_ = particleType;
    arrow->particleSize_ = power;
    arrow->soundName_ = soundName;
    if (caster) {
        arrow->fromCaster_ = 1;
        arrow->casterId_ = caster->id_;
    }
    Assign<CEffect>(this, &visual, arrow);
    world->AddTimedObject(visual);
}

CEffectParticle* CSpellEffect::CreateParticle(int keep, int particleType, const Vector3* at, float size, float alpha,
                                              const Vector3* velocity, CEngineObject* attachTo) {
    ClassId classId = (ParticleTypeOf(particleType) == ParticleType::Tendril || ParticleTypeOf(particleType) == ParticleType::Link) ? ClassId::EffectWall : ClassId::EffectParticle;
    auto* particle = CreateObject<CEffectParticle>(this, classId);   // a CEffectWall for the walls
    particle->type_ = particleType;
    SetPosition(particle, at);
    particle->size_ = size;
    particle->SetVelocity(velocity);
    particle->alpha_ = alpha;
    if (attachTo) particle->SetAttachedTo(attachTo->id_);
    if (keep) {
        Assign<CEffect>(this, &visual, particle);
        world->AddTimedObject(visual);
    } else {
        world->AddTimedObject(particle);
    }
    return particle;
}

void CSpellEffect::CreateLightning(const Vector3* from, CMapObject* boltTarget, CEngineObject* source, float power,
                                   int flag) {
    if (!boltTarget) return;
    Vector3 to = boltTarget->localPosition_;
    HeightPlusProduct(world, kGridHeight, to.x, to.y, &boltTarget->bounds.boxMax.z,
                      ConstPtr(worlddata::TargetHeight()), &to.z);
    auto* bolt = CreateObject<CEffectLightning>(this, ClassId::EffectLightning);
    bolt->SetEnds(from, &to, power * ConstAt(worlddata::BoltWidthScale()));
    bolt->SetObjects(source ? source->id_ : -1, boltTarget->id_);
    bolt->SetFlag(static_cast<unsigned char>(flag));
    Assign<CEffect>(this, &visual, bolt);
    world->AddTimedObject(visual);
}

void CSpellEffect::CreateSound(int keep, const char* name, const Vector3* at, const Vector3* velocity, float volume,
                               float hearing) {
    auto* effect = CreateObject<CEffectSound>(this, ClassId::EffectSound);
    SetPosition(effect, at);
    effect->noFade_ = 1;
    effect->radius_ = hearing;
    effect->volume_ = volume;
    std::memcpy(effect->name_, name, std::strlen(name) + 1);
    effect->SetVelocity(velocity);
    effect->looping_ = keep;
    if (keep) Assign<CEffect>(this, &sound, effect);
    world->AddTimedObject(effect);
}

void CSpellEffect::CreateSoundSequence(const Vector3* at, const Vector3* velocity, const char* first,
                                       const char* pattern, int count, float volume, float hearing) {
    std::string name;
    auto* effect = CreateObject<CEffectSoundSeq>(this, ClassId::EffectSoundSeq);
    SetPosition(effect, at);
    effect->SetVelocity(velocity);
    effect->radius_ = hearing;
    effect->volume_ = volume;
    effect->noFade_ = 1;
    effect->name_ = first;
    auto& names = effect->names_;
    if (count < 1) {
        names.push_back(pattern);
    } else {
        for (int i = 1; i <= count; ++i) {
            name = str::Format(pattern, i);
            names.push_back(name);
        }
    }
    Assign<CEffect>(this, &sound, effect);
    world->AddTimedObject(effect);
}

void CSpellEffect::CreatePointLight(const Vector3* at, const Vector3* color, float range, const Vector3* velocity) {
    auto* effect = CreateObject<CEffectPointLight>(this, ClassId::EffectPointLight);
    effect->color_ = *color;
    SetPosition(effect, at);
    effect->range_ = range;
    effect->SetVelocity(velocity);
    effect->lightFlags_ = kPointLightFlags;
    Assign(this, &light, effect);
    world->AddTimedObject(effect);
}
