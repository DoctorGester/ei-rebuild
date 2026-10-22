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
