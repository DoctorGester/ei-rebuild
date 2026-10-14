// A spell effect's casting state (CSpellEffect::Update state 0, 0067F870) and the helpers that
// create its effect objects (0067EF40-0067F870). Every spell type starts differently: missiles
// get a CEffectArrow or a particle source and go to state 1, the lasting spells get particles,
// lights and sound sequences and go to state 2, the instant ones (lightning, the light strike,
// buffs, charm) deal or apply their effect here. See docs/notes/items_spells.md.
#include "spell_effect_internal.h"
#include "game_strings.h"

using namespace spellfx;

namespace {


// --- The spell types ----------------------------------------------------------------------------

// 0 (0x2011) and 0x28 (0x204E): the fire arrow, a missile at a unit target.
Next StartFireArrow(CSpellEffect* e, int type, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = Power(e);
    e->CreateArrow(&from, e->target, type == 0 ? 0x2011 : 0x204E, power, GameString(0x00795738), &color, range);
    e->CreateSound(0, GameString(0x0079571C), &e->position, &kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// 1 and 0x29: lightning from the caster to the target, hitting it at once.
Next StartLightning(CSpellEffect* e, int type, const Vector3& from) {
    float power = Power(e);
    e->CreateLightning(&from, e->target, e->caster, power, type == 0x29 ? 1 : 0);
    e->CreateSound(0, GameString(0x007956CC), &e->position, &kZeroVector, kVolume, kHearing);
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    Vector3 scratch;
    e->CreatePointLight(PositionOf(e->target, &scratch), &color, range, &kZeroVector);   // no null check
    DamageInfo hit;
    EffectGetDamage(e, nullptr, &hit);
    if (IsUnit(e->target)) EffectHitUnit(e, nullptr, &hit, e->target);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// 2: the acid ray, a missile (power x 0.4).
Next StartAcidRay(CSpellEffect* e, const Vector3& from) {
    if (!IsUnit(e->target)) return Next::Done;
    e->timer = 0x96;
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    float power = PowerScaled(e, kAcidRayScale);
    e->CreateArrow(&from, e->target, 0x2012, power, GameString(0x00795700), &color, range);
    e->CreateSound(0, GameString(0x007956E8), &e->position, &kZeroVector, kVolume, kHearing);
    e->state = 1;
    return Next::Set;
}

// 3: the fireball, a particle source flying at the target point; it explodes in state 1.
Next StartFireball(CSpellEffect* e, const Vector3& from) {
    float speed = e->speed;
    Vector3 velocity;
    velocity.x = e->targetPoint.x - e->position.x;
    velocity.y = e->targetPoint.y - e->position.y;
    velocity.z = e->targetPoint.z - e->position.z;
    velocity.z = 0.0f;
    float distance;
    PlanarLength(&velocity, &distance);
    if (!(speed > 0.0f)) speed = kDefaultSpeed;
    e->timer = FlightTicks(&distance, &speed) - 2;
    ScaleToSpeed(&velocity, &speed);

    float size = PowerScaled(e, kFireballScale);
    e->CreateParticle(1, 0x2000, &from, size, 1.0f, &velocity, nullptr);
    if (e->caster) At<int>(e->visual, 0x17C) = e->caster->id_;   // the particle's unit (no null check)
    e->targetPoint.z = GroundPlus(e, kWorldSlotGroundHeight, e->targetPoint, ConstPtr(kOne));
    ParticleSetTarget(e->visual, nullptr, &e->targetPoint);
    if (speed * ConstAt(kFireballNear) > distance) {
        e->CreateSound(0, GameString(0x007956B0), &e->position, &velocity, kVolume, kHearing);
    } else {
        e->CreateSoundSequence(&e->position, &velocity, GameString(0x00795678), GameString(0x00795694), -1, kVolume,
                               kHearing);
    }
    float range = PrototypeRange(e);
    Vector3 color = PrototypeColor(e);
    e->CreatePointLight(&from, &color, range, &velocity);
    e->state = 1;
    return Next::Set;
}

// 4: a lightning strike from 30 above the target point, hitting every unit in the radius.
Next StartLightStrike(CSpellEffect* e) {
    e->targetPoint.z = GroundMinus(e, kWorldSlotGroundHeight, e->targetPoint, ConstPtr(kStrikeDepth));
    e->position = e->targetPoint;
    Vector3 top = e->targetPoint;
    top.z = top.z + ConstAt(kStrikeHeight);
    float power = Power(e);
    CEngineObject* bolt = CreateObject(e, kClassLightning);
    LightningSetEnds(bolt, nullptr, &top, &e->position, power * ConstAt(kStrikeWidth));
    e->SetVisual(bolt);

    float alpha = Power(e);
    e->CreateParticle(0, 0x2030, &e->targetPoint, e->radius, alpha, &kZeroVector, nullptr);
    e->CreateSound(0, GameString(0x0079565C), &e->position, &kZeroVector, kLoud, kLoud);
    e->targetPoint.z = e->targetPoint.z + ConstAt(kLightAbove);
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, &e->targetPoint, PrototypeRange(e), &rise);

    HitAreaTargets(e);
    e->timer = 7;
    e->state = 2;
    return Next::Set;
}

// 5: the acid column at the target point.
Next StartAcidColumn(CSpellEffect* e) {
    e->position = e->targetPoint;
    e->CreateParticle(0, 0x200C, &e->targetPoint, 1.0f, 1.0f, &kZeroVector, nullptr);
    e->CreateSound(0, GameString(0x00795640), &e->targetPoint, &kZeroVector, kVolume, kHearing);
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, &e->targetPoint, PrototypeRange(e), &rise);
    e->timer = 0x1E;
    e->state = 2;
    return Next::Set;
}

// The direction from the caster to the wall's centre; straight along x when it has no ground part
// (both compare equal to 0, NaN included).
Vector3 WallDirection(const Vector3& centre, const Vector3& from) {
    Vector3 d;
    d.x = centre.x - from.x;
    d.y = centre.y - from.y;
    d.z = centre.z - from.z;
    auto zeroOrNaN = [](float v) { return !(v < 0.0f) && !(v > 0.0f); };
    if (zeroOrNaN(d.x) && zeroOrNaN(d.y)) d.x = 1.0f;
    return d;
}

// 6: the fire wall across the line from the caster to the target point.
Next StartFireWall(CSpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    float alpha = Power(e);
    float size = Power(e);
    e->CreateParticle(1, 0x2010, &centre, size, alpha, &kZeroVector, nullptr);
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(&direction, &e->radius, &span);
    if (e->visual) WallSetDirection(e->visual, nullptr, &span);
    e->position = centre;
    e->timer = e->targetCount;
    e->state = 2;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float radius = e->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, GameString(0x00795608), GameString(0x00795624), -1, kVolume, kHearing);
    return Next::Set;
}

// 7: the light wall, a lightning between the two ends of the same span.
Next StartLightWall(CSpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    Vector3 direction = WallDirection(centre, from);
    Vector3 span;
    WallSpan(&direction, &e->radius, &span);
    Vector3 a, b;
    a.x = e->targetPoint.x - span.x;
    a.y = e->targetPoint.y - span.y;
    a.z = e->targetPoint.z - span.z;
    b.x = span.x + e->targetPoint.x;
    b.y = span.y + e->targetPoint.y;
    b.z = span.z + e->targetPoint.z;
    a.z = GroundPlus(e, kWorldSlotGroundHeight, a, ConstPtr(kOne));
    b.z = GroundPlus(e, kWorldSlotGroundHeight, b, ConstPtr(kOne));
    CEngineObject* bolt = CreateObject(e, kClassLightning);
    LightningSetEnds(bolt, nullptr, &a, &b, 0.0f);
    e->SetVisual(bolt);
    e->position = centre;
    e->timer = e->targetCount;
    e->state = 2;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float radius = e->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, game::EmptyText(), GameString(0x007955EC), -1, kVolume, kHearing);
    return Next::Set;
}

