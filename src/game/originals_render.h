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
__declspec(naked) inline void __cdecl ReportError(long, const char*) EI_ORIGINAL(0x004F0B00)
__declspec(naked) inline void __fastcall LoadTextureOriginal(void** /*holder*/, void*, int /*kind*/, const char* /*name*/)
    EI_ORIGINAL(0x00500A00)
// 004EA4A0 (device_shadow.cpp, thiscall on the device object at the renderer's +0x20, RET 0x24): the
// flat geometry draw [positions (blocks of four), uvs, blocks, indices, index count, colour, specular,
// bounds (null: crossing), z scale]; 2 when nothing is visible, else 0. Called through its address so
// its verify wrapper stays in the path.
__declspec(naked) inline int __fastcall DeviceDrawFlatOriginal(void* /*device*/, void*, const void* /*positions*/,
                                                               const void* /*uvs*/, int /*blocks*/,
                                                               const void* /*indices*/, int /*indexCount*/,
                                                               std::uint32_t /*colour*/, std::uint32_t /*specular*/,
                                                               const void* /*bounds*/, float /*zScale*/)
    EI_ORIGINAL(0x004EA4A0)

// 00500960: the display / renderer object (the global at 007AF060; plain RET, no arguments).
// 00468F70: 16-byte aligned scratch allocation of `count` 16-byte records (ECX, plain RET).
__declspec(naked) inline void* __cdecl DisplayObject() EI_ORIGINAL(0x00500960)
// The same object without the call: the scene context the draw slots take (007AF060).
inline void* SceneContext() { return reinterpret_cast<void*>(0x007AF060); }
// 004D9880: 4x4 matrix product out = a * b (ECX = out, RET 8, no result).
__declspec(naked) inline void __fastcall MatrixMultiplyOriginal(float* /*out*/, void*, const float* /*a*/,
                                                                const float* /*b*/) EI_ORIGINAL(0x004D9880)
// 0049E800 (rewritten as Quaternion::SetAxisAngle, quaternion.cpp; called through its address so the wrapper
// runs): the rotation by `angle` about `axis` (thiscall, RET 8, returns the quaternion).
__declspec(naked) inline Quaternion* __fastcall QuaternionSetAxisAngleOriginal(Quaternion* /*q*/, void*,
                                                                              const Vector3* /*axis*/, float /*angle*/)
    EI_ORIGINAL(0x0049E800)
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

// 004D9B80: pushes a scene matrix into slot `index` of the cache (RET 8: matrix, index; no result).
__declspec(naked) inline void __fastcall PushSceneMatrixOriginal(void* /*cache*/, void*, const float* /*matrix*/, int /*index*/)
    EI_ORIGINAL(0x004D9B80)

// C3k. 00500970: makes sure the texture behind a holder is loaded (plain RET; the manager loads it unless its flag 8 is
// set; a null texture is a fatal error; EAX is not a result).
__declspec(naked) inline void __fastcall TextureEnsureLoadedOriginal(void* /*holder*/) EI_ORIGINAL(0x00500970)

// C3l. 0068EAF0: the material manager enters a key in its hash map at +0xBC (when absent) and copies the stored 0x44-byte value
// to record +0x20 (RET 8: record, key; EAX 1, or 0 for a null record; no caller reads it).
__declspec(naked) inline int __fastcall MaterialRegisterOriginal(void* /*manager*/, void*, void* /*record*/,
                                                                 std::uint32_t /*key*/) EI_ORIGINAL(0x0068EAF0)
// 0068E970: the material manager makes a material of `type` (its slot 23 with id -1, flag 8) and copies the stored value of
// the material's key (+0x10) into it (RET 4; EAX the material, or 0).
__declspec(naked) inline std::uint8_t* __fastcall MaterialCreateOriginal(void* /*manager*/, void*, int /*type*/)
    EI_ORIGINAL(0x0068E970)

// A-U11 (renderer_lifetime.cpp). 0049B780: draws a line list (thiscall on the list, RET 8: pipeline, device object).
// 004EECF0: the device object's cooperative level (RET 8: full screen flag, screen rectangle).
__declspec(naked) inline void __fastcall LineListDrawOriginal(void* /*lines*/, void*, void* /*pipeline*/, void* /*device*/)
    EI_ORIGINAL(0x0049B780)
__declspec(naked) inline void __fastcall DeviceSetCooperativeLevelOriginal(void* /*device*/, void*, int /*fullScreen*/,
                                                                           const void* /*rect*/) EI_ORIGINAL(0x004EECF0)

// A-S13 (directdraw_holder.cpp, device_object.cpp, dx_surface.cpp): the parts of the device object's start-up, called
// through their addresses so each one's verify wrapper stays in the path. 004EDD70: the DirectDraw base's constructor
// (RET 0x24: window, driver GUID, full screen, width, height, bits per pixel, refresh rate, back buffers, for 3D; returns
// the object). 004EEA90: the DirectDraw object of a driver and its display modes (RET 4). 004EFEA0: a surface holder's size from its surface (plain RET; 1, or 0
// when the report of a failure said stop).
__declspec(naked) inline void* __fastcall DirectDrawBaseConstructOriginal(void* /*device*/, void*, void* /*window*/,
                                                                         void* /*driver*/, int /*fullScreen*/,
                                                                         int /*width*/, int /*height*/, int /*bits*/,
                                                                         int /*refreshRate*/, unsigned /*backBuffers*/,
                                                                         int /*for3D*/) EI_ORIGINAL(0x004EDD70)
__declspec(naked) inline void __fastcall DeviceCreateDirectDrawOriginal(void* /*device*/, void*, void* /*driver*/)
    EI_ORIGINAL(0x004EEA90)
__declspec(naked) inline int __fastcall DXSurfaceDescribeOriginal(void* /*holder*/) EI_ORIGINAL(0x004EFEA0)

// C3n. 004EB550 (rewritten as DeviceDrawIndexed; thiscall on the device, RET 0x1C): draws indexed primitives with the
// current material (primitive type, FVF, vertices, vertex count, 16-bit indices, index count, flags); returns 1.
// 004F16A0 (rewritten as CTransformPipeline::ClipTest; RET 4): 2 = the bounds are outside the view, 1 = crossing it.
__declspec(naked) inline int __fastcall DrawPrimitive(CTextureDevice* /*device*/, void*, int /*primitive*/, int /*fvf*/,
                                                      void* /*vertices*/, int /*vertexCount*/,
                                                      const std::uint16_t* /*indices*/, int /*indexCount*/, int /*flags*/)
    EI_ORIGINAL(0x004EB550)
