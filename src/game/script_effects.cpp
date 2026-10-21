// The effects a mission script places in the world: point lights, particle sources, sound (FX)
// sources and lightning bolts (CScript::pointLights_, particles_, fxSources_, lightning_). Saving
// and loading them with the savegame. See script.h and docs/notes/script.md.
#include "script.h"

#include "base_manager.h"
#include "originals_library.h"
#include "world.h"
#include "world_effect.h"
#include "game_singletons.h"

#include "ei/hook.h"
#include "ei/x87.h"
#include "lib/crt.h"
#include "lib/mfc_list.h"

#include <climits>
#include <cmath>
#include <cstring>

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

// The world's manager of timed objects (spells and effects; CWorld::manager48_, 007AFFA8).
CBaseManager* EffectManager() { return TheServerWorld()->manager48_; }

// The id saved for an effect object: -1 for none.
int ObjectId(const CEngineObject* object) { return object ? object->id_ : -1; }

void WriteInt(ScriptStream* stream, int value) { stream->Write(&value, 4); }

// The effect object saved under `id` (-1: none), with a reference taken.
CEngineObject* ReadObject(ScriptStream* stream) {
    int id;
    stream->Read(&id, 4);
    if (id == -1) return nullptr;
    CEngineObject* object = EffectManager()->FindObject(id);
    if (object) ++object->refCount_;
    return object;
}

std::size_t Length(const char* s) { return std::strlen(s); }

// Takes the effect object out of the world's timed objects and drops the reference to it.
void DropEffect(CEngineObject** slot) {
    if (CEngineObject* object = *slot) {
        TheServerWorld()->RemoveTimedObject(object->id_);
        (*slot)->Release();
    }
    *slot = nullptr;
}

} // namespace

// ---- Element destructors ----------------------------------------------------------------------------
// One body for each element type: the effect object dropped (DropEffect).

// 004B17F0
void ScriptPointLight::ScriptPointLightDestruct() { DropEffect(&object); }
// 004B1820
void ScriptLightning::ScriptLightningDestruct() { DropEffect(&object); }
// 004B1850
void ScriptParticleSource::ScriptParticleSourceDestruct() { DropEffect(&object); }
// 004B1880 (the name is the caller's to destroy)
void ScriptFxSource::ScriptFxSourceDropSound() { DropEffect(&object); }
// 004BD540: the FX source's destructor body (the sound dropped, then the name).
void ScriptFxSource::ScriptFxSourceDestruct() {
    DropEffect(&object);
    name.~CString();
}

// 004CD690: DestructElements<ScriptLightning>.
void __stdcall ScriptLightningElementsDestruct(ScriptLightning* first, int count) {
    for (; count != 0; --count, ++first) DropEffect(&first->object);
}

// ---- Commands -----------------------------------------------------------------------------------------

// 004BD260: CreateFX(x, y, z, volume, radius, name), a sound played once.
void CScript::ScriptCreateFx(const SSCCmd* cmd) {
    auto* sound = static_cast<CEffectSound*>(EffectManager()->CreateObject(kClassSound, -1));
    const float* f = cmd->floatArgs.data;
    sound->localPosition_ = {f[0], f[1], f[2]};
    std::memcpy(&sound->volume_, &f[3], 4);
    std::memcpy(&sound->radius_, &f[4], 4);
    sound->looping_ = 0;
    const char* name = cmd->stringArgs.data[0];
    std::memcpy(sound->name_, name, Length(name) + 1);
    TheServerWorld()->AddTimedObject(sound);
}

// 004BD310: CreateFXSource / CreateRandomizedFXSource (script.h). A new source's element is the
// list's NewNode's (zeroed, the name empty); the original then copies a stack temporary over it
// whose object is null, whose name is empty and whose other fields were never written. Every field
// is written below except, for CreateFXSource, spread, the delays and the countdown: the tick
// (004BF000) counts such a source down and replaces its sound at zero, so a zero countdown would
// replace the sound on every tick; INT_MAX keeps the sound, as a positive residue does.
void CScript::ScriptCreateFxSource(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    int id = x87::RoundToInt(f[0]);
    if (id < 0) id = nextFxId_--;

    mfc::CList<ScriptFxSource>& list = fxSources_;
    ScriptFxSource* source = nullptr;
    for (auto* node = list.head; node; node = node->next) {
        if (node->data.id == id) {
            source = &node->data;
            break;
        }
    }
    if (source) {
        DropEffect(&source->object);
    } else {
        auto* node = mfc::impl::NewNodeAs<mfc::impl::FxSourceList>(&list, list.tail, nullptr);   // 004CD5B0
        node->data.object = nullptr;
        node->data.countdown = INT_MAX;
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
        source = &node->data;
    }

    source->id = id;
    source->position.x = f[1];
    source->position.y = f[2];
    source->position.z = f[3];
    std::memcpy(&source->volume, &f[4], 4);
    std::memcpy(&source->radius, &f[5], 4);
    source->name = cmd->stringArgs.data[0];

    if (cmd->id != kCommandCreateFxSource) {
        source->object = nullptr;
        source->minDelay = x87::RoundToInt(f[6]);
        source->maxDelay = x87::RoundToInt(f[7]);
        source->countdown = 0;
        std::memcpy(&source->spread, &f[8], 4);
        source->idle = 1;
        return;
    }
    auto* sound = static_cast<CEffectSound*>(EffectManager()->CreateObject(kClassSound, -1));
    source->object = sound;
    sound->localPosition_ = {source->position.x, source->position.y, source->position.z};
    std::memcpy(&sound->volume_, &source->volume, 4);
    std::memcpy(&sound->radius_, &source->radius, 4);
    const char* name = source->name;
    std::memcpy(sound->name_, name, Length(name) + 1);
    TheServerWorld()->AddTimedObject(sound);
    source->idle = 0;
    ++source->object->refCount_;
}

