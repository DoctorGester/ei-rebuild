// The effects a mission script places in the world: point lights, particle sources, sound (FX)
// sources and lightning bolts (Script::pointLights, particles, fxSources, lightning). Saving
// and loading them with the savegame. See script.h and docs/notes/script.md.
#include "game/script/script.h"

#include "engine/core/base_manager.h"
#include "game/world/world.h"
#include "engine/particles/particle_data.h"
#include "game/effects/world_effect.h"
#include "game/app/singletons.h"

#include "lib/rounding.h"
#include "lib/str.h"

#include <climits>
#include <cmath>
#include <memory>
#include <string>

int ScriptParticleTypeByName(std::string_view name, int* lifetime) {
    if (std::shared_ptr<const effectdata::Record> effect = effectdata::g_effects.Find(name)) {
        *lifetime = effect->scriptLifetime;
        return effect->id;
    }
    *lifetime = -1;
    return 0;
}

namespace {

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

// CreateParticleSource(id, x, y, z, size, type name). A name the table lacks is a data effect's (Res\effects, with its
// [server] script_lifetime); a name neither knows makes nothing. A negative id takes the next automatic one; an
// existing source with the id gets the new effect.
void Script::CreateParticleSource(ScriptArgs args) {
    const int requestedId = lib::RoundEven(args.Float());
    const float x = args.Float();
    const float y = args.Float();
    const float z = args.Float();
    const float size = args.Float();
    std::string name = args.Text();
    int lifetime = -1;
    const int particleType = ScriptParticleTypeByName(name, &lifetime);
    if (particleType == 0) return;

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
