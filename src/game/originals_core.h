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

__declspec(naked) inline double __fastcall DotRaw(const Vector3* /*a*/, const Vector3* /*b*/) EI_ORIGINAL(0x00401E00)
__declspec(naked) inline double __fastcall LengthRaw(const Vector3* /*v*/) EI_ORIGINAL(0x00401E20)

// 0040A3A0: CRefObject::Release (decrements the 16-bit count at +4, deletes through vtable slot 0 at zero; plain
// RET, EAX is not a result). 0040F560: Quaternion::AxisY, writes the axis to `out` (RET 4) and returns it.
__declspec(naked) inline void __fastcall RefObjectRelease(void* /*object*/) EI_ORIGINAL(0x0040A3A0)
// A-E2. 00457720 (rewritten as CEngineObject::ReleaseAndDelete; plain RET): drops one reference of a counted
// object (the count at +4) and deletes it through slot 0 at zero.
__declspec(naked) inline void __fastcall ReleaseCountedOriginal(void* /*object*/) EI_ORIGINAL(0x00457720)
// C3n. 00419CD0 (rewritten as RefPtr::Assign; thiscall on the pointer slot, RET 4): the counted pointer in `slot` takes
// `object` (its count raised, the previous object released).
__declspec(naked) inline void __fastcall RefPtrAssign(void* /*slot*/, void*, void* /*object*/) EI_ORIGINAL(0x00419CD0)
