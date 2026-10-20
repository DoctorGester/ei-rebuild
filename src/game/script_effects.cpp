// The effects a mission script places in the world: point lights, particle sources, sound (FX)
// sources and lightning bolts (CScript::pointLights_, particles_, fxSources_, lightning_). Saving
// and loading them with the savegame. See script.h and docs/notes/script.md.
#include "script.h"

#include "base_manager.h"
#include "originals_library.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc_list.h"

#include <climits>
#include <cmath>
#include <cstring>

namespace {

// Class ids of the effect objects the script makes (CBaseManager::CreateObject).
constexpr int kClassSound = 0x48;       // CEffectSound
constexpr int kClassParticles = 0x4A;   // CEffectParticle
constexpr int kClassLightning = 0x4D;   // CEffectLightning
constexpr int kCommandCreateFxSource = 0x16;   // CreateRandomizedFXSource is 0x18

// The world's manager of timed objects (spells and effects; CWorld::manager48_, 007AFFA8).
CBaseManager* EffectManager() { return static_cast<CBaseManager*>(WorldServer()->manager48_); }

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
        WorldServer()->RemoveTimedObject(object->id_);
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
    WorldServer()->AddTimedObject(sound);
}

// 004BD310: CreateFXSource / CreateRandomizedFXSource (script.h). A new source's element is the
// list's NewNode's (zeroed, the name empty); the original then copies a stack temporary over it
// whose object is null, whose name is empty and whose other fields were never written. Every field
// is written below except, for CreateFXSource, spread, the delays and the countdown: the tick
// (004BF000) counts such a source down and replaces its sound at zero, so a zero countdown would
// replace the sound on every tick; INT_MAX keeps the sound, as a positive residue does.
void CScript::ScriptCreateFxSource(const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    int id = static_cast<int>(std::lrint(f[0]));
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
        source->minDelay = static_cast<int>(std::lrint(f[6]));
        source->maxDelay = static_cast<int>(std::lrint(f[7]));
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
    WorldServer()->AddTimedObject(sound);
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
    if (!(f[0] >= 0.0f)) node->data.id = nextParticleId_--;
    else node->data.id = static_cast<int>(f[0]);

    auto* bolt = static_cast<CEffectLightning*>(EffectManager()->CreateObject(kClassLightning, -1));
    node->data.object = bolt;
    ++bolt->refCount_;
    Vector3 to{f[4], f[5], f[6]};
    Vector3 from{f[1], f[2], f[3]};
    bolt->SetEnds(&from, &to, f[7]);
    WorldServer()->AddTimedObject(bolt);
}

// 004BD9A0: CreateParticleSource(id, x, y, z, size, type name). Unknown type names make nothing. A
// negative id takes the next automatic one; an existing source with the id gets the new effect.
void CScript::ScriptCreateParticleSource(const SSCCmd* cmd) {
    mfc::CString name(cmd->stringArgs.data[0]);
    const ScriptParticleType* type = nullptr;
    for (int i = 0; i < kScriptParticleTypeCount; ++i) {
        if (libcrt::Stricmp(ScriptParticleTypes()[i].name, name) == 0) {
            type = &ScriptParticleTypes()[i];
            break;
        }
    }
    if (!type || type->type == 0) return;
    int particleType = type->type;
    int lifetime = type->lifetime;

    const float* f = cmd->floatArgs.data;
    int id = static_cast<int>(std::lrint(f[0]));
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
    WorldServer()->AddTimedObject(particles);

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
    int id = static_cast<int>(std::lrint(f[0]));
    ScriptParticleSource* source = nullptr;
    for (auto* node = particles_.head; node; node = node->next) {
        if (node->data.id == id) {
            source = &node->data;
            break;
        }
    }
    if (!source) return;
    source->moving = 1;
    source->deleteAfterMove = static_cast<int>(std::lrint(f[5]));
    source->steps = static_cast<int>(std::lrint(f[4]));
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
    alignas(4) unsigned char nameStorage[sizeof(mfc::CString)];
    name = *stream->ReadString(nameStorage);
    reinterpret_cast<mfc::CString*>(nameStorage)->~CString();
    stream->Read(&position, 12);
    stream->Read(&volume, 4);
    stream->Read(&radius, 4);
    stream->Read(&idle, 4);
    stream->Read(&spread, 4);
    stream->Read(&minDelay, 4);
    stream->Read(&maxDelay, 4);
    stream->Read(&countdown, 4);
}

EI_REPLACE_METHOD(0x004B1940, ScriptPointLight::ScriptPointLightSave);
EI_REPLACE_METHOD(0x004B1A80, ScriptParticleSource::ScriptParticleSourceSave);
EI_REPLACE_METHOD(0x004B1C50, ScriptFxSource::ScriptFxSourceSave);
EI_REPLACE_METHOD(0x004B1DB0, ScriptLightning::ScriptLightningSave);
EI_REPLACE_METHOD(0x004B18B0, ScriptPointLight::ScriptPointLightLoad);
EI_REPLACE_METHOD(0x004B1B40, ScriptFxSource::ScriptFxSourceLoad);
EI_REPLACE_METHOD(0x004B17F0, ScriptPointLight::ScriptPointLightDestruct);
EI_REPLACE_METHOD(0x004B1820, ScriptLightning::ScriptLightningDestruct);
EI_REPLACE_METHOD(0x004B1850, ScriptParticleSource::ScriptParticleSourceDestruct);
EI_REPLACE_METHOD(0x004B1880, ScriptFxSource::ScriptFxSourceDropSound);
EI_REPLACE_METHOD(0x004BD260, CScript::ScriptCreateFx);
EI_REPLACE_METHOD(0x004BD310, CScript::ScriptCreateFxSource);
EI_REPLACE_METHOD(0x004BCFF0, CScript::ScriptCreateLightning);
EI_REPLACE_METHOD(0x004BD540, ScriptFxSource::ScriptFxSourceDestruct);
EI_REPLACE_METHOD(0x004BD9A0, CScript::ScriptCreateParticleSource);
EI_REPLACE_METHOD(0x004BDBB0, CScript::ScriptMoveParticleSource);
EI_REPLACE(0x004CD690, ScriptLightningElementsDestruct);
