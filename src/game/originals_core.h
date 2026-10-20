#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CBaseManager;
class CEngineObject;
class CRefObject;
class CTransformObject;
struct RefPtr;
struct Quaternion;
struct Vector3;
namespace net { class ServerEntry; }

// 004018A0 (rewritten as Quaternion::PreMultiply): *q = r * *q through the game's address (the shadow's
// turn, map_object_shadow.cpp; the harness's shadow turn patch, harness_shadow_turn.h).
__declspec(naked) inline void __fastcall PreMultiplyAt(Quaternion* /*q*/, void*, const Quaternion* /*r*/)
    EI_ORIGINAL(0x004018A0)
__declspec(naked) inline double __fastcall DotRaw(const Vector3* /*a*/, const Vector3* /*b*/) EI_ORIGINAL(0x00401E00)
__declspec(naked) inline double __fastcall LengthRaw(const Vector3* /*v*/) EI_ORIGINAL(0x00401E20)
__declspec(naked) inline Vector3* __fastcall Negate(Vector3* /*out*/, const Vector3* /*v*/) EI_ORIGINAL(0x00402200)
// 00403F40 (rewritten as CBaseManager::FindObject): the manager's object with this id, or null.
__declspec(naked) inline CEngineObject* __fastcall ManagerFindObject(void* /*manager*/, void*, int /*id*/)
    EI_ORIGINAL(0x00403F40)
__declspec(naked) inline void __fastcall ConstructEngineObject(void* /*object*/) EI_ORIGINAL(0x0040CB20)
__declspec(naked) inline void __fastcall RefNullBOriginal(RefPtr* /*ref*/) EI_ORIGINAL(0x0041FF70)
__declspec(naked) inline void __fastcall RefNullA(RefPtr* /*ref*/) EI_ORIGINAL(0x0041FF80)
__declspec(naked) inline void* __fastcall RefPtrDestroyAt(RefPtr* /*p*/, void*, unsigned /*flags*/)
    EI_ORIGINAL(0x004292A0)

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
// C3m. 0040CDC0 (rewritten as AddVectors): out = a + b, each component one rounded add (ECX out; returns out).
__declspec(naked) inline Vector3* __fastcall VectorAdd(Vector3* /*out*/, const Vector3* /*a*/, const Vector3* /*b*/)
    EI_ORIGINAL(0x0040CDC0)
// C3n. 00401F00 / 00402220 (rewritten as TurnUpTowardsHook / SetHeadingHook; thiscall on the transform object, RET 8 / 4):
// turns the object up towards a direction (`reduced` 1: by the reduced angle), sets its heading from a direction.
__declspec(naked) inline void __fastcall TransformSetHeading(CTransformObject* /*object*/, void*, const float* /*direction*/)
    EI_ORIGINAL(0x00402220)
// C3n. 00419CD0 (rewritten as RefPtr::Assign; thiscall on the pointer slot, RET 4): the counted pointer in `slot` takes
// `object` (its count raised, the previous object released).
__declspec(naked) inline void __fastcall RefPtrAssign(void* /*slot*/, void*, void* /*object*/) EI_ORIGINAL(0x00419CD0)
// C3n. 00455930 (rewritten as CEngineObject::DestructBase; thiscall, plain RET): the engine object's destructor body.
__declspec(naked) inline void __fastcall EngineObjectDestructBase(CEngineObject* /*object*/) EI_ORIGINAL(0x00455930)
