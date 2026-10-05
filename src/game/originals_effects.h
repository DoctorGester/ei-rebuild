#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;

__declspec(naked) inline void __fastcall SoundStop(void* /*manager*/, void*, int /*sound*/) EI_ORIGINAL(0x006B94A0)
__declspec(naked) inline void __fastcall RemoveLight(void*, void*, CEngineObject* /*light*/) EI_ORIGINAL(0x006CA720)
__declspec(naked) inline std::uint8_t __fastcall IsLit(void*, void*, const float*) EI_ORIGINAL(0x006CB020)
