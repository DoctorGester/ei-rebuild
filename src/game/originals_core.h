#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
struct RefPtr;
struct Quaternion;
struct Vector3;

__declspec(naked) inline double __fastcall DotRaw(const Vector3* /*a*/, const Vector3* /*b*/) EI_ORIGINAL(0x00401E00)
__declspec(naked) inline double __fastcall LengthRaw(const Vector3* /*v*/) EI_ORIGINAL(0x00401E20)
__declspec(naked) inline Vector3* __fastcall Negate(Vector3* /*out*/, const Vector3* /*v*/) EI_ORIGINAL(0x00402200)
// 00403F40 (rewritten as CBaseManager::FindObject): the manager's object with this id, or null.
__declspec(naked) inline CEngineObject* __fastcall ManagerFindObject(void* /*manager*/, void*, int /*id*/)
    EI_ORIGINAL(0x00403F40)
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

// 0040A3A0: CRefObject::Release (decrements the 16-bit count at +4, deletes through vtable slot 0 at zero; plain
// RET, EAX is not a result). 0040F560: Quaternion::AxisY, writes the axis to `out` (RET 4) and returns it.
__declspec(naked) inline void __fastcall RefObjectRelease(void* /*object*/) EI_ORIGINAL(0x0040A3A0)
__declspec(naked) inline Vector3* __fastcall QuaternionAxisY(const Quaternion* /*quaternion*/, void*, Vector3* /*out*/)
    EI_ORIGINAL(0x0040F560)
// 0040D940 / 0040D6B0: Quaternion::AxisX / AxisZ, the same way (RET 4, return out).
__declspec(naked) inline Vector3* __fastcall QuaternionAxisX(const Quaternion* /*quaternion*/, void*, Vector3* /*out*/)
    EI_ORIGINAL(0x0040D940)
__declspec(naked) inline Vector3* __fastcall QuaternionAxisZ(const Quaternion* /*quaternion*/, void*, Vector3* /*out*/)
    EI_ORIGINAL(0x0040D6B0)
// 0059F030: CFigure's destructor body (plain RET; CFigure's slot 0 00453470 and CSkyObject's 004D92B0 end with it).
__declspec(naked) inline void __fastcall FigureDestructBodyOriginal(void* /*figure*/) EI_ORIGINAL(0x0059F030)
// A-E2. 00457720 (rewritten as CEngineObject::ReleaseAndDelete; plain RET): drops one reference of a counted
// object (the count at +4) and deletes it through slot 0 at zero.
__declspec(naked) inline void __fastcall ReleaseCountedOriginal(void* /*object*/) EI_ORIGINAL(0x00457720)
