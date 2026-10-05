// The effects a mission script places in the world: point lights, particle sources, sound (FX)
// sources and lightning bolts (CScript::pointLights_, particles_, fxSources_, lightning_). Saving
// and loading them with the savegame. See script.h and docs/notes/script.md ("Script effects").
#include "script.h"

#include "base_manager.h"
#include "originals_library.h"
#include "world.h"
#include "world_effect.h"

#include "ei/hook.h"

#include <cstring>

namespace {

// Class ids of the effect objects the script makes (CBaseManager::CreateObject).
constexpr int kClassSound = 0x48;       // CEffectSound
constexpr int kClassParticles = 0x4A;   // CEffectParticle
constexpr int kClassLightning = 0x4D;   // CEffectLightning

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
void __fastcall ScriptPointLightDestruct(ScriptPointLight* light) { DropEffect(&light->object); }
// 004B1820
void __fastcall ScriptLightningDestruct(ScriptLightning* bolt) { DropEffect(&bolt->object); }
// 004B1850
void __fastcall ScriptParticleSourceDestruct(ScriptParticleSource* source) { DropEffect(&source->object); }
// 004B1880 (the name is the caller's to destroy)
void __fastcall ScriptFxSourceDropSound(ScriptFxSource* source) { DropEffect(&source->object); }

// ---- Commands -----------------------------------------------------------------------------------------

// 004BD260: CreateFX(x, y, z, volume, radius, name), a sound played once.
void __fastcall ScriptCreateFx(CScript*, void*, const SSCCmd* cmd) {
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

// 004BCFF0: CreateLightning(id, x1, y1, z1, x2, y2, z2, width). A negative (or NaN) id takes the
// next automatic one; others are truncated. The bolt is always added, even when the id exists.
void __fastcall ScriptCreateLightning(CScript* script, void*, const SSCCmd* cmd) {
    MfcList<ScriptLightning>& list = script->lightning_;
    auto* node = static_cast<MfcList<ScriptLightning>::Node*>(
        ScriptLightningListNewNodeOriginal(&list, nullptr, list.tail, nullptr));
    node->data.object = nullptr;
    node->data.id = -1;
    if (list.tail) list.tail->next = node;
    else list.head = node;
    list.tail = node;

    const float* f = cmd->floatArgs.data;
    if (!(f[0] >= 0.0f)) node->data.id = script->nextParticleId_--;
    else node->data.id = static_cast<int>(f[0]);

    auto* bolt = static_cast<CEffectLightning*>(EffectManager()->CreateObject(kClassLightning, -1));
    node->data.object = bolt;
    ++bolt->refCount_;
    Vector3 to{f[4], f[5], f[6]};
    Vector3 from{f[1], f[2], f[3]};
    bolt->SetEnds(&from, &to, f[7]);
    WorldServer()->AddTimedObject(bolt);
}

// ---- Saving ---------------------------------------------------------------------------------------

// 004B1940
void __fastcall ScriptPointLightSave(const ScriptPointLight* light, void*, ScriptStream* stream) {
    WriteInt(stream, ObjectId(light->object));
    WriteInt(stream, light->id);
    WriteInt(stream, light->removeWhenDone);
    WriteInt(stream, light->moving);
    WriteInt(stream, light->steps);
    stream->Write(light->step, 12);
}

// 004B1A80
void __fastcall ScriptParticleSourceSave(const ScriptParticleSource* source, void*, ScriptStream* stream) {
    WriteInt(stream, ObjectId(source->object));
    WriteInt(stream, source->id);
    WriteInt(stream, source->deleteAfterMove);
    WriteInt(stream, source->moving);
    WriteInt(stream, source->steps);
    stream->Write(source->step, 12);
    WriteInt(stream, source->lifetime);
}

// 004B1C50
void __fastcall ScriptFxSourceSave(const ScriptFxSource* source, void*, ScriptStream* stream) {
    WriteInt(stream, ObjectId(source->object));
    WriteInt(stream, source->id);
    const char* name = source->name;
    stream->Write(name, static_cast<std::uint32_t>(Length(name) + 1));
    stream->Write(source->position, 12);
    stream->Write(&source->volume, 4);
    stream->Write(&source->radius, 4);
    WriteInt(stream, source->idle);
    stream->Write(&source->spread, 4);
    WriteInt(stream, source->minDelay);
    WriteInt(stream, source->maxDelay);
    WriteInt(stream, source->countdown);
}

// 004B1DB0
void __fastcall ScriptLightningSave(const ScriptLightning* bolt, void*, ScriptStream* stream) {
    WriteInt(stream, ObjectId(bolt->object));
    WriteInt(stream, bolt->id);
}

// ---- Loading --------------------------------------------------------------------------------------

// 004B18B0
void __fastcall ScriptPointLightLoad(ScriptPointLight* light, void*, ScriptStream* stream) {
    light->object = ReadObject(stream);
    stream->Read(&light->id, 4);
    stream->Read(&light->removeWhenDone, 4);
    stream->Read(&light->moving, 4);
    stream->Read(&light->steps, 4);
    stream->Read(light->step, 12);
}

// 004B1B40
void __fastcall ScriptFxSourceLoad(ScriptFxSource* source, void*, ScriptStream* stream) {
    source->object = ReadObject(stream);
    stream->Read(&source->id, 4);
    alignas(4) unsigned char name[sizeof(mfc::CString)];
    source->name = *stream->ReadString(name);
    reinterpret_cast<mfc::CString*>(name)->~CString();
    stream->Read(source->position, 12);
    stream->Read(&source->volume, 4);
    stream->Read(&source->radius, 4);
    stream->Read(&source->idle, 4);
    stream->Read(&source->spread, 4);
    stream->Read(&source->minDelay, 4);
    stream->Read(&source->maxDelay, 4);
    stream->Read(&source->countdown, 4);
}

EI_REPLACE(0x004B1940, ScriptPointLightSave);
EI_REPLACE(0x004B1A80, ScriptParticleSourceSave);
EI_REPLACE(0x004B1C50, ScriptFxSourceSave);
EI_REPLACE(0x004B1DB0, ScriptLightningSave);
EI_REPLACE(0x004B18B0, ScriptPointLightLoad);
EI_REPLACE(0x004B1B40, ScriptFxSourceLoad);
EI_REPLACE(0x004B17F0, ScriptPointLightDestruct);
EI_REPLACE(0x004B1820, ScriptLightningDestruct);
EI_REPLACE(0x004B1850, ScriptParticleSourceDestruct);
EI_REPLACE(0x004B1880, ScriptFxSourceDropSound);
EI_REPLACE(0x004BD260, ScriptCreateFx);
EI_REPLACE(0x004BCFF0, ScriptCreateLightning);
