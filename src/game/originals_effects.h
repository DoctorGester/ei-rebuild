#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"
#include "game_data.h"

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
inline void* ParticleManagerObject() { return ei::GameData<void* const>(0x007AF754); }
// 006CA5D0: the light manager (*007AF738) adds a light (RET 4, no result).
// 00511FB0: the light factory's (*007AF748) list of lit ids (+0xD4): removes every entry equal to *id (RET 4).


// 0049A010: the weather object's alpha ramp (thiscall, RET 0xC: start and width as ints, an exponent; stores them at
// +4..+0xC and fills its 256-entry table at +0x10; no result).
__declspec(naked) inline void __fastcall WeatherSetRampOriginal(void* /*weather*/, void*, int /*start*/, int /*width*/,
                                                                float /*exponent*/) EI_ORIGINAL(0x0049A010)

class CRainSnow;

