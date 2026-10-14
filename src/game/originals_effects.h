#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CSpell;
class CSpellConstructor;
class CSpellManager;
struct CSpellEffect;
struct CMusicSystem;
struct CSoundManager;
struct DamageInfo;
struct Vector3;

// 00498400: the particle manager (*007AF754) makes a source of `type` (RET 4; EAX the source, referenced).
inline void* ParticleManagerObject() { return *reinterpret_cast<void* const*>(0x007AF754); }
__declspec(naked) inline void* __fastcall ParticleCreateSourceOriginal(void* /*manager*/, void*, int /*type*/)
    EI_ORIGINAL(0x00498400)
__declspec(naked) inline void __fastcall SoundStop(void* /*manager*/, void*, int /*sound*/) EI_ORIGINAL(0x006B94A0)
// C3n batch 5. The sound manager (*007AF784; CSoundManager, thiscall; RET 4): 006B7740 SetEffectsVolume, 006B78A0 SetSpeechVolume,
// 006B7A00 SetMusicVolume. 006B4F70 (CMusicSystem::Stop, the music system at *007AF760): the current music stops.
__declspec(naked) inline void __fastcall SoundManagerSetEffectsVolumeOriginal(CSoundManager* /*manager*/, void*, int /*volume*/)
    EI_ORIGINAL(0x006B7740)
__declspec(naked) inline void __fastcall SoundManagerSetSpeechVolumeOriginal(CSoundManager* /*manager*/, void*, int /*volume*/)
    EI_ORIGINAL(0x006B78A0)
__declspec(naked) inline void __fastcall SoundManagerSetMusicVolumeOriginal(CSoundManager* /*manager*/, void*, int /*volume*/)
    EI_ORIGINAL(0x006B7A00)
// C3p. 006B8600 (rewritten as CSoundManager::Play3D; RET 0x1C): plays `name` at `position` heard from `nearDist` to
// `farDist` (floats); EAX the sound's handle, -1 when it did not start.
__declspec(naked) inline int __fastcall SoundManagerPlay3DOriginal(void* /*manager*/, void*, const char* /*name*/,
                                                                   int /*priority*/, const float* /*position*/,
                                                                   float /*nearDist*/, float /*farDist*/, int /*loop*/,
                                                                   int /*speech*/) EI_ORIGINAL(0x006B8600)
__declspec(naked) inline void __fastcall MusicSystemStopOriginal(CMusicSystem* /*music*/) EI_ORIGINAL(0x006B4F70)
__declspec(naked) inline void __fastcall RemoveLight(void*, void*, CEngineObject* /*light*/) EI_ORIGINAL(0x006CA720)
// 006CA5D0: the light manager (*007AF738) adds a light (RET 4, no result).
__declspec(naked) inline void __fastcall LightManagerAddOriginal(void*, void*, CEngineObject* /*light*/) EI_ORIGINAL(0x006CA5D0)
// 0068D010 (CLight): sets the colour (+0x98, +0xC8) and the position (RET 8; EAX 1, not read).
__declspec(naked) inline void __fastcall SetLightColorPosition(CEngineObject* /*light*/, void*, const void* /*color*/,
                                                               const void* /*position*/) EI_ORIGINAL(0x0068D010)
// 00511FB0: the light factory's (*007AF748) list of lit ids (+0xD4): removes every entry equal to *id (RET 4).
__declspec(naked) inline void __fastcall LightIdsRemoveOriginal(void* /*list*/, void*, const int* /*id*/)
    EI_ORIGINAL(0x00511FB0)
__declspec(naked) inline std::uint8_t __fastcall IsLit(void*, void*, const float*) EI_ORIGINAL(0x006CB020)

// 00675CA0: an effect / world object is marked new (flags |= 2, stamp = the world stamp at 007AFFC8); plain RET.
__declspec(naked) inline void __fastcall ObjectMarkNewOriginal(void* /*object*/) EI_ORIGINAL(0x00675CA0)

// C3l. 006CA830: the light manager updates a light for a displacement (RET 8: light, delta vector or null; reads the position at
// the light's +0x1C; EAX is not a result).
__declspec(naked) inline void __fastcall LightMoveOriginal(void* /*lights*/, void*, CEngineObject* /*light*/,
                                                           const float* /*delta*/) EI_ORIGINAL(0x006CA830)

