#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>

class CAnimationNode;   // figure.h
namespace scene { struct SceneNode; struct SceneSkin; }

__declspec(naked) inline int __fastcall DrawMesh(char*, void*, const void*, int, std::uint32_t, int, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t, int, int)
    EI_ORIGINAL(0x004E9010)
// 004E9A50: the same with two textures' surfaces before the positions (RET 0x50).
class CTextureDevice;
class CTransformPipeline;
struct ClipBounds;
struct CDXSurface;
__declspec(naked) inline int __fastcall DrawMesh2(CTextureDevice*, void*, CDXSurface*, CDXSurface*, const void*, int,
                                                  std::uint32_t, int, std::uint32_t, std::uint32_t, std::uint32_t,
                                                  std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                                  std::uint32_t, std::uint32_t, std::uint32_t, std::uint32_t,
                                                  std::uint32_t, int, int) EI_ORIGINAL(0x004E9A50)

// 00500960: the display / renderer object (the global at 007AF060; plain RET, no arguments).
// 00468F70: 16-byte aligned scratch allocation of `count` 16-byte records (ECX, plain RET).
// The same object without the call: the scene context the draw slots take (007AF060).
inline void* SceneContext() { return reinterpret_cast<void*>(0x007AF060); }
// 004D9880: 4x4 matrix product out = a * b (ECX = out, RET 8, no result).
__declspec(naked) inline void __fastcall MatrixMultiplyOriginal(float* /*out*/, void*, const float* /*a*/,
                                                                const float* /*b*/) EI_ORIGINAL(0x004D9880)
// C3i. 004D9AA0: quaternion (ECX) into a 4x4 matrix `out` (RET 4, no result).
__declspec(naked) inline void __fastcall QuaternionToMatrixOriginal(const void* /*quaternion*/, void*, float* /*out*/)
    EI_ORIGINAL(0x004D9AA0)
// 004EF610 (rewritten as DevicePresent, device_present.cpp): the device object's present (ECX = device object
// 007AF080, plain RET, the DirectDraw result).
__declspec(naked) inline int __fastcall DevicePresentOriginal(void* /*device*/) EI_ORIGINAL(0x004EF610)

// 004D9B80: pushes a scene matrix into slot `index` of the cache (RET 8: matrix, index; no result).
__declspec(naked) inline void __fastcall PushSceneMatrixOriginal(void* /*cache*/, void*, const float* /*matrix*/, int /*index*/)
    EI_ORIGINAL(0x004D9B80)


// C3n. 004EB550 (rewritten as DeviceDrawIndexed; thiscall on the device, RET 0x1C): draws indexed primitives with the
// current material (primitive type, FVF, vertices, vertex count, 16-bit indices, index count, flags); returns 1.
// 004F16A0 (rewritten as CTransformPipeline::ClipTest; RET 4): 2 = the bounds are outside the view, 1 = crossing it.
