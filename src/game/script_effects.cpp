// The effects a mission script places in the world: point lights, particle sources, sound (FX)
// sources and lightning bolts (CScript::pointLights_, particles_, fxSources_, lightning_). Saving
// and loading them with the savegame. See script.h and docs/notes/script.md.
#include "script.h"

#include "base_manager.h"
#include "world.h"
#include "world_effect.h"
#include "game_singletons.h"

#include "ei/x87.h"
#include "lib/text_case.h"

#include <climits>
#include <cmath>
#include <cstring>
#include <memory>
#include <string>

namespace {

// The particle types CreateParticleSource accepts (the exe's table of 64: name, CEffectParticle type,
// lifetime in ticks, -1 forever).
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

// Class ids of the effect objects the script makes (CBaseManager::CreateObject).
constexpr int kClassSound = 0x48;       // CEffectSound
constexpr int kClassParticles = 0x4A;   // CEffectParticle
constexpr int kClassLightning = 0x4D;   // CEffectLightning
constexpr int kCommandCreateFxSource = 0x16;   // CreateRandomizedFXSource is 0x18

// The world's manager of timed objects (spells and effects; CWorld::timedObjectManager_).

void WriteInt(CStream* stream, int value) { stream->WriteValue(value); }

// The effect object saved under `id` (-1: none), as the slot's effect class.
template <class T>
ScriptEffect<T> ReadObject(CStream* stream) {
    int id;
    stream->ReadValue(id);
    if (id == -1) return {};
    return ScriptEffect<T>(g_serverWorld->timedObjectManager_->FindObjectAs<T>(id));
}

std::size_t Length(const char* s) { return std::strlen(s); }

// The first element of `list` with the id, null when none.
template <typename T>
T* FindId(std::list<T>& list, int id) {
    for (T& element : list)
        if (element.id == id) return &element;
    return nullptr;
}

} // namespace

// Takes the effect object out of the world's timed objects and drops the reference to it.
void ScriptDropEffect(CEngineObject* object) {
    g_serverWorld->RemoveTimedObject(object->id_);
    object->Release();
}

// CreateFX(x, y, z, volume, radius, name), a sound played once.
void CScript::ScriptCreateFx(const SSCCmd* cmd) {
    auto* sound = g_serverWorld->timedObjectManager_->CreateObjectAs<CEffectSound>(kClassSound, -1);
    const float* f = cmd->floatArgs.data();
    sound->localPosition_ = {f[0], f[1], f[2]};
    x87::CopyBits(sound->volume_, f[3]);
    x87::CopyBits(sound->radius_, f[4]);
    sound->looping_ = 0;
    const char* name = cmd->stringArgs[0].c_str();
    std::memcpy(sound->name_, name, Length(name) + 1);
    g_serverWorld->AddTimedObject(sound);
}

// CreateFXSource / CreateRandomizedFXSource (script.h). A new source is appended empty. Every field
// is written below except, for CreateFXSource, spread, the delays and the countdown (stack residue in the
// game): the tick counts such a source down and replaces its sound at zero, so a zero countdown would
// replace the sound on every tick; INT_MAX keeps the sound, as a positive residue does.
void CScript::ScriptCreateFxSource(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data();
    int id = x87::RoundToInt(f[0]);
    if (id < 0) id = nextFxId_--;

    ScriptFxSource* source = FindId(fxSources_, id);
    if (source) {
        source->object.reset();
    } else {
        source = &fxSources_.emplace_back();
        source->countdown = INT_MAX;
    }

    source->id = id;
    source->position.x = f[1];
    source->position.y = f[2];
    source->position.z = f[3];
    x87::CopyBits(source->volume, f[4]);
    x87::CopyBits(source->radius, f[5]);
    source->name = cmd->stringArgs[0];

    if (cmd->id != kCommandCreateFxSource) {
        source->minDelay = x87::RoundToInt(f[6]);
        source->maxDelay = x87::RoundToInt(f[7]);
        source->countdown = 0;
        x87::CopyBits(source->spread, f[8]);
        source->idle = 1;
        return;
    }
    auto* sound = g_serverWorld->timedObjectManager_->CreateObjectAs<CEffectSound>(kClassSound, -1);
    source->object = ScriptEffect<CEffectSound>(sound);
    sound->localPosition_ = {source->position.x, source->position.y, source->position.z};
    x87::CopyBits(sound->volume_, source->volume);
    x87::CopyBits(sound->radius_, source->radius);
    const char* name = source->name.c_str();
    std::memcpy(sound->name_, name, Length(name) + 1);
    g_serverWorld->AddTimedObject(sound);
    source->idle = 0;
}

