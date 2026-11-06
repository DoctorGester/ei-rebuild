// The effects a mission script places in the world: point lights, particle sources, sound (FX)
// sources and lightning bolts (Script::pointLights, particles, fxSources, lightning). Saving
// and loading them with the savegame. See script.h and docs/notes/script.md.
#include "game/script.h"

#include "game/core/base_manager.h"
#include "game/world/world.h"
#include "game/effects/world_effect.h"
#include "game/app/singletons.h"

#include "lib/rounding.h"
#include "lib/str.h"
#include "lib/text_case.h"

#include <climits>
#include <cmath>
#include <memory>
#include <string>

namespace {

// The particle types CreateParticleSource accepts: name, EffectParticle type, lifetime in ticks (-1 forever).
constexpr ScriptParticleType kScriptParticleTypes[64] = /*0078B120*/ {
    {"AcidRay", 0x2012, -1},
    {"FireArrow", 0x2011, -1},
    {"FireBall", 0x2000, -1},
    {"CampFire", 0x2001, -1},
    {"FireBlast", 0x2002, 200},
    {"LightningBlast", 0x2030, 200},
    {"Fire", 0x2003, -1},
    {"Smoke", 0x2004, -1},
    {"VulcanSmoke", 0x2005, -1},
    {"Healing", 0x2006, 200},
    {"PoisonFog", 0x2007, -1},
    {"Geyser", 0x2009, -1},
    {"Tornado", 0x200A, -1},
    {"Casting", 0x200B, 200},
    {"Nuke", 0x200C, 200},
    {"Mushroom", 0x200E, 200},
    {"Link", 0x2014, -1},
    {"BlueGas", 0x2013, -1},
    {"SphereAcid", 0x2015, -1},
    {"SphereElectricity", 0x2016, -1},
    {"SphereFire", 0x2017, -1},
    {"ClayRing", 0x2018, 200},
    {"Teleport", 0x2019, 200},
    {"Modifier1", 0x201B, 200},
    {"Modifier2", 0x201C, 200},
    {"Modifier3", 0x201D, 200},
    {"Modifier4", 0x201E, 200},
    {"Modifier5", 0x201F, 200},
    {"Modifier6", 0x2020, 200},
    {"Modifier7", 0x2021, 200},
    {"Modifier8", 0x2022, 200},
    {"Modifier9", 0x2023, 200},
    {"Modifier10", 0x2024, 200},
    {"Modifier11", 0x2025, 200},
    {"Modifier12", 0x2026, 200},
    {"Antimagic", 0x201A, -1},
    {"CastingFire", 0x2027, -1},
    {"CastingElectricity", 0x2028, -1},
    {"CastingAcid", 0x2029, -1},
    {"CastingDivination", 0x202A, -1},
    {"CastingIllusion", 0x202B, -1},
    {"CastingDomination", 0x202C, -1},
    {"CastingEnchantment", 0x202D, -1},
    {"CastingHealing", 0x202E, -1},
    {"ZoneExit", 0x2038, -1},
    {"Portal", 0x203E, -1},
    {"PortalStar", 0x203D, -1},
    {"Cylinder1", 0x203F, 200},
    {"Cylinder2", 0x2040, 200},
    {"FireStar", 0x2041, 20},
    {"AcidStar", 0x2042, 20},
    {"Sparks", 0x2043, 60},
    {"VisionStar1", 0x2044, -1},
    {"VisionStar2", 0x2045, -1},
    {"VisionStar3", 0x2046, -1},
    {"Regeneration", 0x2047, -1},
    {"Silence", 0x2048, -1},
    {"FeebleMind", 0x2049, -1},
    {"FeetCloud1", 0x204A, -1},
    {"FeetCloud2", 0x204B, -1},
    {"CurseStars", 0x204F, -1},
    {"CurseHolder", 0x2050, -1},
    {"StartTrans", 0x2051, 200},
    {"Transform", 0x2052, 200},
};

// The effect object saved under `id` (kNoObjectId: none), as the slot's effect class.
template <class T>
ScriptEffect<T> ReadObject(ByteStream* stream) {
    const int id = stream->ReadId();
    if (id == kNoObjectId) return {};
    return ScriptEffect<T>(g_serverWorld.timedObjectManager->FindObjectAs<T>(id));
}

// The first element of `list` with the id, null when none.
template <typename T>
T* FindId(std::vector<T>& list, int id) {
    for (T& element : list)
        if (element.id == id) return &element;
    return nullptr;
}

} // namespace

