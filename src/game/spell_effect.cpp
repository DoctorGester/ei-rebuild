// Spell effects (SpellEffect, the world server's missiles): hitting units, saving and loading a cast spell, and the
// casting state with the helpers that create its effect objects (the later states: spell_effect_update.cpp).
// See docs/notes/items_spells.md.
#include "game/ai_map.h"
#include "game/core/base_manager.h"
#include "game/combat.h"
#include "game/core/core_streams.h"
#include "game/map_object.h"
#include "game/sector_map.h"
#include "game/spell.h"
#include "game/spell_effect_internal.h"
#include "game/unit.h"
#include "game/unit_ai.h"
#include "game/unit_db.h"
#include "game/world.h"
#include "game/world_effect.h"

#include "lib/float_select.h"
#include "lib/str.h"

#include <bit>
#include <cfloat>
#include <cstdint>
#include <cstring>
#include <string>

namespace {
constexpr float kTargetThreshold = 0.5f;
constexpr float kWallHalfWidth = 1.0f;
constexpr float kSplitDamage = 4.0f;

// Whether area spells hit `unit`: units with a size class, and those whose prototype's height is below 0.5 (or
// NaN).
bool AcceptsAreaSpells(Unit* unit) {
    if (unit->sizeClass != 0) return true;
    float value = unit->GetPrototype()->height;
    return value < kTargetThreshold;
}
} // namespace

void SpellEffect::HitUnit(DamageInfo* hit, MapObject* unit) {
    if (caster && !HeldSpell()->CanAffect(caster, unit)) return;
    Unit* attacker = (flags & 1) ? nullptr : caster.Get();
    unit->ReceiveHit(hit, attacker, caster);
}

void SpellEffect::HitTarget() {
    DamageInfo hit;
    GetDamage(&hit);
    if (spellfx::IsUnit(target)) HitUnit(&hit, target);
}

void SpellEffect::HitUnitsInRadius() {
    DamageInfo hit;
    GetDamage(&hit);
    for (Unit* unit : world->aiMap.sectorMap.FindUnitsInRadius(position, HeldSpell()->radius)) {
        if (!AcceptsAreaSpells(unit)) continue;
        HitUnit(&hit, unit);
    }
}

void SpellEffect::HitUnitsAlong(const Vector3& direction) {
    DamageInfo hit;
    GetDamage(&hit);

    // The wall's normal in the ground plane: s = 2 / sqrt(x^2 + y^2) (rounded to float), normal = (s * -y,
    // s * x, 0). The normal has length 2, so the test against 1 accepts the units within 0.5 of the plane.
    float x = direction.x;
    float minusY = -direction.y;
    float scale = 2.0f / std::sqrt(x * x + minusY * minusY);
    float normalX = scale * minusY;
    float normalY = scale * x;
    float normalZ = 0.0f;

    for (Unit* unit : world->aiMap.sectorMap.FindUnitsInRadius(position, HeldSpell()->radius)) {
        const Unit* at = unit;
        Vector3 d = at->localPosition - position;
        float distance = Dot(Vector3{d.x, d.y, d.z}, Vector3{normalX, normalY, normalZ});
        distance = AbsBits(distance);
        if (distance > kWallHalfWidth) continue;
        if (!AcceptsAreaSpells(unit)) continue;
        HitUnit(&hit, unit);
    }
}

namespace {

// What a spell type deals: its damage type, and a hitKind of 1 for the spells that burn an area over time.
struct SpellDamageKind {
    DamageType type = {};
    int hitKind = 0;
};

constexpr SpellDamageKind kFireHit{DamageType::Fire, 0};
constexpr SpellDamageKind kLightningHit{DamageType::Lightning, 0};
constexpr SpellDamageKind kAcidHit{DamageType::Acid, 0};
constexpr SpellDamageKind kFireArea{DamageType::Fire, 1};
constexpr SpellDamageKind kLightningArea{DamageType::Lightning, 1};
constexpr SpellDamageKind kAcidArea{DamageType::Acid, 1};

// The damage kind of `type`; null for the spells that deal no damage.
const SpellDamageKind* DamageKindOf(SpellType type) {
    switch (type) {
    case SpellType::FireArrow: case SpellType::Fireball: case SpellType::RickMagic: return &kFireHit;
    case SpellType::Lightning: case SpellType::InvokeLightning: case SpellType::CurseMagic: return &kLightningHit;
    case SpellType::AcidRay: return &kAcidHit;
    case SpellType::AcidColumn: case SpellType::AcidFog: return &kAcidArea;
    case SpellType::FireWall: case SpellType::Campfire: return &kFireArea;
    case SpellType::LightningWall: return &kLightningArea;
    default: return nullptr;
    }
}

} // namespace

