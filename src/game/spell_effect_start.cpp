// A spell effect's casting state (SpellEffect::Update state 0) and the helpers that create its effect
// objects. Every spell type starts differently: missiles get a EffectArrow or a particle source and go to
// state 1, the lasting spells get particles, lights and sound sequences and go to state 2, the instant ones
// (lightning, the light strike, buffs, charm) deal or apply their effect here. Also the cast spell's
// lifetime. See docs/notes/items_spells.md.
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
Next StartFireArrow(SpellEffect* e, SpellType type, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = Power(e);
    e->CreateArrow(from, e->target, type == SpellType::FireArrow ? static_cast<int>(ParticleType::FireArrow) : static_cast<int>(ParticleType::RikArrowSmoke), power, "magic\\Firearrow\\circle.wav", color, range);
    e->CreateSound("magic\\Firearrow\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// Lightning and CurseMagic: lightning from the caster to the target, hitting it at once.
Next StartLightning(SpellEffect* e, SpellType type, const Vector3& from) {
    float power = Power(e);
    e->CreateLightning(from, e->target, e->caster, power, type == SpellType::CurseMagic ? 1 : 0);
    e->CreateSound("magic\\lightning\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(e->target->EyePoint(), color, range, kZeroVector);   // no null check
    DamageInfo hit;
    e->GetDamage(&hit);
    if (IsUnit(e->target)) e->HitUnit(&hit, e->target);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// AcidRay: a missile (power x 0.4).
Next StartAcidRay(SpellEffect* e, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = PowerScaled(e, worlddata::AcidRayScale());
    e->CreateArrow(from, e->target, static_cast<int>(ParticleType::AcidRay), power, "magic\\Acidray\\circle.wav", color, range);
    e->CreateSound("magic\\Acidray\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// Fireball: a particle source flying at the target point; it explodes in state 1.
Next StartFireball(SpellEffect* e, const Vector3& from) {
    float speed = e->HeldSpell()->speed;
    Vector3 velocity;
    velocity.x = e->targetPoint.x - e->position.x;
    velocity.y = e->targetPoint.y - e->position.y;
    velocity.z = e->targetPoint.z - e->position.z;
    velocity.z = 0.0f;
    float distance;
    PlanarLength(velocity, &distance);
    speed = speed > 0.0f ? speed : kDefaultSpeed;
    e->timer = FlightTicks(&distance, &speed) - 2;
    ScaleToSpeed(&velocity, &speed);

    float size = PowerScaled(e, worlddata::FireballScale());
    EffectParticle* particle = e->CreateParticle(1, static_cast<int>(ParticleType::FireBall), from, size, 1.0f, velocity, nullptr);   // the visual
    if (e->caster) particle->unitId = e->caster->id;     // the particle's unit (no null check)
    e->targetPoint.z = GroundPlus(e, kRawSurfaceHeight, e->targetPoint, ConstPtr(worlddata::OneRef()));
    particle->SetTarget(e->targetPoint);
    if (speed * ConstAt(worlddata::FireballNear()) > distance) {
        e->CreateSound("magic\\FireBall\\start.wav", e->position, velocity, kVolume, kHearing);
    } else {
        e->CreateSoundSequence(e->position, velocity, "magic\\Fireball\\start.wav", "magic\\Fireball\\circle.wav", -1);
    }
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(from, color, range, velocity);
    e->state = 1;
    return Next::Set;
}

// InvokeLightning: a lightning strike from 30 above the target point, hitting every unit in the radius.
Next StartLightStrike(SpellEffect* e) {
    e->targetPoint.z = GroundMinus(e, kRawSurfaceHeight, e->targetPoint, ConstPtr(worlddata::StrikeDepth()));
    e->position = e->targetPoint;
    Vector3 top = e->targetPoint;
    top.z = top.z + ConstAt(worlddata::StrikeHeight());
    float power = Power(e);
    auto* bolt = CreateObject<EffectLightning>(e, ClassId::EffectLightning);
    bolt->SetEnds(top, e->position, power * ConstAt(worlddata::StrikeWidth()));
    e->SetVisual(bolt);

    float alpha = Power(e);
    e->CreateParticle(0, static_cast<int>(ParticleType::LightningBlast), e->targetPoint, e->HeldSpell()->radius, alpha, kZeroVector, nullptr);
    e->CreateSound("magic\\InvokeLight\\start.wav", e->position, kZeroVector, kLoud, kLoud);
    e->targetPoint.z = e->targetPoint.z + ConstAt(worlddata::LightAbove());
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, e->targetPoint, PrototypeRange(e), rise);

    HitAreaTargets(e);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// AcidColumn: the column at the target point.
Next StartAcidColumn(SpellEffect* e) {
    e->position = e->targetPoint;
    e->CreateParticle(0, static_cast<int>(ParticleType::Nuke), e->targetPoint, 1.0f, 1.0f, kZeroVector, nullptr);
    e->CreateSound("magic\\Acidcolumn\\start.wav", e->targetPoint, kZeroVector, kVolume, kHearing);
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, e->targetPoint, PrototypeRange(e), rise);
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
Next StartFireWall(SpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    float alpha = Power(e);
    float size = Power(e);
    e->CreateParticle(1, static_cast<int>(ParticleType::Tendril), centre, size, alpha, kZeroVector, nullptr);
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(direction, &e->HeldSpell()->radius, &span);
    if (e->visual) e->Visual<EffectWall>()->SetDirection(span);
    e->position = centre;
    e->timer = static_cast<int>(e->HeldSpell()->duration);
    e->state = 2;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float radius = e->HeldSpell()->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, centre, radius + range, kZeroVector);
    e->CreateSoundSequence(centre, kZeroVector, "magic\\Firewall\\start.wav", "magic\\Firewall\\circle.wav", -1);
    return Next::Set;
}

// LightningWall: the light wall, a lightning between the two ends of the same span.
Next StartLightWall(SpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(direction, &e->HeldSpell()->radius, &span);
    Vector3 a, b;
    a.x = e->targetPoint.x - span.x;
    a.y = e->targetPoint.y - span.y;
    a.z = e->targetPoint.z - span.z;
    b.x = span.x + e->targetPoint.x;
    b.y = span.y + e->targetPoint.y;
    b.z = span.z + e->targetPoint.z;
    a.z = GroundPlus(e, kRawSurfaceHeight, a, ConstPtr(worlddata::OneRef()));
    b.z = GroundPlus(e, kRawSurfaceHeight, b, ConstPtr(worlddata::OneRef()));
    auto* bolt = CreateObject<EffectLightning>(e, ClassId::EffectLightning);
    bolt->SetEnds(a, b, 0.0f);
    e->SetVisual(bolt);
    e->position = centre;
    e->timer = static_cast<int>(e->HeldSpell()->duration);
    e->state = 2;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float radius = e->HeldSpell()->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, centre, radius + range, kZeroVector);
    e->CreateSoundSequence(centre, kZeroVector, game::kEmptyText, "magic\\Lightwall\\circle.wav", -1);
    return Next::Set;
}

// AcidFog: the poison cloud at the target point.
Next StartPoisonCloud(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float alpha = Power(e);
    e->CreateParticle(1, static_cast<int>(ParticleType::PoisonFog), centre, e->HeldSpell()->radius, alpha, kZeroVector, nullptr);
    e->CreateSoundSequence(centre, kZeroVector, "magic\\poison\\start.wav", "magic\\poison\\circle.wav", -1);
    float radius = e->HeldSpell()->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, centre, radius + range, kZeroVector);
    e->position = centre;
    return Next::Set;   // state 2, timer = duration (the shared tail)
}

// The buffs (Start lists them). The target gets a record of the spell
// with the strength trunc(power x 16); the effect itself ends.
Next ApplyBuff(SpellEffect* e) {
    if (!e->target) return Next::Done;
    MapEffect record = e->HeldSpell()->EffectRecord();
    record.power = static_cast<std::uint8_t>(
        PowerScaledToInt(&e->HeldSpell()->damage, &Prototype(e)->damage, ConstPtr(worlddata::BuffScale())));
    record.ending = e->silentEnd;
    e->target->AddEffect(&record);
    return Next::Done;
}

// Fireworks (five numbered sounds).
Next StartFirework(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    float size = Power(e);
    e->CreateParticle(1, static_cast<int>(ParticleType::Geyser), centre, size, 1.0f, kZeroVector, nullptr);
    PrototypeLight(e, centre, PrototypeRange(e), kZeroVector);
    e->CreateSoundSequence(centre, kZeroVector, game::kEmptyText, "magic\\Firework\\%i.wav", 5);
    e->position = centre;
    return Next::Set;
}

// Clairvoyance. The particle uses the target point as it was; the light, the ground point.
Next StartClairvoyance(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    e->position = centre;
    e->CreateParticle(0, static_cast<int>(ParticleType::ClayRing), e->targetPoint, e->HeldSpell()->radius, 1.0f, kZeroVector, nullptr);
    PrototypeLight(e, centre, e->HeldSpell()->radius, kZeroVector);
    e->CreateSound("magic\\Clairvoyence\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// Possession (only the sound here).
Next StartPossession(SpellEffect* e) {
    if (!e->target) return Next::Done;
    e->CreateSound("magic\\Possession\\start.wav", e->target->EyePoint(), kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// VisionFog: a EffectShowVisibility linking the caster and the target.
Next StartVisionFog(SpellEffect* e) {
    if (!e->caster || !e->target) return Next::Done;
    int targetId = e->target->id;
    int casterId = e->caster->id;
    auto* shown = CreateObject<EffectShowVisibility>(e, ClassId::EffectShowVisibility);
    shown->SetUnits(casterId, targetId);
    SetPosition(shown, kZeroVector);
    e->SetVisual(shown);
    e->CreateSound("magic\\VisionFog\\start.wav", e->target->EyePoint(), kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// Healing. Heals the target's controller by the effect value; the sound and the light only
// when the target is not at full health (or +0xA4 is clear).
Next StartHealing(SpellEffect* e) {
    if (e->target->GetClassId() != ClassId::Unit) return Next::Done;   // no null check
    // Controller Heal (the amount's bits are pushed, MOV/PUSH).
    Unit* unit = TargetUnit(e);
    unit->Controller()->Heal(e->HeldSpell()->damage);
    float size = Power(e);
    e->CreateParticle(0, static_cast<int>(ParticleType::Healing), kZeroVector, size, 1.0f, kZeroVector, unit);
    bool full = unit->packedVitals.hitPoints == unit->packedVitals.maxHitPoints;
    if (e->silentEnd == 0 || !full) {
        e->CreateSound("magic\\Healing\\start.wav", unit->localPosition, kZeroVector, kVolume, kHearing);
        Vector3 rise{0.0f, 0.0f, LightRise(e)};
        float range = PrototypeRange(e);
        Vector3 color = PrototypeColor(e);
        e->CreatePointLight(e->targetPoint, color, -range, rise);
    }
    e->timer = 0x1E;
    e->state = 2;
    return Next::Set;
}

// Link between two different units that are both free; otherwise the caster complains (0x18).
Next StartLink(SpellEffect* e) {
    if (!IsUnit(e->target) || !IsUnit(e->caster)) return Next::Done;
    bool free = e->target->IsLargeUnit() == 0 && e->caster->IsLargeUnit() == 0 &&
                e->target->IsDead() == 0 && e->target != e->caster;
    if (!free) {
        if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(AckKind::LinkImpossible);
        return Next::Done;
    }
    Vector3 from = e->caster->EyePoint();
    e->position = from;
    e->CreateParticle(1, static_cast<int>(ParticleType::Link), from, 1.0f, 1.0f, kZeroVector, nullptr);
    if (e->visual) {
        Vector3 to = e->target->EyePoint();
        Vector3 direction;
        direction.x = to.x - from.x;
        direction.y = to.y - from.y;
        direction.z = to.z - from.z;
        e->Visual<EffectWall>()->SetDirection(direction);
    }
    e->CreateSoundSequence(from, kZeroVector, "magic\\Link\\start.wav", "magic\\Link\\circle.wav", -1);
    return Next::Set;
}

// Teleport: a particle where the caster stands and an inverted one at the target point.
Next StartTeleport(SpellEffect* e) {
    const Vector3* source = e->caster ? &e->caster->localPosition : &e->position;
    Vector3 point = *source;
    point.z = GroundPlus(e, kGridHeight, point, &point.z);
    e->CreateParticle(1, static_cast<int>(ParticleType::Teleport), point, 1.0f, 1.0f, kZeroVector, nullptr);
    point = e->targetPoint;
    point.z = GroundMinus(e, kGridHeight, point, ConstPtr(worlddata::TeleportDepth()));
    e->CreateParticle(0, static_cast<int>(ParticleType::Teleport), point, -1.0f, 1.0f, kZeroVector, nullptr);
    e->CreateSound("magic\\Teleport\\start.wav", point, kZeroVector, kVolume, kHearing);
    PrototypeLight(e, point, PrototypeRange(e), kZeroVector);
    int duration = static_cast<int>(e->HeldSpell()->duration);
    if (duration < 1) duration = 1;
    e->state = 2;
    e->timer = duration + 0x1E;
    return Next::Set;
}

// Charm. The target's controller is tamed by the caster until their sides (controller
// +0x1C, +0x24C) agree; a failed attempt makes the caster complain (0x1A).
Next StartCharm(SpellEffect* e) {
    if (e->target->GetClassId() != ClassId::Unit) return Next::Leave;   // no null check
    if (!IsUnit(e->caster)) return Next::Leave;
    UnitServer* tamed = ControllerOf(TargetUnit(e));
    UnitServer* tamer = ControllerOf(e->caster);
    float skill = tamer->stats.intelligence + e->HeldSpell()->damage;
    auto side = [](UnitServer* controller) { return controller->unit->player; };
    while (side(tamed) != side(tamer)) {
        if (tamed->TryTame(e->caster, skill) == 0) {
            if (IsUnit(e->caster)) ControllerOf(e->caster)->Shout(AckKind::LinkTameFailed);
            return Next::Done;
        }
    }
    e->CreateSound("magic\\Charm\\start.wav", e->target->localPosition, kZeroVector, kVolume, kHearing);
    return Next::Done;
}

// Campfire: the effect only moves to the ground point.
Next StartAtGround(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kGridHeight, centre, ConstPtr(worlddata::OneRef()));
    e->position = centre;
    return Next::Set;
}

} // namespace


void SpellEffect::Start(SpellType type) {
    flags &= ~1u;
    Vector3 from;
    if (caster) {
        from = caster->EyePoint();
    } else {
        from = position;
    }

    // The cases that end in the shared tail (state 2, timer = the spell's duration) return Set
    // without touching the state; the others set it themselves.
    Next next = Next::Done;
    bool durationTail = false;
    switch (type) {
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
        timer = static_cast<int>(HeldSpell()->duration);
    }
    UpdateFootprint(type, 1);
}

void SpellEffect::SetVisual(Effect* object) {
    Assign(this, &visual, object);
    world->AddTimedObject(visual);
}

void SpellEffect::UpdateFootprint(SpellType type, int stamp) {
    if (!visual) return;
    AIMap* aiMap = &world->aiMap;
    switch (type) {
    case SpellType::FireWall: {
        Vector3 direction;
        direction = (Visual<EffectWall>()->direction);
        g_spellFootprint.AlongDirection(aiMap, position, direction);
        break;
    }
    case SpellType::LightningWall: {
        Vector3 half;
        HalfSpan(Visual<EffectLightning>(), &half);
        g_spellFootprint.AlongDirection(aiMap, position, half);
        break;
    }
    case SpellType::AcidFog:
    case SpellType::Campfire: g_spellFootprint.DiscAt(aiMap, position, HeldSpell()->radius); break;
    default: return;
    }
    if (stamp) {
        aiMap->StampFootprint(world->tick, &g_spellFootprint);
        footprintStamped = 1;
    } else {
        aiMap->ClearFootprint(&g_spellFootprint);
        footprintStamped = 0;
    }
}

void SpellEffect::CreateArrow(const Vector3& from, EngineObject* missileTarget, int particleType, float power,
                               const char* soundName, const Vector3& color, float range) {
    auto* arrow = CreateObject<EffectArrow>(this, ClassId::EffectArrow);
    if (!arrow) return;
    arrow->lightColor = color;
    arrow->lightRange = range;
    SetPosition(arrow, from);
    arrow->SetTarget(missileTarget->id);
    arrow->particleType = particleType;
    arrow->particleSize = power;
    arrow->soundName = soundName;
    if (caster) {
        arrow->fromCaster = 1;
        arrow->casterId = caster->id;
    }
    Assign<Effect>(this, &visual, arrow);
    world->AddTimedObject(visual);
}

EffectParticle* SpellEffect::CreateParticle(int keep, int particleType, const Vector3& at, float size, float alpha,
                                              const Vector3& velocity, EngineObject* attachTo) {
    ClassId classId = (ParticleTypeOf(particleType) == ParticleType::Tendril || ParticleTypeOf(particleType) == ParticleType::Link) ? ClassId::EffectWall : ClassId::EffectParticle;
    auto* particle = CreateObject<EffectParticle>(this, classId);   // a EffectWall for the walls
    particle->type = particleType;
    SetPosition(particle, at);
    particle->size = size;
    particle->SetVelocity(velocity);
    particle->alpha = alpha;
    if (attachTo) particle->SetAttachedTo(attachTo->id);
    if (keep) {
        Assign<Effect>(this, &visual, particle);
        world->AddTimedObject(visual);
    } else {
        world->AddTimedObject(particle);
    }
    return particle;
}

void SpellEffect::CreateLightning(const Vector3& from, MapObject* boltTarget, EngineObject* source, float power,
                                   int flag) {
    if (!boltTarget) return;
    Vector3 to = boltTarget->localPosition;
    HeightPlusProduct(world, kGridHeight, to.x, to.y, &boltTarget->bounds.boxMax.z,
                      ConstPtr(worlddata::TargetHeight()), &to.z);
    auto* bolt = CreateObject<EffectLightning>(this, ClassId::EffectLightning);
    bolt->SetEnds(from, to, power * ConstAt(worlddata::BoltWidthScale()));
    bolt->SetObjects(source ? source->id : -1, boltTarget->id);
    bolt->SetFlag(static_cast<unsigned char>(flag));
    Assign<Effect>(this, &visual, bolt);
    world->AddTimedObject(visual);
}

void SpellEffect::CreateSound(const char* name, const Vector3& at, const Vector3& velocity, float volume, float hearing) {
    auto* effect = CreateObject<EffectSound>(this, ClassId::EffectSound);
    SetPosition(effect, at);
    effect->noFade = 1;
    effect->radius = hearing;
    effect->volume = volume;
    std::memcpy(effect->name, name, std::strlen(name) + 1);
    effect->SetVelocity(velocity);
    effect->looping = 0;
    world->AddTimedObject(effect);
}

void SpellEffect::CreateSoundSequence(const Vector3& at, const Vector3& velocity, const char* first,
                                       const char* pattern, int count) {
    std::string name;
    auto* effect = CreateObject<EffectSoundSeq>(this, ClassId::EffectSoundSeq);
    SetPosition(effect, at);
    effect->SetVelocity(velocity);
    effect->radius = kHearing;
    effect->volume = kVolume;
    effect->noFade = 1;
    effect->name = first;
    auto& names = effect->names;
    if (count < 1) {
        names.push_back(pattern);
    } else {
        for (int i = 1; i <= count; ++i) {
            name = str::Format(pattern, i);
            names.push_back(name);
        }
    }
    Assign<Effect>(this, &sound, effect);
    world->AddTimedObject(effect);
}

void SpellEffect::CreatePointLight(const Vector3& at, const Vector3& color, float range, const Vector3& velocity) {
    auto* effect = CreateObject<EffectPointLight>(this, ClassId::EffectPointLight);
    effect->color = color;
    SetPosition(effect, at);
    effect->range = range;
    effect->SetVelocity(velocity);
    effect->lightFlags = kPointLightFlags;
    Assign(this, &light, effect);
    world->AddTimedObject(effect);
}

// --- The cast spell's lifetime, start values and the object it forgets ----------------------------

namespace {

// A counted pointer's destructor and the counted pointer assignment of null: the old object loses a
// reference.
template <class T>
void Drop(T*& field) {
    T* old = field;
    field = nullptr;
    if (old) old->Release();
}

} // namespace

SpellEffect::SpellEffect() = default;

SpellEffect::~SpellEffect() {
    if (footprintStamped) UpdateFootprint(HeldSpell()->Type(), 0);
    if (visual) world->RemoveTimedObject(static_cast<unsigned>(visual->id));
    Drop(visual);
    ReleaseSound();
    ReleaseLight();
}

void SpellEffect::Init(WorldServer* newWorld, Spell* newSpell, Unit* newCaster, const Vector3& newPosition,
                        MapObject* newTarget, const Vector3& newTargetPoint, std::uint8_t newSilentEnd) {
    newSpell->CopyTo(HeldSpell());
    world = newWorld;
    position = newPosition;
    caster.Assign(newCaster);
    target.Assign(newTarget);
    targetPoint = newTargetPoint;
    state = 0;
    silentEnd = newSilentEnd;
}

bool SpellEffect::Forget(EngineObject* object) {
    if (target == object) return false;
    if (caster == object && caster) caster.Reset();
    return true;
}