// Takes the effect object out of the world's timed objects and drops the reference to it.
template <class T>
void ScriptDropEffect(T* object) {
    g_serverWorld.RemoveTimedObject(object->id);
    object->Release();
}
template void ScriptDropEffect(EffectPointLight*);
template void ScriptDropEffect(EffectParticle*);
template void ScriptDropEffect(EffectSound*);
template void ScriptDropEffect(EffectLightning*);

EffectSound* ScriptNewSound(const Vector3& position, float volume, float radius, const std::string& name, bool looping) {
    auto* sound = g_serverWorld.timedObjectManager->CreateObjectAs<EffectSound>(ClassId::EffectSound, -1);
    sound->localPosition = position;
    sound->volume = volume;
    sound->radius = radius;
    sound->looping = looping;
    str::CopyInto(sound->name, name);
    g_serverWorld.AddTimedObject(sound);
    return sound;
}

// CreateFX(x, y, z, volume, radius, name), a sound played once.
void Script::CreateFx(ScriptArgs args) {
    const float x = args.Float();
    const float y = args.Float();
    const float z = args.Float();
    const float volume = args.Float();
    const float radius = args.Float();
    ScriptNewSound({x, y, z}, volume, radius, args.Text(), false);
}

// CreateFXSource / CreateRandomizedFXSource (script.h). A new source is appended empty. Every field
// is written below except, for CreateFXSource, spread, the delays and the countdown (stack residue in the
// game): the tick counts such a source down and replaces its sound at zero, so a zero countdown would
// replace the sound on every tick; INT_MAX keeps the sound, as a positive residue does.
void Script::CreateFxSource(ScriptArgs args, bool randomized) {
    int id = lib::RoundEven(args.Float());
    if (id < 0) id = nextFxId--;

    ScriptFxSource* source = FindId(fxSources, id);
    if (source) {
        source->object.reset();
    } else {
        source = &fxSources.emplace_back();
        source->countdown = INT_MAX;
    }

    source->id = id;
    const float x = args.Float();
    const float y = args.Float();
    const float z = args.Float();
    source->position = {x, y, z};
    source->volume = args.Float();
    source->radius = args.Float();
    source->name = args.Text();

    if (randomized) {
        source->minDelay = lib::RoundEven(args.Float());
        source->maxDelay = lib::RoundEven(args.Float());
        source->countdown = 0;
        source->spread = args.Float();
        source->idle = 1;
        return;
    }
    source->object = ScriptEffect<EffectSound>(
        ScriptNewSound(source->position, source->volume, source->radius, source->name, true));
    source->idle = 0;
}

// CreateLightning(id, x1, y1, z1, x2, y2, z2, width). A negative (or NaN) id takes the
// next automatic one; others are truncated. The bolt is always added, even when the id exists.
void Script::CreateLightning(ScriptArgs args) {
    const float id = args.Float();
    const float fromX = args.Float();
    const float fromY = args.Float();
    const float fromZ = args.Float();
    const float toX = args.Float();
    const float toY = args.Float();
    const float toZ = args.Float();
    const float width = args.Float();
    ScriptLightning& newLightning = lightning.emplace_back();

    if (id >= 0.0f) newLightning.id = lib::Truncate(id);
    else newLightning.id = nextParticleId--;    // also NaN

    auto* bolt = g_serverWorld.timedObjectManager->CreateObjectAs<EffectLightning>(ClassId::EffectLightning, -1);
    newLightning.object = ScriptEffect<EffectLightning>(bolt);
    Vector3 to{toX, toY, toZ};
    Vector3 from{fromX, fromY, fromZ};
    bolt->SetEnds(from, to, width);
    g_serverWorld.AddTimedObject(bolt);
}

