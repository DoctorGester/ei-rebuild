#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>


__declspec(naked) inline char* __fastcall AllocVertices(int) EI_ORIGINAL(0x004E5710)
__declspec(naked) inline void* __fastcall LockVertexBuffer(char*, void*, int) EI_ORIGINAL(0x004E7820)
__declspec(naked) inline int __fastcall DrawMesh(char*, void*, const void*, int, std::uint32_t, int, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, int, int)
    EI_ORIGINAL(0x004E9010)
__declspec(naked) inline char* __fastcall AllocVerticesOriginal(int) EI_ORIGINAL(0x004ECB70)
__declspec(naked) inline void __cdecl ReportError(long, const char*) EI_ORIGINAL(0x004F0B00)
__declspec(naked) inline void __fastcall LoadTextureOriginal(void** /*holder*/, void*, int /*kind*/, const char* /*name*/)
    EI_ORIGINAL(0x00500A00)
__declspec(naked) inline char* __fastcall ConstructBone(void*) EI_ORIGINAL(0x00506710)

// 00500960: the display / renderer object (the global at 007AF060; plain RET, no arguments).
// 00468F70: 16-byte aligned scratch allocation of `count` 16-byte records (ECX, plain RET).
__declspec(naked) inline void* __cdecl DisplayObject() EI_ORIGINAL(0x00500960)
__declspec(naked) inline char* __fastcall AllocAligned16(int /*count*/) EI_ORIGINAL(0x00468F70)
// 004ECB50: 32-byte aligned allocation of `count` bytes (ECX, plain RET).
__declspec(naked) inline char* __fastcall AllocAligned32Original(int /*count*/) EI_ORIGINAL(0x004ECB50)
