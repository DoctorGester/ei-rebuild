// Checks for the script effect commands and element destructors (src/game/script_effects.cpp; the
// savers and loaders are checked in verify_script_save.cpp / verify_script_load.cpp). Scenario:
// tests/scenarios/script_fx.txt (effects.txt and particles.txt reach CreateParticleSource too).
//
// The creators make world objects and the destructors take them out of the world, so running the
// original as well would change the world twice: those are checked on ours only, against what the
// command's arguments say. The new object is the one the effect manager registered last.
#include "verify.h"

#include "../../src/game/base_manager.h"
#include "../../src/game/script.h"
#include "../../src/game/world.h"
#include "../../src/game/world_effect.h"
#include "../../src/lib/crt.h"

#include <cmath>

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

CBaseManager* EffectManager() { return static_cast<CBaseManager*>(WorldServer()->manager48_); }

// The object the effect manager registered last (the creators' new object), or null.
CEngineObject* NewestEffect() { return EffectManager()->FindObject(EffectManager()->lastId_); }

bool SameBits(const void* a, const void* b, std::size_t n) { return std::memcmp(a, b, n) == 0; }

// ---- Element destructors ----------------------------------------------------------------------------
// Ours only (the original would take the object out of the world too): the slot cleared; the object's
// count down by one or two (two when it was still a timed object), read only when it cannot have
// reached zero.
template <typename T>
void DestructVerify(const char* name, void(__fastcall* ours)(T*), T* self) {
    CEngineObject* object = self->object;
    int before = object ? object->refCount_ : 0;
    ours(self);
    bool ok = self->object == nullptr;
    if (object && before > 2) ok = ok && (object->refCount_ == before - 1 || object->refCount_ == before - 2);
    ei::verify::Check(name, true, ok, "object %p, count %d before", static_cast<void*>(object), before);
}

// 004B17F0: point light slots (0059DD70, the slot array's Serialize).
void __fastcall PointLightDestructVerify(ScriptPointLight* self) {
    DestructVerify("ScriptPointLightDestruct", &ScriptPointLightDestruct, self);
}
// 004B1820: lightning (CreateLightning's temporary, the list's RemoveAll 0059C1F0, 004BEB00).
void __fastcall LightningDestructVerify(ScriptLightning* self) {
    DestructVerify("ScriptLightningDestruct", &ScriptLightningDestruct, self);
}
// 004B1850: particle sources (the list's RemoveAll 0059C1A0).
void __fastcall ParticleDestructVerify(ScriptParticleSource* self) {
    DestructVerify("ScriptParticleSourceDestruct", &ScriptParticleSourceDestruct, self);
}
// 004B1880: CreateFXSource's temporary.
void __fastcall FxDropVerify(ScriptFxSource* self) {
    DestructVerify("ScriptFxSourceDropSound", &ScriptFxSourceDropSound, self);
}

// ---- Commands -----------------------------------------------------------------------------------------

// 004BD260 CreateFX: a new sound with the arguments' position (bits), volume, radius and name, not
// looping.
void __fastcall CreateFxVerify(CScript* script, void*, const SSCCmd* cmd) {
    int last = EffectManager()->lastId_;
    ScriptCreateFx(script, nullptr, cmd);
    CEngineObject* object = NewestEffect();
    bool ok = EffectManager()->lastId_ != last && object && object->GetClassId() == 0x48;
    if (ok) {
        auto* sound = static_cast<CEffectSound*>(object);
        const float* f = cmd->floatArgs.data;
        ok = SameBits(&sound->localPosition_, f, 12) && SameBits(&sound->volume_, &f[3], 4) &&
             SameBits(&sound->radius_, &f[4], 4) && sound->looping_ == 0 &&
             std::strcmp(sound->name_, cmd->stringArgs.data[0]) == 0;
    }
    ei::verify::Check("ScriptCreateFx", true, ok, "sound %p (id %d)", static_cast<void*>(object),
                      EffectManager()->lastId_);
}

// 004BCFF0 CreateLightning: one more node at the tail with the new bolt (class 0x4D, referenced by the
// node and the world), its id (the next automatic one for a negative or NaN argument, which is then
// counted down; else truncated) and the bolt's ends and width as the arguments.
void __fastcall CreateLightningVerify(CScript* script, void*, const SSCCmd* cmd) {
    int count = script->lightning_.count;
    int nextId = script->nextParticleId_;
    ScriptCreateLightning(script, nullptr, cmd);
    const float* f = cmd->floatArgs.data;
    bool automatic = !(f[0] >= 0.0f);
    auto* node = script->lightning_.tail;
    bool ok = script->lightning_.count == count + 1 && node && !node->next &&
              script->nextParticleId_ == (automatic ? nextId - 1 : nextId) &&
              node->data.id == (automatic ? nextId : static_cast<int>(f[0])) && node->data.object &&
              node->data.object == NewestEffect() && node->data.object->GetClassId() == 0x4D &&
              node->data.object->refCount_ >= 2;
    if (ok) {
        auto* bolt = static_cast<CEffectLightning*>(node->data.object);
        ok = SameBits(&bolt->from_, &f[1], 12) && SameBits(&bolt->to_, &f[4], 12) && SameBits(&bolt->width_, &f[7], 4);
    }
    ei::verify::Check("ScriptCreateLightning", true, ok, "%d -> %d bolts, id %d", count, script->lightning_.count,
                      node ? node->data.id : 0);
}