// 004BCFF0: CreateLightning(id, x1, y1, z1, x2, y2, z2, width). A negative (or NaN) id takes the
// next automatic one; others are truncated. The bolt is always added, even when the id exists.
void CScript::ScriptCreateLightning(const SSCCmd* cmd) {
    mfc::CList<ScriptLightning>& list = lightning_;
    auto* node = mfc::impl::NewNodeAs<mfc::impl::LightningList>(&list, list.tail, nullptr);   // 004CD620
    node->data.object = nullptr;
    node->data.id = -1;
    if (list.tail) list.tail->next = node;
    else list.head = node;
    list.tail = node;

    const float* f = cmd->floatArgs.data;
    if (f[0] >= 0.0f) node->data.id = static_cast<int>(f[0]);
    else node->data.id = nextParticleId_--;   // also NaN

    auto* bolt = static_cast<CEffectLightning*>(EffectManager()->CreateObject(kClassLightning, -1));
    node->data.object = bolt;
    ++bolt->refCount_;
    Vector3 to{f[4], f[5], f[6]};
    Vector3 from{f[1], f[2], f[3]};
    bolt->SetEnds(&from, &to, f[7]);
    TheServerWorld()->AddTimedObject(bolt);
}

// 004BD9A0: CreateParticleSource(id, x, y, z, size, type name). Unknown type names make nothing. A
// negative id takes the next automatic one; an existing source with the id gets the new effect.
void CScript::ScriptCreateParticleSource(const SSCCmd* cmd) {
    mfc::CString name(cmd->stringArgs.data[0]);
    const ScriptParticleType* type = nullptr;
    for (int i = 0; i < kScriptParticleTypeCount; ++i) {
        if (libcrt::Stricmp(kScriptParticleTypes[i].name, name) == 0) {
            type = &kScriptParticleTypes[i];
            break;
        }
    }
    if (!type || type->type == 0) return;
    int particleType = type->type;
    int lifetime = type->lifetime;

    const float* f = cmd->floatArgs.data;
    int id = x87::RoundToInt(f[0]);
    if (id < 0) id = nextParticleId_--;

    mfc::CList<ScriptParticleSource>& list = particles_;
    ScriptParticleSource* source = nullptr;
    for (auto* node = list.head; node; node = node->next) {
        if (node->data.id == id) {
            source = &node->data;
            break;
        }
    }
    if (source) {
        DropEffect(&source->object);
    } else {
        // The original copies a temporary whose fields are all written below.
        auto* node = mfc::impl::NewNodeAs<mfc::impl::ParticleSourceList>(&list, list.tail, nullptr);   // 004CD540
        node->data.object = nullptr;
        if (list.tail) list.tail->next = node;
        else list.head = node;
        list.tail = node;
        source = &node->data;
    }

    auto* particles = static_cast<CEffectParticle*>(EffectManager()->CreateObject(kClassParticles, -1));
    particles->type_ = particleType;
    particles->localPosition_ = {f[1], f[2], f[3]};
    ++particles->refCount_;
    std::memcpy(&particles->size_, &f[4], 4);
    TheServerWorld()->AddTimedObject(particles);

    source->object = particles;
    source->id = id;
    source->deleteAfterMove = 0;
    source->moving = 0;
    source->steps = 0;
    source->step.x = source->step.y = source->step.z = 0.0f;
    source->lifetime = lifetime;
}

