// Spell effects (SpellEffect, the world server's missiles): hitting units, saving and loading a cast spell, and the
// casting state with the helpers that create its effect objects (the later states: spell_effect_update.cpp).
// See docs/notes/items_spells.md.
#include "game/ai/ai_map.h"
#include "game/core/base_manager.h"
#include "game/combat/combat.h"
#include "game/console/console.h"
#include "game/core/core_streams.h"
#include "game/objects/map_object.h"
#include "game/ai/sector_map.h"
#include "game/script/script_lua_scheduler.h"
#include "game/spells/spell.h"
#include "game/spells/spell_effect_internal.h"
#include "game/units/unit.h"
#include "game/ai/unit_ai.h"
#include "game/units/unit_db.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"

#include "lib/str.h"

#include <cfloat>
#include <cstdint>
#include <string>

namespace {
constexpr float kTargetThreshold = 0.5f;
constexpr float kSplitDamage = 4.0f;
} // namespace

bool AcceptsAreaSpells(Unit* unit) {
    if (unit->sizeClass != 0) return true;
    float value = unit->GetPrototype()->height;
    return value < kTargetThreshold;
}

void SpellEffect::HitUnit(DamageInfo* hit, MapObject* unit) {
    if (caster && !HeldSpell()->CanAffect(caster, unit)) return;
    Unit* attacker = (flags & 1) ? nullptr : caster.Get();
    unit->ReceiveHit(hit, attacker, caster);
}

DamageInfo SpellEffect::TypedHit(DamageType type, float amount, int hitKind) {
    DamageInfo hit;
    hit.location = 6;
    hit.multiplier = 1.0f;
    hit.attackRoll = FLT_MAX;
    hit.backstabFactor = 1.0f;
    hit.Weight(type) = 1.0f;
    hit.amount = amount;
    hit.hitKind = hitKind;
    return hit;
}

void SpellEffect::SplitHit(DamageInfo* hit, int targets) {
    if (targets <= 1) return;
    hit->multiplier = kSplitDamage / static_cast<float>(targets);
    hit->amount = hit->multiplier * hit->amount;
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
    if (state == spellfx::kStateScripted) {
        stream->ReadValue(castKey);
        std::int32_t count = 0;
        stream->ReadValue(count);
        BaseManager* manager = world->TimedObjectManager();
        for (std::int32_t i = 0; i < count; ++i) {
            const int id = stream->ReadId();
            SpellOwned entry;
            stream->ReadValue(entry.ticks);
            stream->ReadValue(entry.payload);
            entry.object.Assign(manager->FindObjectAs<Effect>(id));
            if (entry.object) owned.push_back(std::move(entry));
        }
    }
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
    if (state == spellfx::kStateScripted) {
        stream->WriteValue(castKey);
        stream->WriteValue(static_cast<std::int32_t>(owned.size()));
        for (const SpellOwned& entry : owned) {
            stream->WriteId(entry.object);
            stream->WriteValue(entry.ticks);
            stream->WriteValue(entry.payload);
        }
    }
}

void SpellEffect::ResumeScripted() {
    ScriptSpellHost& spells = world->MainScript()->Runner().spells;
    if (state == spellfx::kStateMissile || state == spellfx::kStateLasting) {
        if (HeldSpell()->Behaviour() == SpellType::Scripted) spells.Resume(*this);
        return;
    }
    if (state != spellfx::kStateScripted || spells.Adopt(*this)) return;
    console::EndLine(console::g_consoleOut << "Script error: the save's Lua state has no cast " << castKey << " of "
                                           << HeldSpell()->GetPrototype()->script << "; it ends");
    state = spellfx::kStateEnded;
}

// --- The casting state -------------------------------------------------------------------------------

// A spell effect's casting state (SpellEffect::Update state 0) and the helpers that create its effect
// objects. A scripted spell's cast (the game's own spells are on scripts: assets/spells) starts its script; the cut
// spells still in code (possession, link, charm) start here. Also the cast spell's lifetime. See
// docs/notes/items_spells.md.

using namespace spellfx;

namespace {

// --- The spell types ----------------------------------------------------------------------------

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

// A scripted spell's cast: the zone's server state runs it (OnCast now), in the scripted state while it lives.
void StartScripted(SpellEffect* e) {
    e->state = e->world->MainScript()->Runner().spells.Cast(*e) ? kStateScripted : kStateEnded;
}

} // namespace

void SpellEffect::Start(SpellType type) {
    flags &= ~1u;
    // Every case sets the state itself: the lasting one or the end.
    switch (type) {
    case SpellType::Possession: StartPossession(this); break;
    case SpellType::Link: StartLink(this); break;
    case SpellType::Charm: StartCharm(this); break;
    case SpellType::Scripted: StartScripted(this); break;
    default: state = kStateEnded; break;   // Shapechange, Comprehend and None
    }
}

void SpellEffect::SetVisual(Effect* object) {
    Hold(this, visual, object);
    world->AddTimedObject(visual);
}

namespace {

// The cells of the effect's footprint, into the spells' footprint set.
void ShapeFootprint(SpellEffect* e) {
    AIMap* aiMap = &e->world->aiMap;
    if (e->footprint.disc) g_spellFootprint.DiscAt(aiMap, e->footprint.at, e->footprint.radius);
    else g_spellFootprint.AlongDirection(aiMap, e->footprint.at, e->footprint.along);
}

} // namespace