// 0049A010: the weather object's alpha ramp (thiscall, RET 0xC: start and width as ints, an exponent; stores them at
// +4..+0xC and fills its 256-entry table at +0x10; no result).
__declspec(naked) inline void __fastcall WeatherSetRampOriginal(void* /*weather*/, void*, int /*start*/, int /*width*/,
                                                                float /*exponent*/) EI_ORIGINAL(0x0049A010)

// A-W13. 005616C0: the rain / snow layer's destructor body (CRainSnow::DestructBody, effects_weather.cpp), called
// through its address by the deleting destructor.
class CRainSnow;
__declspec(naked) inline void __fastcall RainSnowDestructOriginal(CRainSnow* /*layer*/) EI_ORIGINAL(0x005616C0)

// C3o. 004113A0 (rewritten as CCamera::Shake; thiscall on the camera 0079B2A8, RET 0x10): a shake from an event at
// `source` (ignored 20 or more away). The three floats are pushed as floats.
__declspec(naked) inline void __fastcall CameraShakeOriginal(void* /*camera*/, void*, const Vector3* /*source*/,
                                                             float /*duration*/, float /*amplitude*/, float /*decay*/)
    EI_ORIGINAL(0x004113A0)

// C3n batch 4. Spell effects and spells at their addresses (thiscall, so the verify wrappers and Rebirth's patches run):
// 00679280 (rewritten as CEffectParticle::SetAttachedTo; RET 4) the particle follows the object with the id;
// 0067E6D0 (CSpellEffect::Update) the effect's step, 0 when it is done; 00683950 (CSpellEffect::GetDamage; RET 4) the damage
// it deals into the DamageInfo; 0068B4C0 (CSpell::CanAffect; RET 8) whether the spell affects `target` cast by `caster`;
// 0068BA20 (CSpell::GetPrototype) the spell's prototype record; 0068B410 (CSpell::Clone; RET 4) a copy made with the manager.
__declspec(naked) inline void __fastcall ParticleSetAttachedToOriginal(CEngineObject* /*particle*/, void*, int /*id*/)
    EI_ORIGINAL(0x00679280)
__declspec(naked) inline int __fastcall SpellEffectUpdateOriginal(CSpellEffect* /*effect*/, void*) EI_ORIGINAL(0x0067E6D0)
__declspec(naked) inline int __fastcall SpellEffectGetDamageOriginal(CSpellEffect* /*effect*/, void*, DamageInfo* /*out*/)
    EI_ORIGINAL(0x00683950)
__declspec(naked) inline bool __fastcall SpellCanAffectOriginal(const CSpell* /*spell*/, void*, const CEngineObject* /*caster*/,
                                                                CEngineObject* /*target*/) EI_ORIGINAL(0x0068B4C0)
__declspec(naked) inline char* __fastcall SpellGetPrototypeOriginal(CSpell* /*spell*/) EI_ORIGINAL(0x0068BA20)
__declspec(naked) inline CSpell* __fastcall SpellCloneOriginal(CSpell* /*spell*/, void*, CSpellManager* /*manager*/)
    EI_ORIGINAL(0x0068B410)
// The spell constructor (CSpellConstructor; thiscall): 006841A0 SetManager (RET 4), 006841E0 BeginConstruct (RET 4: the
// prototype record) and 006842A0 AddModifier (RET 4: the modifier record), both 1 on success; 00689030 (the spell manager's
// CSpellManager::CreateSpellFromText, RET 4) makes a spell from its text.
__declspec(naked) inline void __fastcall SpellConstructorSetManagerOriginal(CSpellConstructor* /*constructor*/, void*,
                                                                           CSpellManager* /*manager*/) EI_ORIGINAL(0x006841A0)
__declspec(naked) inline int __fastcall SpellConstructorBeginOriginal(CSpellConstructor* /*constructor*/, void*,
                                                                     char* /*prototype*/) EI_ORIGINAL(0x006841E0)
__declspec(naked) inline int __fastcall SpellConstructorAddModifierOriginal(CSpellConstructor* /*constructor*/, void*,
                                                                           char* /*modifier*/) EI_ORIGINAL(0x006842A0)
__declspec(naked) inline CSpell* __fastcall SpellManagerCreateSpellFromTextOriginal(CSpellManager* /*manager*/, void*,
                                                                                   const char* /*text*/)
    EI_ORIGINAL(0x00689030)