// 004BDBB0: MoveParticleSource(id, x, y, z, steps, delete when done). The source moves towards the
// point in `steps` equal steps (one per tick); nothing when no source has the id.
void CScript::ScriptMoveParticleSource(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    int id = x87::RoundToInt(f[0]);
    ScriptParticleSource* source = nullptr;
    for (auto* node = particles_.head; node; node = node->next) {
        if (node->data.id == id) {
            source = &node->data;
            break;
        }
    }
    if (!source) return;
    source->moving = 1;
    source->deleteAfterMove = x87::RoundToInt(f[5]);
    source->steps = x87::RoundToInt(f[4]);
    const Vector3& at = static_cast<CEffectParticle*>(source->object)->localPosition_;
    float inverse = 1.0f / static_cast<float>(source->steps);
    source->step.x = (f[1] - at.x) * inverse;
    source->step.y = (f[2] - at.y) * inverse;
    source->step.z = (f[3] - at.z) * inverse;
}

// ---- Saving ---------------------------------------------------------------------------------------

// 004B1940
void ScriptPointLight::ScriptPointLightSave(ScriptStream* stream) const {
    WriteInt(stream, ObjectId(object));
    WriteInt(stream, id);
    WriteInt(stream, removeWhenDone);
    WriteInt(stream, moving);
    WriteInt(stream, steps);
    stream->Write(&step, 12);
}

// 004B1A80
void ScriptParticleSource::ScriptParticleSourceSave(ScriptStream* stream) const {
    WriteInt(stream, ObjectId(object));
    WriteInt(stream, id);
    WriteInt(stream, deleteAfterMove);
    WriteInt(stream, moving);
    WriteInt(stream, steps);
    stream->Write(&step, 12);
    WriteInt(stream, lifetime);
}

// 004B1C50
void ScriptFxSource::ScriptFxSourceSave(ScriptStream* stream) const {
    WriteInt(stream, ObjectId(object));
    WriteInt(stream, id);
    const char* text = name;
    stream->Write(text, static_cast<std::uint32_t>(Length(text) + 1));
    stream->Write(&position, 12);
    stream->Write(&volume, 4);
    stream->Write(&radius, 4);
    WriteInt(stream, idle);
    stream->Write(&spread, 4);
    WriteInt(stream, minDelay);
    WriteInt(stream, maxDelay);
    WriteInt(stream, countdown);
}

// 004B1DB0
void ScriptLightning::ScriptLightningSave(ScriptStream* stream) const {
    WriteInt(stream, ObjectId(object));
    WriteInt(stream, id);
}

// ---- Loading --------------------------------------------------------------------------------------

// 004B18B0
void ScriptPointLight::ScriptPointLightLoad(ScriptStream* stream) {
    object = ReadObject(stream);
    stream->Read(&id, 4);
    stream->Read(&removeWhenDone, 4);
    stream->Read(&moving, 4);
    stream->Read(&steps, 4);
    stream->Read(&step, 12);
}

// 004B1B40
void ScriptFxSource::ScriptFxSourceLoad(ScriptStream* stream) {
    object = ReadObject(stream);
    stream->Read(&id, 4);
    {
        mfc::CString read(mfc::CString::kUnset);
        name = *stream->ReadString(&read);
    }
    stream->Read(&position, 12);
    stream->Read(&volume, 4);
    stream->Read(&radius, 4);
    stream->Read(&idle, 4);
    stream->Read(&spread, 4);
    stream->Read(&minDelay, 4);
    stream->Read(&maxDelay, 4);
    stream->Read(&countdown, 4);
}

EI_REWRITTEN(0x004B1940, ScriptPointLight::ScriptPointLightSave);
EI_REWRITTEN(0x004B1A80, ScriptParticleSource::ScriptParticleSourceSave);
EI_REWRITTEN(0x004B1C50, ScriptFxSource::ScriptFxSourceSave);
EI_REWRITTEN(0x004B1DB0, ScriptLightning::ScriptLightningSave);
EI_REWRITTEN(0x004B18B0, ScriptPointLight::ScriptPointLightLoad);
EI_REWRITTEN(0x004B1B40, ScriptFxSource::ScriptFxSourceLoad);
EI_REWRITTEN(0x004B17F0, ScriptPointLight::ScriptPointLightDestruct);
EI_REWRITTEN(0x004B1820, ScriptLightning::ScriptLightningDestruct);
EI_REWRITTEN(0x004B1850, ScriptParticleSource::ScriptParticleSourceDestruct);
EI_REWRITTEN(0x004B1880, ScriptFxSource::ScriptFxSourceDropSound);
EI_REWRITTEN(0x004BD260, CScript::ScriptCreateFx);
EI_REWRITTEN(0x004BD310, CScript::ScriptCreateFxSource);
EI_REWRITTEN(0x004BCFF0, CScript::ScriptCreateLightning);
EI_REWRITTEN(0x004BD540, ScriptFxSource::ScriptFxSourceDestruct);
EI_REWRITTEN(0x004BD9A0, CScript::ScriptCreateParticleSource);
EI_REWRITTEN(0x004BDBB0, CScript::ScriptMoveParticleSource);
EI_REWRITTEN(0x004CD690, ScriptLightningElementsDestruct);