void SpellEffect::GetDamage(DamageInfo* out) {
    std::memset(out, 0, sizeof(*out));
    out->location = 6;
    out->multiplier = 1.0f;
    out->attackRoll = FLT_MAX;
    out->backstabFactor = 1.0f;
    out->calledShot = 0;

    const Spell* record = HeldSpell();
    if (const SpellDamageKind* kind = DamageKindOf(record->Type())) {
        out->Weight(kind->type) = 1.0f;
        out->amount = record->damage;
        out->hitKind = kind->hitKind;
    }

    // The duration is the number of targets the damage is split between.
    int targets = static_cast<int>(record->duration);
    if (targets > 1) {
        out->multiplier = kSplitDamage / static_cast<float>(targets);
        out->amount = out->multiplier * out->amount;
    }
}

// --- Saving and loading ------------------------------------------------------------------------------

// Saving and loading a cast spell (SpellEffect, the world server's missiles list) with the world's
// save game. See spell.h and docs/notes/items_spells.md ("Spell effects").

namespace {

// One of the three counted effect objects by the id read (kNoObjectId none), looked up in the world's timed
// object manager.
template <class T>
void LoadEffectObject(ByteStream* stream, World* world, Ref<T>& field) {
    BaseManager* manager = world->TimedObjectManager();
    const int id = stream->ReadId();
    field.Assign(id == kNoObjectId ? nullptr : manager->FindObjectAs<T>(id));
}

} // namespace

void SpellEffect::Load(ByteStream* stream) {
    world = g_loadingProjectileWorld;
    stream->ReadValue(position);
    LoadRef(caster, stream, world);
    LoadRef(target, stream, world);
    stream->ReadValue(targetPoint);
    stream->ReadValue(state);
    HeldSpell()->Load(stream);
    std::uint32_t packed = 0;
    stream->ReadValue(packed);
    HeldSpell()->prototypeId = packed;
    HeldSpell()->RequireRecords();
    World* server = world;
    LoadEffectObject(stream, server, visual);
    LoadEffectObject(stream, server, sound);
    LoadEffectObject(stream, server, light);
    stream->ReadValue(timer);
    stream->ReadValue(flags);
    stream->ReadValue(position);
    UpdateFootprint(HeldSpell()->Type(), 1);
}

void SpellEffect::Save(ByteStream* stream) {
    stream->WriteValue(position);
    stream->WriteId(caster);
    stream->WriteId(target);
    stream->WriteValue(targetPoint);
    stream->WriteValue(state);
    HeldSpell()->Save(stream);
    stream->WriteValue(HeldSpell()->prototypeId);
    stream->WriteId(visual);
    stream->WriteId(sound);
    stream->WriteId(light);
    stream->WriteValue(timer);
    stream->WriteValue(flags);
    stream->WriteValue(position);
}

// --- The casting state -------------------------------------------------------------------------------

// A spell effect's casting state (SpellEffect::Update state 0) and the helpers that create its effect
// objects. Every spell type starts differently: missiles get a EffectArrow or a particle source and go to
// state 1, the lasting spells get particles, lights and sound sequences and go to state 2, the instant ones
// (lightning, the light strike, buffs, charm) deal or apply their effect here. Also the cast spell's
// lifetime. See docs/notes/items_spells.md.

using namespace spellfx;

