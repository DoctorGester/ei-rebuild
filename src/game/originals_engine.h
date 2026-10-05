#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"

class CEngineObject;
class IsaacRandom;
struct DeltaField;
struct ParticleElement;

__declspec(naked) inline void __fastcall DiplomacyOnAttack(void*, void*, CEngineObject*, CEngineObject*, int)
    EI_ORIGINAL(0x00455680)
__declspec(naked) inline char __fastcall QuitGame(int) EI_ORIGINAL(0x00455F20)
// 00455940 (rewritten as CEngineObject::ManagerContext): the object's manager context (+0x14 -> +0xAC, the world).
__declspec(naked) inline void* __fastcall ManagerContextOf(const void* /*object*/) EI_ORIGINAL(0x00455940)
__declspec(naked) inline void* __fastcall ObjectWorld(const void* /*object*/) EI_ORIGINAL(0x00455950)
__declspec(naked) inline void __cdecl PumpMessagesOriginal() EI_ORIGINAL(0x00458220)
__declspec(naked) inline std::uint8_t* __fastcall MapDatabaseFind(void*, void*, const char*) EI_ORIGINAL(0x0045E850)
__declspec(naked) inline void __fastcall VariableRemove(void*, void*, const char*) EI_ORIGINAL(0x00460DB0)
__declspec(naked) inline int __fastcall IsTextKeyOriginal(int) EI_ORIGINAL(0x004613A0)
__declspec(naked) inline void __fastcall SessionPump(void*, void*, void*) EI_ORIGINAL(0x00479E30)
__declspec(naked) inline void* __fastcall FieldReadString(DeltaField*, void*, void*) EI_ORIGINAL(0x0047E310)
// 004800E0 (cdecl, printf style): reports a DirectX error (HRESULT, format, arguments) and exits; returns 0 if it comes back.
__declspec(naked) inline int __cdecl FatalDirectX(long /*result*/, const char* /*format*/, ...) EI_ORIGINAL(0x004800E0)
// 00480080 (rewritten as FatalError; cdecl, printf style, plain RET): reports a fatal error and exits; returns 0
// if it comes back.
__declspec(naked) inline int __cdecl FatalErrorOriginal(const char* /*format*/, ...) EI_ORIGINAL(0x00480080)
// Text tables (CTextManager): 004DEFC0 opens a section by key (RET 4, EAX 1 when found and non-empty, else 0);
// 004E0020 reads a string into a CString (RET 8, EAX 1 when found, else 0).
__declspec(naked) inline int __fastcall TextsOpenSection(void* /*texts*/, void*, const char* /*key*/)
    EI_ORIGINAL(0x004DEFC0)
__declspec(naked) inline int __fastcall TextsGetString(void* /*texts*/, void*, const char* /*key*/, mfc::CString* /*out*/)
    EI_ORIGINAL(0x004E0020)
__declspec(naked) inline double __fastcall RandomRange(IsaacRandom*, void*, float /*min*/, float /*max*/)
    EI_ORIGINAL(0x00488970)
__declspec(naked) inline ParticleElement* __fastcall AllocateElementsAt(int /*count*/) EI_ORIGINAL(0x00497EB0)

// Named float table (RET 4 / RET 8): 00460AF0 Get (result in ST0), 00460BC0 Set (the value is the raw dword the
// original pushes, so no FPU load changes it on the way). 004DF8D0: the texts read the current line into a CString
// (RET 8, EAX 0/1).
__declspec(naked) inline float __fastcall NamedTableGet(void* /*table*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x00460AF0)
__declspec(naked) inline void __fastcall NamedTableSet(void* /*table*/, void*, const char* /*name*/,
                                                       std::uint32_t /*valueBits*/) EI_ORIGINAL(0x00460BC0)
__declspec(naked) inline int __fastcall TextsReadLineOriginal(void* /*texts*/, void*, mfc::CString* /*out*/,
                                                              int /*advance*/) EI_ORIGINAL(0x004DF8D0)
// 004A54D0: CMemoryStream::Resize(size), makes room for `size` bytes (RET 4, EAX not a result).
__declspec(naked) inline void __fastcall StreamResizeOriginal(void* /*stream*/, void*, std::uint32_t /*size*/)
    EI_ORIGINAL(0x004A54D0)
// 0044C470: the game's debug message (cdecl, printf style, a bare RET; the caller's EAX comes back unchanged, and
// bink_video.cpp masks it, so it is declared to return a value). src/lib/crt.cpp keeps its own copy.
__declspec(naked) inline unsigned __cdecl DebugMessageOriginal(const char*, ...) EI_ORIGINAL(0x0044C470)