// CreateLightning(id, x1, y1, z1, x2, y2, z2, width). A negative (or NaN) id takes the
// next automatic one; others are truncated. The bolt is always added, even when the id exists.
void CScript::ScriptCreateLightning(const SSCCmd* cmd) {
    ScriptLightning& lightning = lightning_.emplace_back();

    const float* f = cmd->floatArgs.data();
    if (f[0] >= 0.0f) lightning.id = static_cast<int>(f[0]);
    else lightning.id = nextParticleId_--;   // also NaN

    auto* bolt = g_serverWorld->timedObjectManager_->CreateObjectAs<CEffectLightning>(kClassLightning, -1);
    lightning.object = ScriptEffect<CEffectLightning>(bolt);
    Vector3 to{f[4], f[5], f[6]};
    Vector3 from{f[1], f[2], f[3]};
    bolt->SetEnds(&from, &to, f[7]);
    g_serverWorld->AddTimedObject(bolt);
}

// CreateParticleSource(id, x, y, z, size, type name). Unknown type names make nothing. A
// negative id takes the next automatic one; an existing source with the id gets the new effect.
void CScript::ScriptCreateParticleSource(const SSCCmd* cmd) {
    std::string name = cmd->stringArgs[0];
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

    const float* f = cmd->floatArgs.data();
    int id = x87::RoundToInt(f[0]);
    if (id < 0) id = nextParticleId_--;

    ScriptParticleSource* source = FindId(particles_, id);
    if (source) source->object.reset();
    else source = &particles_.emplace_back();

    auto* particles = g_serverWorld->timedObjectManager_->CreateObjectAs<CEffectParticle>(kClassParticles, -1);
    particles->type_ = particleType;
    particles->localPosition_ = {f[1], f[2], f[3]};
    source->object = ScriptEffect<CEffectParticle>(particles);
    x87::CopyBits(particles->size_, f[4]);
    g_serverWorld->AddTimedObject(particles);

    source->id = id;
    source->deleteAfterMove = 0;
    source->moving = 0;
    source->steps = 0;
    source->step.x = source->step.y = source->step.z = 0.0f;
    source->lifetime = lifetime;
}

// MoveParticleSource(id, x, y, z, steps, delete when done). The source moves towards the
// point in `steps` equal steps (one per tick); nothing when no source has the id.
void CScript::ScriptMoveParticleSource(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data();
    ScriptParticleSource* source = FindId(particles_, x87::RoundToInt(f[0]));
    if (!source) return;
    source->moving = 1;
    source->deleteAfterMove = x87::RoundToInt(f[5]);
    source->steps = x87::RoundToInt(f[4]);
    const Vector3& at = source->object->localPosition_;
    float inverse = 1.0f / static_cast<float>(source->steps);
    source->step.x = (f[1] - at.x) * inverse;
    source->step.y = (f[2] - at.y) * inverse;
    source->step.z = (f[3] - at.z) * inverse;
}


void ScriptPointLight::Save(CStream* stream) const {
    WriteInt(stream, object.SavedId());
    WriteInt(stream, id);
    WriteInt(stream, removeWhenDone);
    WriteInt(stream, moving);
    WriteInt(stream, steps);
    stream->WriteValue(step);
}

void ScriptParticleSource::Save(CStream* stream) const {
    WriteInt(stream, object.SavedId());
    WriteInt(stream, id);
    WriteInt(stream, deleteAfterMove);
    WriteInt(stream, moving);
    WriteInt(stream, steps);
    stream->WriteValue(step);
    WriteInt(stream, lifetime);
}

void ScriptFxSource::Save(CStream* stream) const {
    WriteInt(stream, object.SavedId());
    WriteInt(stream, id);
    stream->WriteString(name);
    stream->WriteValue(position);
    stream->WriteValue(volume);
    stream->WriteValue(radius);
    WriteInt(stream, idle);
    stream->WriteValue(spread);
    WriteInt(stream, minDelay);
    WriteInt(stream, maxDelay);
    WriteInt(stream, countdown);
}

void ScriptLightning::Save(CStream* stream) const {
    WriteInt(stream, object.SavedId());
    WriteInt(stream, id);
}


void ScriptPointLight::Load(CStream* stream) {
    object = ReadObject<CEffectPointLight>(stream);
    stream->ReadValue(id);
    stream->ReadValue(removeWhenDone);
    stream->ReadValue(moving);
    stream->ReadValue(steps);
    stream->ReadValue(step);
}

void ScriptFxSource::Load(CStream* stream) {
    object = ReadObject<CEffectSound>(stream);
    stream->ReadValue(id);
    stream->ReadInto(&name);
    stream->ReadValue(position);
    stream->ReadValue(volume);
    stream->ReadValue(radius);
    stream->ReadValue(idle);
    stream->ReadValue(spread);
    stream->ReadValue(minDelay);
    stream->ReadValue(maxDelay);
    stream->ReadValue(countdown);
}
