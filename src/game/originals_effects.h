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
__declspec(naked) inline void __fastcall RemoveLight(void*, void*, CEngineObject* /*light*/) EI_ORIGINAL(0x006CA720)
// 006CA5D0: the light manager (*007AF738) adds a light (RET 4, no result).
__declspec(naked) inline void __fastcall LightManagerAddOriginal(void*, void*, CEngineObject* /*light*/) EI_ORIGINAL(0x006CA5D0)
// 0068D010 (CLight): sets the colour (+0x98, +0xC8) and the position (RET 8; EAX 1, not read).
__declspec(naked) inline void __fastcall SetLightColorPosition(CEngineObject* /*light*/, void*, const void* /*color*/,
                                                               const void* /*position*/) EI_ORIGINAL(0x0068D010)
// 00511FB0: the light factory's (*007AF748) list of lit ids (+0xD4): removes every entry equal to *id (RET 4).
__declspec(naked) inline void __fastcall LightIdsRemoveOriginal(void* /*list*/, void*, const int* /*id*/)
    EI_ORIGINAL(0x00511FB0)

// C3l. 006CA830: the light manager updates a light for a displacement (RET 8: light, delta vector or null; reads the position at
// the light's +0x1C; EAX is not a result).
__declspec(naked) inline void __fastcall LightMoveOriginal(void* /*lights*/, void*, CEngineObject* /*light*/,
                                                           const float* /*delta*/) EI_ORIGINAL(0x006CA830)

// 0049A010: the weather object's alpha ramp (thiscall, RET 0xC: start and width as ints, an exponent; stores them at
// +4..+0xC and fills its 256-entry table at +0x10; no result).
__declspec(naked) inline void __fastcall WeatherSetRampOriginal(void* /*weather*/, void*, int /*start*/, int /*width*/,
                                                                float /*exponent*/) EI_ORIGINAL(0x0049A010)

class CRainSnow;

// C3n batch 4. Spell effects and spells at their addresses (thiscall, so the verify wrappers and Rebirth's patches run):
// 00679280 (rewritten as CEffectParticle::SetAttachedTo; RET 4) the particle follows the object with the id;
// 0068B4C0 (CSpell::CanAffect; RET 8) whether the spell affects `target` cast by `caster`.
__declspec(naked) inline void __fastcall ParticleSetAttachedToOriginal(CEngineObject* /*particle*/, void*, int /*id*/)
    EI_ORIGINAL(0x00679280)
__declspec(naked) inline bool __fastcall SpellCanAffectOriginal(const CSpell* /*spell*/, void*, const CEngineObject* /*caster*/,
                                                                CEngineObject* /*target*/) EI_ORIGINAL(0x0068B4C0)
