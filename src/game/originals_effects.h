#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;

// 00498400: the particle manager (*007AF754) makes a source of `type` (RET 4; EAX the source, referenced).
inline void* ParticleManagerObject() { return *reinterpret_cast<void* const*>(0x007AF754); }
__declspec(naked) inline void* __fastcall ParticleCreateSourceOriginal(void* /*manager*/, void*, int /*type*/)
    EI_ORIGINAL(0x00498400)
__declspec(naked) inline void __fastcall SoundStop(void* /*manager*/, void*, int /*sound*/) EI_ORIGINAL(0x006B94A0)
__declspec(naked) inline void __fastcall RemoveLight(void*, void*, CEngineObject* /*light*/) EI_ORIGINAL(0x006CA720)
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