// 004BD540: the FX source's destructor body (the list's RemoveAt 004C6370, the element destructor
// 004CE2B0, 004BEB00). Ours only, as above (the name is destroyed too).
void __fastcall FxDestructVerify(ScriptFxSource* self) {
    DestructVerify("ScriptFxSourceDestruct", &ScriptFxSourceDestruct, self);
}

MfcList<ScriptParticleSource>::Node* FindParticleSource(CScript* script, int id) {
    for (auto* node = script->particles_.head; node; node = node->next)
        if (node->data.id == id) return node;
    return nullptr;
}

// 004BD9A0 CreateParticleSource: an unknown type name changes nothing; otherwise the source with the
// id (the next automatic one for a negative id) holds the newest effect (class 0x4A, the table's
// type, the arguments' position and size bits), is not moving and has the type's lifetime; the list
// grew by one exactly when the id was new.
void __fastcall CreateParticleSourceVerify(CScript* script, void*, const SSCCmd* cmd) {
    const float* f = cmd->floatArgs.data;
    const char* name = cmd->stringArgs.data[0];
    const ScriptParticleType* type = nullptr;
    for (int i = 0; i < kScriptParticleTypeCount && !type; ++i)
        if (gamecrt::stricmp(ScriptParticleTypes()[i].name, name) == 0) type = &ScriptParticleTypes()[i];
    int count = script->particles_.count;
    int nextId = script->nextParticleId_;
    int last = EffectManager()->lastId_;
    int id = static_cast<int>(std::lrint(f[0]));
    bool automatic = id < 0;
    if (automatic) id = nextId;
    bool existed = type && FindParticleSource(script, id) != nullptr;

    ScriptCreateParticleSource(script, nullptr, cmd);

    bool ok;
    if (!type) {
        ok = script->particles_.count == count && script->nextParticleId_ == nextId && EffectManager()->lastId_ == last;
    } else {
        auto* node = FindParticleSource(script, id);
        ok = node && script->particles_.count == count + (existed ? 0 : 1) &&
             script->nextParticleId_ == (automatic ? nextId - 1 : nextId) && (existed || node == script->particles_.tail);
        if (ok) {
            const ScriptParticleSource& s = node->data;
            auto* particles = static_cast<CEffectParticle*>(s.object);
            ok = particles && particles == NewestEffect() && particles->GetClassId() == 0x4A &&
                 particles->type_ == type->type && SameBits(&particles->localPosition_, &f[1], 12) &&
                 SameBits(&particles->size_, &f[4], 4) && particles->refCount_ >= 2 && s.deleteAfterMove == 0 &&
                 s.moving == 0 && s.steps == 0 && s.step[0] == 0.0f && s.step[1] == 0.0f && s.step[2] == 0.0f &&
                 s.lifetime == type->lifetime;
        }
    }
    ei::verify::Check("ScriptCreateParticleSource", true, ok, "\"%s\" id %d, %d -> %d sources", name, id, count,
                      script->particles_.count);
}

// 004BDBB0 MoveParticleSource: against the original. The original moves the source first; its result
// is kept aside, the source restored, ours runs; flags and step count must equal, the step nearly.
// PUSH EBP; MOV EBP,ESP; SUB ESP,0x1C (6 bytes).
void(__fastcall* s_moveParticles)(CScript*, void*, const SSCCmd*);
void __fastcall MoveParticleSourceVerify(CScript* script, void*, const SSCCmd* cmd) {
    auto* node = FindParticleSource(script, static_cast<int>(std::lrint(cmd->floatArgs.data[0])));
    if (!node) {
        int count = script->particles_.count;
        ScriptMoveParticleSource(script, nullptr, cmd);
        ei::verify::Check("ScriptMoveParticleSource", count, script->particles_.count, "no source");
        return;
    }
    ScriptParticleSource before = node->data;
    s_moveParticles(script, nullptr, cmd);
    ScriptParticleSource expected = node->data;
    node->data = before;
    ScriptMoveParticleSource(script, nullptr, cmd);
    const ScriptParticleSource& actual = node->data;
    ei::verify::Check("ScriptMoveParticleSource", true,
                      expected.object == actual.object && expected.id == actual.id &&
                          expected.moving == actual.moving && expected.deleteAfterMove == actual.deleteAfterMove &&
                          expected.steps == actual.steps && expected.lifetime == actual.lifetime,
                      "source %d", actual.id);
    for (int i = 0; i < 3; ++i)
        ei::verify::CheckNear("ScriptMoveParticleSource step", expected.step[i], actual.step[i], "source %d axis %d",
                              actual.id, i);
}

} // namespace

EI_VERIFY(0x004BD540, FxDestructVerify, 0, s_unused);
EI_VERIFY(0x004BD9A0, CreateParticleSourceVerify, 0, s_unused);
EI_VERIFY(0x004BDBB0, MoveParticleSourceVerify, 6, s_moveParticles);
EI_VERIFY(0x004B17F0, PointLightDestructVerify, 0, s_unused);
EI_VERIFY(0x004B1820, LightningDestructVerify, 0, s_unused);
EI_VERIFY(0x004B1850, ParticleDestructVerify, 0, s_unused);
EI_VERIFY(0x004B1880, FxDropVerify, 0, s_unused);
EI_VERIFY(0x004BD260, CreateFxVerify, 0, s_unused);
EI_VERIFY(0x004BCFF0, CreateLightningVerify, 0, s_unused);
