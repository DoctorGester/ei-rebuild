#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

struct RefPtr;
struct Vector3;

__declspec(naked) inline double __fastcall DotRaw(const Vector3* /*a*/, const Vector3* /*b*/) EI_ORIGINAL(0x00401E00)
__declspec(naked) inline double __fastcall LengthRaw(const Vector3* /*v*/) EI_ORIGINAL(0x00401E20)
__declspec(naked) inline float* __fastcall Negate(float* /*out*/, const float* /*v*/) EI_ORIGINAL(0x00402200)
__declspec(naked) inline char* __fastcall AllocWords(int) EI_ORIGINAL(0x00406B30)
__declspec(naked) inline void __fastcall ConstructEngineObject(void* /*object*/) EI_ORIGINAL(0x0040CB20)
__declspec(naked) inline void __cdecl NetworkTimers() EI_ORIGINAL(0x004122D0)
__declspec(naked) inline void __fastcall RefShare(void*, void*, void*) EI_ORIGINAL(0x00418BE0)
__declspec(naked) inline void __fastcall RefNullBOriginal(RefPtr* /*ref*/) EI_ORIGINAL(0x0041FF70)
__declspec(naked) inline void __fastcall RefNullA(RefPtr* /*ref*/) EI_ORIGINAL(0x0041FF80)
__declspec(naked) inline void* __fastcall RefPtrDestroyAt(RefPtr* /*p*/, void*, unsigned /*flags*/)
    EI_ORIGINAL(0x004292A0)
__declspec(naked) inline void __fastcall Element25B0Destroy(void* /*element*/, void*, int /*flags*/)
    EI_ORIGINAL(0x004292C0)
__declspec(naked) inline void __fastcall Element25ACDestroy(void* /*element*/, void*, int /*flags*/)
    EI_ORIGINAL(0x0042A5F0)
__declspec(naked) inline void __fastcall DestructTextureName(void*) EI_ORIGINAL(0x0044C490)