namespace {

// --- The spell types ----------------------------------------------------------------------------

// FireArrow (particle FireArrow) and RickMagic (RikArrowSmoke): the fire arrow, a missile at a unit target.
void StartFireArrow(SpellEffect* e, SpellType type, const Vector3& from) {
    if (!IsUnit(e->target)) {
        e->state = kStateEnded;
        return;
    }
    e->timer = 150;
    float range = Prototype(e)->lightRange;
    Vector3 color = Prototype(e)->lightColor;
    float power = Power(e);
    ParticleType trail = type == SpellType::FireArrow ? ParticleType::FireArrow : ParticleType::RikArrowSmoke;
    e->CreateArrow(from, e->target, trail, power, "magic\\Firearrow\\circle.wav", color, range);
    e->CreateSound("magic\\Firearrow\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    e->state = kStateMissile;
}

// Lightning and CurseMagic: lightning from the caster to the target, hitting it at once.
void StartLightning(SpellEffect* e, SpellType type, const Vector3& from) {
    float power = Power(e);
    e->CreateLightning(from, e->target, e->caster, power, type == SpellType::CurseMagic ? 1 : 0);
    e->CreateSound("magic\\lightning\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    float range = Prototype(e)->lightRange;
    Vector3 color = Prototype(e)->lightColor;
    if (e->target) e->CreatePointLight(e->target->EyePoint(), color, range, kZeroVector);
    e->HitTarget();
    e->timer = 7;
    e->state = kStateLasting;
}

// AcidRay: a missile (power x 0.4).
void StartAcidRay(SpellEffect* e, const Vector3& from) {
    if (!IsUnit(e->target)) {
        e->state = kStateEnded;
        return;
    }
    e->timer = 150;
    float range = Prototype(e)->lightRange;
    Vector3 color = Prototype(e)->lightColor;
    float power = Power(e) * kAcidRayScale;
    e->CreateArrow(from, e->target, ParticleType::AcidRay, power, "magic\\Acidray\\circle.wav", color,
                   range);
    e->CreateSound("magic\\Acidray\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    e->state = kStateMissile;
}

// Fireball: a particle source flying at the target point; it explodes in state 1.
void StartFireball(SpellEffect* e, const Vector3& from) {
    float speed = e->HeldSpell()->speed;
    Vector3 velocity = e->targetPoint - e->position;
    velocity.z = 0.0f;
    float distance = Length2D(XY(velocity));
    speed = speed > 0.0f ? speed : kDefaultSpeed;
    e->timer = FlightTicks(distance, speed) - 2;
    velocity = ScaledToSpeed(velocity, speed);

    float size = Power(e) * kFireballScale;
    // The visual.
    EffectParticle* particle = e->CreateParticle(1, ParticleType::FireBall, from, size, 1.0f, velocity, nullptr);
    if (e->caster) particle->unitId = e->caster->id;     // the particle's unit
    e->targetPoint.z = e->world->RawSurfaceHeightAt(e->targetPoint.x, e->targetPoint.y) + 1.0f;
    particle->SetTarget(e->targetPoint);
    if (speed * kFireballNear > distance) {
        e->CreateSound("magic\\FireBall\\start.wav", e->position, velocity, kVolume, kHearing);
    } else {
        e->CreateSoundSequence(e->position, velocity, "magic\\Fireball\\start.wav", "magic\\Fireball\\circle.wav", -1);
    }
    float range = Prototype(e)->lightRange;
    Vector3 color = Prototype(e)->lightColor;
    e->CreatePointLight(from, color, range, velocity);
    e->state = kStateMissile;
}

// InvokeLightning: a lightning strike from 30 above the target point, hitting every unit in the radius.
void StartLightStrike(SpellEffect* e) {
    e->targetPoint.z = e->world->RawSurfaceHeightAt(e->targetPoint.x, e->targetPoint.y) - kStrikeDepth;
    e->position = e->targetPoint;
    Vector3 top = e->targetPoint;
    top.z += kStrikeHeight;
    float power = Power(e);
    auto* bolt = CreateObject<EffectLightning>(e, ClassId::EffectLightning);
    bolt->SetEnds(top, e->position, power * kStrikeWidth);
    e->SetVisual(bolt);

    float alpha = Power(e);
    e->CreateParticle(0, ParticleType::LightningBlast, e->targetPoint, e->HeldSpell()->radius, alpha, kZeroVector,
                      nullptr);
    e->CreateSound("magic\\InvokeLight\\start.wav", e->position, kZeroVector, kLoud, kLoud);
    e->targetPoint.z += kLightAbove;
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, e->targetPoint, Prototype(e)->lightRange, rise);

    e->HitUnitsInRadius();
    e->timer = 7;
    e->state = kStateLasting;
}

// AcidColumn: the column at the target point.
void StartAcidColumn(SpellEffect* e) {
    e->position = e->targetPoint;
    e->CreateParticle(0, ParticleType::Nuke, e->targetPoint, 1.0f, 1.0f, kZeroVector, nullptr);
    e->CreateSound("magic\\Acidcolumn\\start.wav", e->targetPoint, kZeroVector, kVolume, kHearing);
    Vector3 rise{0.0f, 0.0f, LightRise(e)};
    PrototypeLight(e, e->targetPoint, Prototype(e)->lightRange, rise);
    e->timer = 30;
    e->state = kStateLasting;
}

// The direction from the caster to the wall's centre; straight along x when it has no ground part
// (both 0).
Vector3 WallDirection(const Vector3& centre, const Vector3& from) {
    Vector3 d = centre - from;
    if (XY(d) == Vector2{0.0f, 0.0f}) d.x = 1.0f;
    return d;
}

// FireWall: the wall across the line from the caster to the target point.
void StartFireWall(SpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    float alpha = Power(e);
    float size = Power(e);
    e->CreateParticle(1, ParticleType::Tendril, centre, size, alpha, kZeroVector, nullptr);
    Vector3 direction = WallDirection(centre, from);
    Vector3 span = WallSpan(direction, e->HeldSpell()->radius);
    if (e->visual) e->Visual<EffectWall>()->SetDirection(span);
    e->position = centre;
    EnterLasting(e);
    centre.z = e->world->GridHeightAt(centre.x, centre.y) + 1.0f;
    float radius = e->HeldSpell()->radius;
    float range = Prototype(e)->lightRange;
    PrototypeLight(e, centre, radius + range, kZeroVector);
    e->CreateSoundSequence(centre, kZeroVector, "magic\\Firewall\\start.wav", "magic\\Firewall\\circle.wav", -1);
}

// LightningWall: the light wall, a lightning between the two ends of the same span.
void StartLightWall(SpellEffect* e, const Vector3& from) {
    Vector3 centre = e->targetPoint;
    Vector3 direction = WallDirection(centre, from);
    Vector3 span = WallSpan(direction, e->HeldSpell()->radius);
    Vector3 a, b;
    a = e->targetPoint - span;
    b = span + e->targetPoint;
    a.z = e->world->RawSurfaceHeightAt(a.x, a.y) + 1.0f;
    b.z = e->world->RawSurfaceHeightAt(b.x, b.y) + 1.0f;
    auto* bolt = CreateObject<EffectLightning>(e, ClassId::EffectLightning);
    bolt->SetEnds(a, b, 0.0f);
    e->SetVisual(bolt);
    e->position = centre;
    EnterLasting(e);
    centre.z = e->world->GridHeightAt(centre.x, centre.y) + 1.0f;
    float radius = e->HeldSpell()->radius;
    float range = Prototype(e)->lightRange;
    PrototypeLight(e, centre, radius + range, kZeroVector);
    e->CreateSoundSequence(centre, kZeroVector, "", "magic\\Lightwall\\circle.wav", -1);
}

// AcidFog: the poison cloud at the target point.
void StartPoisonCloud(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = e->world->GridHeightAt(centre.x, centre.y) + 1.0f;
    float alpha = Power(e);
    e->CreateParticle(1, ParticleType::PoisonFog, centre, e->HeldSpell()->radius, alpha, kZeroVector,
                      nullptr);
    e->CreateSoundSequence(centre, kZeroVector, "magic\\poison\\start.wav", "magic\\poison\\circle.wav", -1);
    float radius = e->HeldSpell()->radius;
    float range = Prototype(e)->lightRange;
    PrototypeLight(e, centre, radius + range, kZeroVector);
    e->position = centre;
    EnterLasting(e);
}

// The buffs (Start lists them). The target gets a record of the spell
// with the strength trunc(power x 16); the effect itself ends.
void ApplyBuff(SpellEffect* e) {
    e->state = kStateEnded;
    if (!e->target) return;
    MapEffect record = e->HeldSpell()->EffectRecord();
    record.power = static_cast<std::uint8_t>(static_cast<std::int64_t>(Power(e) * kBuffScale));
    record.ending = e->silentEnd;
    e->target->AddEffect(&record);
}

// Fireworks (five numbered sounds).
void StartFirework(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = e->world->GridHeightAt(centre.x, centre.y) + 1.0f;
    float size = Power(e);
    e->CreateParticle(1, ParticleType::Geyser, centre, size, 1.0f, kZeroVector, nullptr);
    PrototypeLight(e, centre, Prototype(e)->lightRange, kZeroVector);
    e->CreateSoundSequence(centre, kZeroVector, "", "magic\\Firework\\%i.wav", 5);
    e->position = centre;
    EnterLasting(e);
}

// Clairvoyance. The particle uses the target point as it was; the light, the ground point.
void StartClairvoyance(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = e->world->GridHeightAt(centre.x, centre.y) + 1.0f;
    e->position = centre;
    e->CreateParticle(0, ParticleType::ClayRing, e->targetPoint, e->HeldSpell()->radius, 1.0f, kZeroVector,
                      nullptr);
    PrototypeLight(e, centre, e->HeldSpell()->radius, kZeroVector);
    e->CreateSound("magic\\Clairvoyence\\start.wav", e->position, kZeroVector, kVolume, kHearing);
    EnterLasting(e);
}

// Possession (only the sound here).
// Unreached in the shipped game: CastSpell refuses possession, only a saved projectile of that type gets here.
void StartPossession(SpellEffect* e) {
    if (!e->target) {
        e->state = kStateEnded;
        return;
    }
    e->CreateSound("magic\\Possession\\start.wav", e->target->EyePoint(), kZeroVector, kVolume, kHearing);
    EnterLasting(e);
}

// VisionFog: a EffectShowVisibility linking the caster and the target.
void StartVisionFog(SpellEffect* e) {
    if (!e->caster || !e->target) {
        e->state = kStateEnded;
        return;
    }
    int targetId = e->target->id;
    int casterId = e->caster->id;
    auto* shown = CreateObject<EffectShowVisibility>(e, ClassId::EffectShowVisibility);
    shown->SetUnits(casterId, targetId);
    SetPosition(shown, kZeroVector);
    e->SetVisual(shown);
    e->CreateSound("magic\\VisionFog\\start.wav", e->target->EyePoint(), kZeroVector, kVolume, kHearing);
    EnterLasting(e);
}

// Healing. Heals the target's controller by the effect value; the sound and the light only
// when the target is not at full health (or silentEnd is clear).
void StartHealing(SpellEffect* e) {
    if (!IsUnit(e->target)) {
        e->state = kStateEnded;
        return;
    }
    Unit* unit = UnitOrNull(e->target);
    unit->controller->Heal(e->HeldSpell()->damage);
    float size = Power(e);
    e->CreateParticle(0, ParticleType::Healing, kZeroVector, size, 1.0f, kZeroVector, unit);
    bool full = unit->packedVitals.hitPoints == unit->packedVitals.maxHitPoints;
    if (e->silentEnd == 0 || !full) {
        e->CreateSound("magic\\Healing\\start.wav", unit->localPosition, kZeroVector, kVolume, kHearing);
        Vector3 rise{0.0f, 0.0f, LightRise(e)};
        float range = Prototype(e)->lightRange;
        Vector3 color = Prototype(e)->lightColor;
        e->CreatePointLight(e->targetPoint, color, -range, rise);
    }
    e->timer = 30;
    e->state = kStateLasting;
}

// Link between two different units that are both free; otherwise the caster complains (LinkImpossible).
// Unreached in the shipped game: CastSpell refuses the link, only a saved projectile of that type gets here.
void StartLink(SpellEffect* e) {
    if (!IsUnit(e->target) || !IsUnit(e->caster)) {
        e->state = kStateEnded;
        return;
    }
    bool free = e->target->IsLargeUnit() == 0 && e->caster->IsLargeUnit() == 0 &&
                e->target->IsDead() == 0 && e->target != e->caster;
    if (!free) {
        ControllerOf(e->caster)->Shout(AckKind::LinkImpossible);
        e->state = kStateEnded;
        return;
    }
    Vector3 from = e->caster->EyePoint();
    e->position = from;
    e->CreateParticle(1, ParticleType::Link, from, 1.0f, 1.0f, kZeroVector, nullptr);
    if (e->visual) {
        Vector3 to = e->target->EyePoint();
        Vector3 direction = to - from;
        e->Visual<EffectWall>()->SetDirection(direction);
    }
    e->CreateSoundSequence(from, kZeroVector, "magic\\Link\\start.wav", "magic\\Link\\circle.wav", -1);
    EnterLasting(e);
}

// Teleport: a particle where the caster stands and an inverted one at the target point.
void StartTeleport(SpellEffect* e) {
    const Vector3* source = e->caster ? &e->caster->localPosition : &e->position;
    Vector3 point = *source;
    point.z = e->world->GridHeightAt(point.x, point.y) + point.z;
    e->CreateParticle(1, ParticleType::Teleport, point, 1.0f, 1.0f, kZeroVector, nullptr);
    point = e->targetPoint;
    point.z = e->world->GridHeightAt(point.x, point.y) - kTeleportDepth;
    e->CreateParticle(0, ParticleType::Teleport, point, -1.0f, 1.0f, kZeroVector, nullptr);
    e->CreateSound("magic\\Teleport\\start.wav", point, kZeroVector, kVolume, kHearing);
    PrototypeLight(e, point, Prototype(e)->lightRange, kZeroVector);
    int duration = static_cast<int>(e->HeldSpell()->duration);
    if (duration < 1) duration = 1;
    e->state = kStateLasting;
    e->timer = duration + 30;
}

// Charm. The target is tamed by the caster until their units' players agree; a failed attempt makes the caster
// complain (AckKind::LinkTameFailed).
// Unreached in the shipped game: CastSpell refuses charm, only a saved projectile of that type gets here.
void StartCharm(SpellEffect* e) {
    if (!IsUnit(e->target) || !IsUnit(e->caster)) return;   // the state stays: Start runs again
    UnitServer* tamed = ControllerOf(UnitOrNull(e->target));
    UnitServer* tamer = ControllerOf(e->caster);
    float skill = tamer->stats.intelligence + e->HeldSpell()->damage;
    auto side = [](UnitServer* controller) { return controller->unit->player; };
    while (side(tamed) != side(tamer)) {
        if (tamed->TryTame(e->caster, skill) == 0) {
            ControllerOf(e->caster)->Shout(AckKind::LinkTameFailed);
            e->state = kStateEnded;
            return;
        }
    }
    e->CreateSound("magic\\Charm\\start.wav", e->target->localPosition, kZeroVector, kVolume, kHearing);
    e->state = kStateEnded;
}

// Campfire: the effect only moves to the ground point.
void StartAtGround(SpellEffect* e) {
    Vector3 centre = e->targetPoint;
    centre.z = e->world->GridHeightAt(centre.x, centre.y) + 1.0f;
    e->position = centre;
    EnterLasting(e);
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

    // Every case sets the state itself: the missile, the lasting one or the end.
    switch (type) {
    case SpellType::FireArrow:
    case SpellType::RickMagic: StartFireArrow(this, type, from); break;
    case SpellType::Lightning:
    case SpellType::CurseMagic: StartLightning(this, type, from); break;
    case SpellType::AcidRay: StartAcidRay(this, from); break;
    case SpellType::Fireball: StartFireball(this, from); break;
    case SpellType::InvokeLightning: StartLightStrike(this); break;
    case SpellType::AcidColumn: StartAcidColumn(this); break;
    case SpellType::FireWall: StartFireWall(this, from); break;
    case SpellType::LightningWall: StartLightWall(this, from); break;
    case SpellType::AcidFog: StartPoisonCloud(this); break;
    case SpellType::ProtectionFromFire: case SpellType::ProtectionFromLightning: case SpellType::ProtectionFromAcid:
    case SpellType::EagleSight: case SpellType::Infravision: case SpellType::DetectLife: case SpellType::Invisibility:
    case SpellType::Silence: case SpellType::Lichdom: case SpellType::Stench: case SpellType::Stun:
    case SpellType::Antimagic: case SpellType::Strengthen: case SpellType::Weaken: case SpellType::Regeneration:
    case SpellType::Feeblemind: case SpellType::Speed: case SpellType::Slow: case SpellType::Enlarge:
    case SpellType::Shrink: ApplyBuff(this); break;
    case SpellType::Fireworks: StartFirework(this); break;
    case SpellType::Clairvoyance: StartClairvoyance(this); break;
    case SpellType::Possession: StartPossession(this); break;
    case SpellType::VisionFog: StartVisionFog(this); break;
    case SpellType::Healing: StartHealing(this); break;
    case SpellType::Link: StartLink(this); break;
    case SpellType::Teleport: StartTeleport(this); break;
    case SpellType::Charm: StartCharm(this); break;
    case SpellType::Campfire: StartAtGround(this); break;
    default: state = kStateEnded; break;   // Shapechange, Comprehend and above CurseMagic
    }
    UpdateFootprint(type, 1);
}

void SpellEffect::SetVisual(Effect* object) {
    Hold(this, visual, object);
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
        const EffectLightning* bolt = Visual<EffectLightning>();
        Vector3 half = (bolt->to - bolt->from) * 0.5f;
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

void SpellEffect::CreateArrow(const Vector3& from, EngineObject* missileTarget, ParticleType particleType, float power,
                               const char* soundName, const Vector3& color, float range) {
    auto* arrow = CreateObject<EffectArrow>(this, ClassId::EffectArrow);
    if (!arrow) return;
    arrow->lightColor = color;
    arrow->lightRange = range;
    SetPosition(arrow, from);
    arrow->SetTarget(missileTarget->id);
    arrow->particleType = static_cast<int>(particleType);
    arrow->particleSize = power;
    arrow->soundName = soundName;
    if (caster) {
        arrow->fromCaster = 1;
        arrow->casterId = caster->id;
    }
    SetVisual(arrow);
}

EffectParticle* SpellEffect::CreateParticle(int keep, ParticleType particleType, const Vector3& at, float size, float alpha,
                                              const Vector3& velocity, EngineObject* attachTo) {
    const bool wall = particleType == ParticleType::Tendril || particleType == ParticleType::Link;
    ClassId classId = wall ? ClassId::EffectWall : ClassId::EffectParticle;
    auto* particle = CreateObject<EffectParticle>(this, classId);   // a EffectWall for the walls
    particle->type = static_cast<int>(particleType);
    SetPosition(particle, at);
    particle->size = size;
    particle->SetVelocity(velocity);
    particle->alpha = alpha;
    if (attachTo) particle->SetAttachedTo(attachTo->id);
    if (keep) {
        SetVisual(particle);
    } else {
        world->AddTimedObject(particle);
    }
    return particle;
}

void SpellEffect::CreateLightning(const Vector3& from, MapObject* boltTarget, EngineObject* source, float power,
                                   int flag) {
    if (!boltTarget) return;
    Vector3 to = boltTarget->localPosition;
    to.z = world->GridHeightAt(to.x, to.y) + boltTarget->bounds.boxMax.z * kTargetHeight;
    auto* bolt = CreateObject<EffectLightning>(this, ClassId::EffectLightning);
    bolt->SetEnds(from, to, power * kBoltWidthScale);
    bolt->SetObjects(source ? source->id : -1, boltTarget->id);
    bolt->SetFlag(static_cast<unsigned char>(flag));
    SetVisual(bolt);
}

void SpellEffect::CreateSound(const char* name, const Vector3& at, const Vector3& velocity, float volume,
                              float hearing) {
    auto* effect = CreateObject<EffectSound>(this, ClassId::EffectSound);
    SetPosition(effect, at);
    effect->noFade = 1;
    effect->radius = hearing;
    effect->volume = volume;
    str::CopyInto(effect->name, name);
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
    Hold(this, sound, effect);
    world->AddTimedObject(effect);
}

void SpellEffect::CreatePointLight(const Vector3& at, const Vector3& color, float range, const Vector3& velocity) {
    auto* effect = CreateObject<EffectPointLight>(this, ClassId::EffectPointLight);
    effect->color = color;
    SetPosition(effect, at);
    effect->range = range;
    effect->SetVelocity(velocity);
    effect->lightFlags = kPointLightFlags;
    Hold(this, light, effect);
    world->AddTimedObject(effect);
}

// --- The cast spell's lifetime, start values and the object it forgets ----------------------------

SpellEffect::SpellEffect() = default;

SpellEffect::~SpellEffect() {
    if (footprintStamped) UpdateFootprint(HeldSpell()->Type(), 0);
    ReleaseVisual();
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
    state = kStateCasting;
    silentEnd = newSilentEnd;
}

bool SpellEffect::Forget(EngineObject* object) {
    if (target == object) return false;
    if (caster == object && caster) caster.Reset();
    return true;
}