// 8: the poison cloud at the target point.
Next StartPoisonCloud(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float alpha = Power(e);
    e->CreateParticle(1, 0x2007, &centre, e->radius, alpha, &kZeroVector, nullptr);
    e->CreateSoundSequence(&centre, &kZeroVector, GameString(0x007955BC), GameString(0x007955D4), -1, kVolume, kHearing);
    float radius = e->radius;
    float range = PrototypeRange(e);
    PrototypeLight(e, &centre, radius + range, &kZeroVector);
    e->position = centre;
    return Next::Set;   // state 2, timer = duration (the shared tail)
}

// 9-0x11, 0x13, 0x19, 0x1A, 0x1D-0x22, 0x25, 0x26: buffs. The target gets a record of the spell
// with the strength trunc(power x 16); the effect itself ends.
Next ApplyBuff(CSpellEffect* e) {
    if (!e->target) return Next::Done;
    BuffRecord record;
    BuffRecordInit(&record);
    std::memcpy(&record, &e->unknown_50, sizeof(record));
    record.strength = static_cast<std::uint8_t>(
        PowerScaledToInt(&e->damage, reinterpret_cast<const float*>(Prototype(e) + 0x2C), ConstPtr(kBuffScale)));
    record.flag = e->unknown_A4;
    ei::VCall<void>(e->target, kSlotApplyBuff, &record);
    return Next::Done;
}

