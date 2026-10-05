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
// 004D9880: 4x4 matrix product out = a * b (ECX = out, RET 8, no result).
__declspec(naked) inline void __fastcall MatrixMultiplyOriginal(float* /*out*/, void*, const float* /*a*/,
                                                                const float* /*b*/) EI_ORIGINAL(0x004D9880)
// C3i. 004D9AA0: quaternion (ECX) into a 4x4 matrix `out` (RET 4, no result).
__declspec(naked) inline void __fastcall QuaternionToMatrixOriginal(const void* /*quaternion*/, void*, float* /*out*/)
    EI_ORIGINAL(0x004D9AA0)
// 004EF7B0 (rewritten as DXSurfaceConstruct, dx_surface.cpp; called through its address so the wrapper runs):
// the CDXSurface constructor on `memory` for an off-screen surface of the screen's format (thiscall: device
// object, width, height; RET 0xC; returns `memory`).
__declspec(naked) inline char* __fastcall DXSurfaceConstructOriginal(void* /*memory*/, void*, void* /*device*/,
                                                                      int /*width*/, int /*height*/) EI_ORIGINAL(0x004EF7B0)
// 004EFC60 (rewritten as DXSurfaceFromPicture, dx_surface.cpp; called through its address so the wrapper runs):
// the CDXSurface constructor on `memory` for an 8888 .mmp picture (thiscall: device object, the picture's file
// data, flag (low byte: video memory); RET 0xC; returns `memory`, throws when the picture cannot be loaded).
__declspec(naked) inline void* __fastcall DXSurfaceFromPictureOriginal(void* /*memory*/, void*, void* /*device*/,
                                                                        const void* /*picture*/, int /*flag*/)
    EI_ORIGINAL(0x004EFC60)
// 004EFA60: the CDXSurface constructor on `memory` from a DDSURFACEDESC2 (thiscall: device object, description;
// RET 8; returns `memory`).
__declspec(naked) inline void* __fastcall DXSurfaceFromDescOriginal(void* /*memory*/, void*, void* /*device*/,
                                                                     const std::uint32_t* /*desc*/) EI_ORIGINAL(0x004EFA60)
// 00501A40: the renderer's clear of the render target (and depth) with its colour +0x14 (ECX = the renderer
// 007AF060, plain RET, EAX 1).
__declspec(naked) inline int __fastcall RendererClearOriginal(void* /*renderer*/) EI_ORIGINAL(0x00501A40)
// 004EF610 (rewritten as DevicePresent, device_present.cpp): the device object's present (ECX = device object
// 007AF080, plain RET, the DirectDraw result).
__declspec(naked) inline int __fastcall DevicePresentOriginal(void* /*device*/) EI_ORIGINAL(0x004EF610)

// C3j. 004E7840 / 004E7A40: flush a device's batch A / B (plain RET, no result).
__declspec(naked) inline void __fastcall DeviceFlushBatchAOriginal(void* /*device*/) EI_ORIGINAL(0x004E7840)
__declspec(naked) inline void __fastcall DeviceFlushBatchBOriginal(void* /*device*/) EI_ORIGINAL(0x004E7A40)
// 004E7E00: draws a vertex buffer with 16-bit indices (RET 8: indices, count; no result).
__declspec(naked) inline void __fastcall DeviceDrawVertexBufferOriginal(void* /*device*/, void*, const void* /*indices*/,
                                                                        int /*count*/) EI_ORIGINAL(0x004E7E00)
// 004D9B80: pushes a scene matrix into slot `index` of the cache (RET 8: matrix, index; no result).
__declspec(naked) inline void __fastcall PushSceneMatrixOriginal(void* /*cache*/, void*, const float* /*matrix*/, int /*index*/)
    EI_ORIGINAL(0x004D9B80)
// 004F65B0: allocates `count` 8-byte elements, 32-byte aligned (ECX = count, plain RET, EAX = memory).
__declspec(naked) inline void* __fastcall AllocAligned32Of8Original(int /*count*/) EI_ORIGINAL(0x004F65B0)
// 004E6BA0: drops one reference of a surface holder and deletes it at zero (plain RET, no result).
__declspec(naked) inline void __fastcall SurfaceHolderReleaseOriginal(void* /*holder*/) EI_ORIGINAL(0x004E6BA0)

// C3k. 00500970: makes sure the texture behind a holder is loaded (plain RET; the manager loads it unless its flag 8 is
// set; a null texture is a fatal error; EAX is not a result). 00501AF0: binds a texture to a stage (RET 8: stage, texture
// object or null; EAX is always 1, no caller reads it).
__declspec(naked) inline void __fastcall TextureEnsureLoadedOriginal(void* /*holder*/) EI_ORIGINAL(0x00500970)
__declspec(naked) inline int __fastcall DeviceSetTextureOriginal(void* /*device*/, void*, int /*stage*/, void* /*texture*/)
    EI_ORIGINAL(0x00501AF0)