void SpellEffect::Block(const SpellFootprint& shape) {
    if (footprintStamped) Unblock();
    footprint = shape;
    ShapeFootprint(this);
    world->aiMap.StampFootprint(world->tick, &g_spellFootprint);
    footprintStamped = 1;
}

void SpellEffect::Unblock() {
    ShapeFootprint(this);
    world->aiMap.ClearFootprint(&g_spellFootprint);
    footprintStamped = 0;
}

bool SpellEffect::WallParticle(int particleType) {
    return particleType == static_cast<int>(ParticleType::Tendril) ||
           particleType == static_cast<int>(ParticleType::Link);
}

EffectArrow* SpellEffect::NewArrow(const Vector3& from, EngineObject* missileTarget, int particleType, float size,
                                   const char* soundName, const Vector3& color, float range, Unit* launcher) {
    auto* arrow = CreateObject<EffectArrow>(this, ClassId::EffectArrow);
    if (!arrow) return nullptr;
    arrow->lightColor = color;
    arrow->lightRange = range;
    SetPosition(arrow, from);
    arrow->SetTarget(missileTarget->id);
    arrow->particleType = particleType;
    arrow->particleSize = size;
    arrow->soundName = soundName;
    if (launcher) {
        arrow->fromCaster = 1;
        arrow->casterId = launcher->id;
    }
    return arrow;
}

EffectParticle* SpellEffect::CreateParticle(int keep, ParticleType particleType, const Vector3& at, float size, float alpha,
                                              const Vector3& velocity, EngineObject* attachTo) {
    EffectParticle* particle = NewParticle(static_cast<int>(particleType), at, size, alpha, velocity, attachTo);
    if (keep) {
        SetVisual(particle);
    } else {
        world->AddTimedObject(particle);
    }
    return particle;
}

EffectParticle* SpellEffect::NewParticle(int particleType, const Vector3& at, float size, float alpha,
                                         const Vector3& velocity, EngineObject* attachTo) {
    ClassId classId = WallParticle(particleType) ? ClassId::EffectWall : ClassId::EffectParticle;
    auto* particle = CreateObject<EffectParticle>(this, classId);   // a EffectWall for the walls
    particle->type = particleType;
    SetPosition(particle, at);
    particle->size = size;
    particle->SetVelocity(velocity);
    particle->alpha = alpha;
    if (attachTo) particle->SetAttachedTo(attachTo->id);
    return particle;
}

Vector3 SpellEffect::LightningEnd(MapObject* object) {
    Vector3 to = object->localPosition;
    to.z = world->GridHeightAt(to.x, to.y) + object->bounds.boxMax.z * kTargetHeight;
    return to;
}

EffectLightning* SpellEffect::NewLightning(const Vector3& from, const Vector3& to, int fromObject, int toObject,
                                           float width, int flag) {
    auto* bolt = CreateObject<EffectLightning>(this, ClassId::EffectLightning);
    bolt->SetEnds(from, to, width);
    bolt->SetObjects(fromObject, toObject);
    bolt->SetFlag(static_cast<unsigned char>(flag));
    return bolt;
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
    std::vector<std::string> names;
    if (count < 1) {
        names.push_back(pattern);
    } else {
        for (int i = 1; i <= count; ++i) names.push_back(str::Format(pattern, i));
    }
    EffectSoundSeq* effect = NewSoundSequence(at, velocity, first, std::move(names));
    Hold(this, sound, effect);
    world->AddTimedObject(effect);
}

EffectSoundSeq* SpellEffect::NewSoundSequence(const Vector3& at, const Vector3& velocity, const char* first,
                                              std::vector<std::string> names) {
    auto* effect = CreateObject<EffectSoundSeq>(this, ClassId::EffectSoundSeq);
    SetPosition(effect, at);
    effect->SetVelocity(velocity);
    effect->radius = kHearing;
    effect->volume = kVolume;
    effect->noFade = 1;
    effect->name = first;
    effect->names = std::move(names);
    return effect;
}

EffectPointLight* SpellEffect::NewPointLight(const Vector3& at, const Vector3& color, float range,
                                             const Vector3& velocity) {
    auto* effect = CreateObject<EffectPointLight>(this, ClassId::EffectPointLight);
    effect->color = color;
    SetPosition(effect, at);
    effect->range = range;
    effect->SetVelocity(velocity);
    effect->lightFlags = kPointLightFlags;
    return effect;
}

MapEffect SpellEffect::BuffRecord() {
    MapEffect record = HeldSpell()->EffectRecord();
    record.power = static_cast<std::uint8_t>(static_cast<std::int64_t>(Power(this) * kBuffScale));
    record.ending = silentEnd;
    return record;
}

bool SpellEffect::MoveUnit(Unit* unit, const Vector3& point, bool check) {
    if (check && !world->aiMap.IsPassableWithoutSelf(unit, point)) return false;
    world->aiMap.sectorMap.Move(unit, point);
    return true;
}

// --- The cast spell's lifetime, start values and the object it forgets ----------------------------

SpellEffect::SpellEffect() = default;

SpellEffect::~SpellEffect() {
    if (host) host->Forget(*this);
    if (footprintStamped) Unblock();
    ReleaseOwned();
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
    if (target == object) {
        if (state != kStateScripted || Prototype(this)->endsWithTarget) return false;
        target.Reset();
    }
    if (caster == object && caster) caster.Reset();
    return true;
}