// 0x12: fireworks (five numbered sounds).
Next StartFirework(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    float size = Power(e);
    e->CreateParticle(1, 0x2009, &centre, size, 1.0f, &kZeroVector, nullptr);
    PrototypeLight(e, &centre, PrototypeRange(e), &kZeroVector);
    e->CreateSoundSequence(&centre, &kZeroVector, game::EmptyText(), GameString(0x007955A4), 5, kVolume, kHearing);
    e->position = centre;
    return Next::Set;
}

// 0x14: clairvoyance. The particle uses the target point as it was; the light, the ground point.
Next StartClairvoyance(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    e->position = centre;
    e->CreateParticle(0, 0x2018, &e->targetPoint, e->radius, 1.0f, &kZeroVector, nullptr);
    PrototypeLight(e, &centre, e->radius, &kZeroVector);
    e->CreateSound(0, GameString(0x00795584), &e->position, &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// 0x15: possession (only the sound here).
Next StartPossession(CSpellEffect* e) {
    if (!e->target) return Next::Done;
    Vector3 scratch;
    e->CreateSound(0, GameString(0x007954EC), PositionOf(e->target, &scratch), &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// 0x17: vision fog: a CEffectShowVisibility linking the caster and the target.
Next StartVisionFog(CSpellEffect* e) {
    if (!e->caster || !e->target) return Next::Done;
    int targetId = e->target->id_;
    int casterId = e->caster->id_;
    CEngineObject* shown = CreateObject(e, kClassShowVisibility);
    ShowVisibilitySetObjects(shown, nullptr, casterId, targetId);
    SetPosition(shown, &kZeroVector);
    e->SetVisual(shown);
    Vector3 scratch;
    e->CreateSound(0, GameString(0x007954D0), PositionOf(e->target, &scratch), &kZeroVector, kVolume, kHearing);
    return Next::Set;
}

// 0x18: healing. Heals the target's controller by the effect value; the sound and the light only
// when the target is not at full health (or +0xA4 is clear).
Next StartHealing(CSpellEffect* e) {
    if (ClassId(e->target) != kClassUnit) return Next::Done;   // no null check
    ei::VCall<void>(ControllerOf(e->target), kControllerSlotHeal, Bits(e->damage));
    CEngineObject* unit = e->target;
    float size = Power(e);
    e->CreateParticle(0, 0x2006, &kZeroVector, size, 1.0f, &kZeroVector, unit);
    bool full = At<std::uint16_t>(e->target, 0x17E) == At<std::uint16_t>(e->target, 0x180);
    if (e->unknown_A4 == 0 || !full) {
        e->CreateSound(0, GameString(0x00795520), &At<Vector3>(e->target, kUnitPosition), &kZeroVector, kVolume, kHearing);
        Vector3 rise{0.0f, 0.0f, LightRise(e)};
        float range = PrototypeRange(e);
        Vector3 color = PrototypeColor(e);
        e->CreatePointLight(&e->targetPoint, &color, -range, &rise);
    }
    e->timer = 0x1E;
    e->state = 2;
    return Next::Set;
}

// 0x1B: link between two different units that are both free; otherwise the caster complains (0x18).
Next StartLink(CSpellEffect* e) {
    if (!IsUnit(e->target) || !IsUnit(e->caster)) return Next::Done;
    bool free = ei::VCall<int>(e->target, kSlotBusyA) == 0 && ei::VCall<int>(e->caster, kSlotBusyA) == 0 &&
                ei::VCall<int>(e->target, kSlotBusyB) == 0 && e->target != e->caster;
    if (!free) {
        if (IsUnit(e->caster)) ControllerShout(static_cast<CUnitServer*>(ControllerOf(e->caster)), nullptr, 0x18);
        return Next::Done;
    }
    Vector3 scratch;
    const Vector3* casterAt = PositionOf(e->caster, &scratch);
    Vector3 from = *casterAt;
    e->position = *casterAt;
    e->CreateParticle(1, 0x2014, &from, 1.0f, 1.0f, &kZeroVector, nullptr);
    if (e->visual) {
        const Vector3* to = PositionOf(e->target, &scratch);
        Vector3 direction;
        direction.x = to->x - from.x;
        direction.y = to->y - from.y;
        direction.z = to->z - from.z;
        WallSetDirection(e->visual, nullptr, &direction);
    }
    e->CreateSoundSequence(&from, &kZeroVector, GameString(0x00795554), GameString(0x0079556C), -1, kVolume, kHearing);
    return Next::Set;
}

// 0x1C: teleport: a particle where the caster stands and an inverted one at the target point.
Next StartTeleport(CSpellEffect* e) {
    const Vector3* source = e->caster ? &At<Vector3>(e->caster, kUnitPosition) : &e->position;
    Vector3 point = *source;
    point.z = GroundPlus(e, kWorldSlotGridHeight, point, &point.z);
    e->CreateParticle(1, 0x2019, &point, 1.0f, 1.0f, &kZeroVector, nullptr);
    point = e->targetPoint;
    point.z = GroundMinus(e, kWorldSlotGridHeight, point, ConstPtr(kTeleportDepth));
    e->CreateParticle(0, 0x2019, &point, -1.0f, 1.0f, &kZeroVector, nullptr);
    e->CreateSound(0, GameString(0x00795538), &point, &kZeroVector, kVolume, kHearing);
    PrototypeLight(e, &point, PrototypeRange(e), &kZeroVector);
    int duration = e->targetCount;
    if (duration < 1) duration = 1;
    e->state = 2;
    e->timer = duration + 0x1E;
    return Next::Set;
}

// 0x24: charm. The target's controller is tamed by the caster until their sides (controller
// +0x1C, +0x24C) agree; a failed attempt makes the caster complain (0x1A).
Next StartCharm(CSpellEffect* e) {
    if (ClassId(e->target) != kClassUnit) return Next::Leave;   // no null check
    if (!IsUnit(e->caster)) return Next::Leave;
    void* tamed = ControllerOf(e->target);
    void* tamer = ControllerOf(e->caster);
    float skill = At<float>(tamer, 0x200) + e->damage;
    auto side = [](void* controller) { return At<int>(At<void*>(controller, 0x1C), 0x24C); };
    while (side(tamed) != side(tamer)) {
        if (ControllerTame(static_cast<CUnitServer*>(tamed), nullptr, reinterpret_cast<CUnit*>(e->caster), skill) == 0) {
            if (IsUnit(e->caster)) ControllerShout(static_cast<CUnitServer*>(ControllerOf(e->caster)), nullptr, 0x1A);
            return Next::Done;
        }
    }
    e->CreateSound(0, GameString(0x00795508), &At<Vector3>(e->target, kUnitPosition), &kZeroVector, kVolume, kHearing);
    return Next::Done;
}

// 0x27: the effect only moves to the ground point.
Next StartAtGround(CSpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = GroundPlus(e, kWorldSlotGridHeight, centre, ConstPtr(kOne));
    e->position = centre;
    return Next::Set;
}

} // namespace

// --- CSpellEffect ---------------------------------------------------------------------------------

void CSpellEffect::Start(int type) {
    flags &= ~1u;
    Vector3 from;
    if (caster) {
        Vector3 scratch;
        from = *PositionOf(caster, &scratch);
    } else {
        from = position;
    }

    // The cases that end in the shared tail (state 2, timer = the spell's duration) return Set
    // without touching the state; the others set it themselves.
    Next next = Next::Done;
    bool durationTail = false;
    switch (type) {
    case 0x00:
    case 0x28: next = StartFireArrow(this, type, from); break;
    case 0x01:
    case 0x29: next = StartLightning(this, type, from); break;
    case 0x02: next = StartAcidRay(this, from); break;
    case 0x03: next = StartFireball(this, from); break;
    case 0x04: next = StartLightStrike(this); break;
    case 0x05: next = StartAcidColumn(this); break;
    case 0x06: next = StartFireWall(this, from); break;
    case 0x07: next = StartLightWall(this, from); break;
    case 0x08: next = StartPoisonCloud(this); durationTail = true; break;
    case 0x09: case 0x0A: case 0x0B: case 0x0C: case 0x0D: case 0x0E: case 0x0F: case 0x10: case 0x11:
    case 0x13: case 0x19: case 0x1A: case 0x1D: case 0x1E: case 0x1F: case 0x20: case 0x21: case 0x22:
    case 0x25: case 0x26: next = ApplyBuff(this); break;
    case 0x12: next = StartFirework(this); durationTail = true; break;
    case 0x14: next = StartClairvoyance(this); durationTail = true; break;
    case 0x15: next = StartPossession(this); durationTail = true; break;
    case 0x17: next = StartVisionFog(this); durationTail = true; break;
    case 0x18: next = StartHealing(this); break;
    case 0x1B: next = StartLink(this); durationTail = true; break;
    case 0x1C: next = StartTeleport(this); break;
    case 0x24: next = StartCharm(this); break;
    case 0x27: next = StartAtGround(this); durationTail = true; break;
    default: break;   // 0x16, 0x23 and above 0x29
    }
    if (next == Next::Done) {
        state = 3;
    } else if (next == Next::Set && durationTail) {
        state = 2;
        timer = targetCount;
    }
    UpdateFootprint(type, 1);
}
EI_REPLACE_METHOD(0x0067F870, CSpellEffect::Start);

void CSpellEffect::SetVisual(CEngineObject* object) {
    Assign(this, &visual, object);
    WorldAddTimed(world, nullptr, visual);
}
EI_REPLACE_METHOD(0x0067EF40, CSpellEffect::SetVisual);

void CSpellEffect::UpdateFootprint(int type, int stamp) {
    if (!visual) return;
    void* aiMap = world + 0x170;
    switch (type) {
    case 6: {
        Vector3 direction;
        std::memcpy(&direction, reinterpret_cast<char*>(visual) + 0x180, sizeof(direction));
        FootprintAlong(SpellFootprint(), nullptr, aiMap, &position, kHalfBits, &direction);
        break;
    }
    case 7: {
        Vector3 half;
        HalfSpan(visual, &half);
        FootprintAlong(SpellFootprint(), nullptr, aiMap, &position, kHalfBits, &half);
        break;
    }
    case 8:
    case 0x27: FootprintDisc(SpellFootprint(), nullptr, aiMap, &position, radius); break;
    default: return;
    }
    if (stamp) {
        AiMapStamp(aiMap, nullptr, At<int>(world, 0x64), SpellFootprint());
        footprintStamped = 1;
    } else {
        AiMapClear(aiMap, nullptr, SpellFootprint());
        footprintStamped = 0;
    }
}
EI_REPLACE_METHOD(0x0067EFC0, CSpellEffect::UpdateFootprint);

void CSpellEffect::CreateArrow(const Vector3* from, CEngineObject* missileTarget, int particleType, float power,
                               const char* soundName, const Vector3* color, float range) {
    CEngineObject* arrow = CreateObject(this, kClassArrow);
    if (!arrow) return;
    std::memcpy(&At<Vector3>(arrow, 0x6A4), color, 12);
    std::memcpy(&At<float>(arrow, 0x6B0), &range, 4);
    SetPosition(arrow, from);
    MissileSetTarget(arrow, nullptr, missileTarget->id_);
    At<int>(arrow, 0x6B4) = particleType;
    std::memcpy(&At<float>(arrow, 0x6B8), &power, 4);
    At<mfc::CString>(arrow, 0x6A0) = soundName;
    if (caster) {
        At<std::uint8_t>(arrow, 0x154) = 1;
        At<int>(arrow, 0x6BC) = caster->id_;
    }
    Assign(this, &visual, arrow);
    WorldAddTimed(world, nullptr, visual);
}
EI_REPLACE_METHOD(0x0067F170, CSpellEffect::CreateArrow);

CEngineObject* CSpellEffect::CreateParticle(int keep, int particleType, const Vector3* at, float size, float alpha,
                                            const Vector3* velocity, CEngineObject* attachTo) {
    int classId = (particleType == 0x2010 || particleType == 0x2014) ? kClassWall : kClassParticle;
    CEngineObject* particle = CreateObject(this, classId);
    At<int>(particle, 0x158) = particleType;
    SetPosition(particle, at);
    std::memcpy(&At<float>(particle, 0x15C), &size, 4);
    EffectSetVelocity(particle, nullptr, velocity);
    std::memcpy(&At<float>(particle, 0x160), &alpha, 4);
    if (attachTo) ParticleSetAttachedTo(particle, nullptr, attachTo->id_);
    if (keep) {
        Assign(this, &visual, particle);
        WorldAddTimed(world, nullptr, visual);
    } else {
        WorldAddTimed(world, nullptr, particle);
    }
    return particle;
}
EI_REPLACE_METHOD(0x0067F260, CSpellEffect::CreateParticle);

void CSpellEffect::CreateLightning(const Vector3* from, CEngineObject* boltTarget, CEngineObject* source, float power,
                                   int flag) {
    if (!boltTarget) return;
    Vector3 to = At<Vector3>(boltTarget, kUnitPosition);
    HeightPlusProduct(world, kWorldSlotGridHeight, Bits(to.x), Bits(to.y), &At<float>(boltTarget, 0x14C),
                      ConstPtr(kTargetHeight), &to.z);
    CEngineObject* bolt = CreateObject(this, kClassLightning);
    LightningSetEnds(bolt, nullptr, from, &to, power * ConstAt(kBoltWidthScale));
    LightningSetObjects(bolt, nullptr, source ? source->id_ : -1, boltTarget->id_);
    LightningSetFlag(bolt, nullptr, flag);
    Assign(this, &visual, bolt);
    WorldAddTimed(world, nullptr, visual);
}
EI_REPLACE_METHOD(0x0067F410, CSpellEffect::CreateLightning);

void CSpellEffect::CreateSound(int keep, const char* name, const Vector3* at, const Vector3* velocity, float volume,
                               float hearing) {
    CEngineObject* effect = CreateObject(this, kClassSound);
    SetPosition(effect, at);
    At<std::uint8_t>(effect, 0x258) = 1;
    std::memcpy(&At<float>(effect, 0x250), &hearing, 4);
    std::memcpy(&At<float>(effect, 0x24C), &volume, 4);
    std::memcpy(&At<char>(effect, 0x14C), name, std::strlen(name) + 1);
    EffectSetVelocity(effect, nullptr, velocity);
    At<int>(effect, 0x254) = keep;
    if (keep) Assign(this, &sound, effect);
    WorldAddTimed(world, nullptr, effect);
}
EI_REPLACE_METHOD(0x0067F580, CSpellEffect::CreateSound);

void CSpellEffect::CreateSoundSequence(const Vector3* at, const Vector3* velocity, const char* first,
                                       const char* pattern, int count, float volume, float hearing) {
    mfc::CString name;
    CEngineObject* effect = CreateObject(this, kClassSoundSequence);
    SetPosition(effect, at);
    EffectSetVelocity(effect, nullptr, velocity);
    std::memcpy(&At<float>(effect, 0x168), &hearing, 4);
    std::memcpy(&At<float>(effect, 0x164), &volume, 4);
    At<std::uint8_t>(effect, 0x16C) = 1;
    At<mfc::CString>(effect, 0x14C) = first;
    auto& names = At<console::CStringArray>(effect, 0x150);
    if (count < 1) {
        names.SetAtGrow(names.size, pattern);
    } else {
        for (int i = 1; i <= count; ++i) {
            name.Format(pattern, i);
            names.SetAtGrow(names.size, name);
        }
    }
    Assign(this, &sound, effect);
    WorldAddTimed(world, nullptr, effect);
}
EI_REPLACE_METHOD(0x0067F660, CSpellEffect::CreateSoundSequence);

void CSpellEffect::CreatePointLight(const Vector3* at, const Vector3* color, float range, const Vector3* velocity) {
    CEngineObject* effect = CreateObject(this, kClassPointLight);
    std::memcpy(&At<Vector3>(effect, 0x14C), color, 12);
    SetPosition(effect, at);
    std::memcpy(&At<float>(effect, 0x158), &range, 4);
    EffectSetVelocity(effect, nullptr, velocity);
    At<std::uint32_t>(effect, 0x160) = kPointLightFlags;
    Assign(this, &light, effect);
    WorldAddTimed(world, nullptr, effect);
}
EI_REPLACE_METHOD(0x0067F7C0, CSpellEffect::CreatePointLight);