// CreateParticleSource(id, x, y, z, size, type name). Unknown type names make nothing. A
// negative id takes the next automatic one; an existing source with the id gets the new effect.
void Script::CreateParticleSource(ScriptArgs args) {
    const int requestedId = lib::RoundEven(args.Float());
    const float x = args.Float();
    const float y = args.Float();
    const float z = args.Float();
    const float size = args.Float();
    std::string name = args.Text();
    const ScriptParticleType* type = nullptr;
    for (int i = 0; i < kScriptParticleTypeCount; ++i) {
        if (text::CompareIgnoringCase(kScriptParticleTypes[i].name, name) == 0) {
            type = &kScriptParticleTypes[i];
            break;
        }
    }
    if (!type || type->type == 0) return;
    int particleType = type->type;
    int lifetime = type->lifetime;

    int id = requestedId;
    if (id < 0) id = nextParticleId--;

    ScriptParticleSource* source = FindId(particles, id);
    if (source) source->object.reset();
    else source = &particles.emplace_back();

    auto* newParticles = g_serverWorld.timedObjectManager->CreateObjectAs<EffectParticle>(ClassId::EffectParticle, -1);
    newParticles->type = particleType;
    newParticles->localPosition = {x, y, z};
    source->object = ScriptEffect<EffectParticle>(newParticles);
    newParticles->size = size;
    g_serverWorld.AddTimedObject(newParticles);

    source->id = id;
    source->deleteAfterMove = 0;
    source->moving = 0;
    source->steps = 0;
    source->step.x = source->step.y = source->step.z = 0.0f;
    source->lifetime = lifetime;
}

// MoveParticleSource(id, x, y, z, steps, delete when done). The source moves towards the
// point in `steps` equal steps (one per tick); nothing when no source has the id.
void Script::MoveParticleSource(ScriptArgs args) {
    const int id = lib::RoundEven(args.Float());
    const float x = args.Float();
    const float y = args.Float();
    const float z = args.Float();
    const int steps = lib::RoundEven(args.Float());
    const int deleteAfterMove = lib::RoundEven(args.Float());
    ScriptParticleSource* source = FindId(particles, id);
    if (!source) return;
    source->moving = 1;
    source->deleteAfterMove = deleteAfterMove;
    source->steps = steps;
    const Vector3& at = source->object->localPosition;
    float inverse = 1.0f / static_cast<float>(source->steps);
    source->step = (Vector3{x, y, z} - at) * inverse;
}


void ScriptPointLight::Save(ByteStream* stream) const {
    stream->WriteId(object.get());
    stream->WriteValue(id);
    stream->WriteValue(removeWhenDone);
    stream->WriteValue(moving);
    stream->WriteValue(steps);
    stream->WriteValue(step);
}

void ScriptParticleSource::Save(ByteStream* stream) const {
    stream->WriteId(object.get());
    stream->WriteValue(id);
    stream->WriteValue(deleteAfterMove);
    stream->WriteValue(moving);
    stream->WriteValue(steps);
    stream->WriteValue(step);
    stream->WriteValue(lifetime);
}

void ScriptFxSource::Save(ByteStream* stream) const {
    stream->WriteId(object.get());
    stream->WriteValue(id);
    stream->WriteString(name);
    stream->WriteValue(position);
    stream->WriteValue(volume);
    stream->WriteValue(radius);
    stream->WriteValue(idle);
    stream->WriteValue(spread);
    stream->WriteValue(minDelay);
    stream->WriteValue(maxDelay);
    stream->WriteValue(countdown);
}

void ScriptLightning::Save(ByteStream* stream) const {
    stream->WriteId(object.get());
    stream->WriteValue(id);
}


void ScriptParticleSource::Load(ByteStream* stream) {
    object = ReadObject<EffectParticle>(stream);
    stream->ReadValue(id);
    stream->ReadValue(deleteAfterMove);
    stream->ReadValue(moving);
    stream->ReadValue(steps);
    stream->ReadValue(step);
    stream->ReadValue(lifetime);
}

void ScriptLightning::Load(ByteStream* stream) {
    object = ReadObject<EffectLightning>(stream);
    stream->ReadValue(id);
}

void ScriptPointLight::Load(ByteStream* stream) {
    object = ReadObject<EffectPointLight>(stream);
    stream->ReadValue(id);
    stream->ReadValue(removeWhenDone);
    stream->ReadValue(moving);
    stream->ReadValue(steps);
    stream->ReadValue(step);
}

void ScriptFxSource::Load(ByteStream* stream) {
    object = ReadObject<EffectSound>(stream);
    stream->ReadValue(id);
    name = stream->ReadString();
    stream->ReadValue(position);
    stream->ReadValue(volume);
    stream->ReadValue(radius);
    stream->ReadValue(idle);
    stream->ReadValue(spread);
    stream->ReadValue(minDelay);
    stream->ReadValue(maxDelay);
    stream->ReadValue(countdown);
}
