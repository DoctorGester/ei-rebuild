#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class IsaacRandom;
struct DeltaField;
struct ParticleElement;

__declspec(naked) inline void __fastcall DiplomacyOnAttack(void*, void*, CEngineObject*, CEngineObject*, int)
    EI_ORIGINAL(0x00455680)
__declspec(naked) inline char __fastcall QuitGame(int) EI_ORIGINAL(0x00455F20)
__declspec(naked) inline void* __fastcall ObjectWorld(const void* /*object*/) EI_ORIGINAL(0x00455950)
__declspec(naked) inline void __cdecl PumpMessagesOriginal() EI_ORIGINAL(0x00458220)
__declspec(naked) inline std::uint8_t* __fastcall MapDatabaseFind(void*, void*, const char*) EI_ORIGINAL(0x0045E850)
__declspec(naked) inline void __fastcall VariableRemove(void*, void*, const char*) EI_ORIGINAL(0x00460DB0)
__declspec(naked) inline int __fastcall IsTextKeyOriginal(int) EI_ORIGINAL(0x004613A0)
__declspec(naked) inline void __fastcall SessionPump(void*, void*, void*) EI_ORIGINAL(0x00479E30)
__declspec(naked) inline void* __fastcall FieldReadString(DeltaField*, void*, void*) EI_ORIGINAL(0x0047E310)
__declspec(naked) inline double __fastcall RandomRange(IsaacRandom*, void*, float /*min*/, float /*max*/)
    EI_ORIGINAL(0x00488970)
__declspec(naked) inline ParticleElement* __fastcall AllocateElementsAt(int /*count*/) EI_ORIGINAL(0x00497EB0)
