#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"

struct AnimationSteps;
struct RefPtr;

__declspec(naked) inline void** __fastcall PartiesEnd(void*, void*, void**) EI_ORIGINAL(0x004208A0)
__declspec(naked) inline void __fastcall List25BCClear(void* /*list*/) EI_ORIGINAL(0x00421CD0)
__declspec(naked) inline void __fastcall List25B0FreeNode(void* /*list*/, void*, void* /*node*/, int /*n*/)
    EI_ORIGINAL(0x00422390)
__declspec(naked) inline void* __fastcall ListConstruct4231C0(void* /*list*/, void*, const void* /*alloc*/)
    EI_ORIGINAL(0x004231C0)
__declspec(naked) inline void __fastcall List2610Destruct(void* /*list*/) EI_ORIGINAL(0x00423370)
__declspec(naked) inline void __fastcall List25ACFree(void* /*list*/, void*, void* /*node*/, int /*n*/)
    EI_ORIGINAL(0x00425A40)
__declspec(naked) inline void __fastcall List25B0Clear(void* /*list*/) EI_ORIGINAL(0x00427100)
__declspec(naked) inline void __fastcall List2598Clear(void* /*list*/) EI_ORIGINAL(0x004271C0)
__declspec(naked) inline void** __fastcall PartiesErase(void*, void*, void**, void*) EI_ORIGINAL(0x004294D0)
__declspec(naked) inline RefPtr* __fastcall UninitializedCopy(const RefPtr* /*first*/, const RefPtr* /*last*/, RefPtr* /*result*/, std::uint32_t /*tag*/)
    EI_ORIGINAL(0x0042AF80)
__declspec(naked) inline void __stdcall ZeroPoints(void*, int) EI_ORIGINAL(0x00452DC0)
__declspec(naked) inline void __fastcall StringsCopyOriginal(void*, void*, const void*) EI_ORIGINAL(0x004C14A0)
__declspec(naked) inline void __fastcall GroupsCopy(void*, void*, const void*) EI_ORIGINAL(0x004C1BE0)
__declspec(naked) inline void __fastcall NamesSetSizeOriginal(void*, void*, int, int) EI_ORIGINAL(0x004C2510)
__declspec(naked) inline void __fastcall FloatsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C2880)
__declspec(naked) inline void __fastcall CommandsCopy(void*, void*, const void*) EI_ORIGINAL(0x004CC0C0)
__declspec(naked) inline void __fastcall CellListDestroy(void*) EI_ORIGINAL(0x00539890)
__declspec(naked) inline void __fastcall FirstListConstruct(void*, void*, const void*) EI_ORIGINAL(0x0053F4F0)
__declspec(naked) inline void __fastcall StepsSetSize(AnimationSteps*, void*, int, int) EI_ORIGINAL(0x00557B80)
__declspec(naked) inline void __fastcall NodeFree12Fast(void*, int) EI_ORIGINAL(0x0056FB40)
__declspec(naked) inline void __fastcall ConstructModelMap(void*, void*, const void*) EI_ORIGINAL(0x005FA420)
__declspec(naked) inline int __stdcall WsGetHostName(char*, int) EI_ORIGINAL(0x006E6100)
__declspec(naked) inline char* __cdecl GameStrncpy(char* /*to*/, const char* /*from*/, std::size_t /*n*/)
    EI_ORIGINAL(0x006E7E80)
__declspec(naked) inline int __cdecl GameAtoi(const char* /*text*/) EI_ORIGINAL(0x006E80D8)
__declspec(naked) inline void __cdecl ExitGame(int) EI_ORIGINAL(0x006E8DA7)
__declspec(naked) inline double __cdecl GameAtof(const char*) EI_ORIGINAL(0x006E911A)
__declspec(naked) inline const char* __cdecl MbsInc(const char*) EI_ORIGINAL(0x006E9947)
__declspec(naked) inline int __cdecl IsMbbLead(unsigned) EI_ORIGINAL(0x006E99F2)
__declspec(naked) inline int __cdecl IsLeadByte(int) EI_ORIGINAL(0x006E9A6C)
__declspec(naked) inline double __cdecl Hypot(double /*x*/, double /*y*/) EI_ORIGINAL(0x006FD026)
__declspec(naked) inline mfc::CString __stdcall PlusChar(const mfc::CString&, char) EI_ORIGINAL(0x006FD56E)
__declspec(naked) inline void __cdecl GameDelete(void*) EI_ORIGINAL(0x007005D8)
__declspec(naked) inline int __stdcall AfxMessageBox(const char*, unsigned, unsigned) EI_ORIGINAL(0x0070AC94)
