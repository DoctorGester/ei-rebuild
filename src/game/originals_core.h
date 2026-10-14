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
// C3m. 0040CDC0 (rewritten as AddVectors): out = a + b, each component one rounded add (ECX out; returns out).
__declspec(naked) inline Vector3* __fastcall VectorAdd(Vector3* /*out*/, const Vector3* /*a*/, const Vector3* /*b*/)
    EI_ORIGINAL(0x0040CDC0)
// C3m. 004413E0 (rewritten as ServerEntryBase::ConstructBase): the client base of a server entry (0x48 bytes).
__declspec(naked) inline void __fastcall ServerEntryConstructBase(net::ServerEntry* /*entry*/) EI_ORIGINAL(0x004413E0)
// C3n. 00401F00 / 00402220 (rewritten as TurnUpTowardsHook / SetHeadingHook; thiscall on the transform object, RET 8 / 4):
// turns the object up towards a direction (`reduced` 1: by the reduced angle), sets its heading from a direction.
__declspec(naked) inline void __fastcall TransformTurnUpTowards(CTransformObject* /*object*/, void*, const float* /*direction*/,
                                                                int /*reduced*/) EI_ORIGINAL(0x00401F00)
__declspec(naked) inline void __fastcall TransformSetHeading(CTransformObject* /*object*/, void*, const float* /*direction*/)
    EI_ORIGINAL(0x00402220)
// C3n. 00404920 / 00404960 (rewritten as ObjectListIterator::First / Next; thiscall on the 8-byte list iterator, RET 4 /
// plain): the first object of a manager's list, then each next one (null at the end). Base of the views.
struct ObjectIteratorOriginals {
    CEngineObject* First(CBaseManager* list);
    CEngineObject* Next();
};
__declspec(naked) inline CEngineObject* ObjectIteratorOriginals::First(CBaseManager*) EI_ORIGINAL(0x00404920)
__declspec(naked) inline CEngineObject* ObjectIteratorOriginals::Next() EI_ORIGINAL(0x00404960)
// C3n. 00419CD0 (rewritten as RefPtr::Assign; thiscall on the pointer slot, RET 4): the counted pointer in `slot` takes
// `object` (its count raised, the previous object released).
__declspec(naked) inline void __fastcall RefPtrAssign(void* /*slot*/, void*, void* /*object*/) EI_ORIGINAL(0x00419CD0)
// C3n. 00429B50 (rewritten as ObjectHolderAssign; thiscall on the slot, RET 4): a counted object pointer takes `object`
// (reference added, the old one released through its slot 4).
__declspec(naked) inline void __fastcall ReferenceAssign(CEngineObject** /*slot*/, void*, CEngineObject* /*object*/)
    EI_ORIGINAL(0x00429B50)
// C3n. 00455930 (rewritten as CEngineObject::DestructBase; thiscall, plain RET): the engine object's destructor body.
__declspec(naked) inline void __fastcall EngineObjectDestructBase(CEngineObject* /*object*/) EI_ORIGINAL(0x00455930)
// C3n. 00461360 (rewritten as TextKeyFromName; fastcall on the text): the numeric key of a name in the game's texts.
__declspec(naked) inline int __fastcall TextKeyOf(const char* /*name*/) EI_ORIGINAL(0x00461360)
// C3o. 00402B20 (rewritten as CBaseManager::ConstructBase; thiscall, plain RET): the manager base constructor; returns
// this (EAX = ECX at the end).
__declspec(naked) inline void* __fastcall ManagerConstructBaseOriginal(void* /*manager*/) EI_ORIGINAL(0x00402B20)
// C3o. 00405350 (rewritten as hash400::PositionsRemoveKey; thiscall, RET 4): an id -> pointer hash map (STLport
// hash_map<int, T*>) removes `*key`; EAX the removed value, or null when the key was not there.
__declspec(naked) inline void* __fastcall IdMapRemoveKeyOriginal(void* /*map*/, void*, const int* /*key*/)
    EI_ORIGINAL(0x00405350)
// C3o. 00419720 (rewritten as RefValueConstructBase; thiscall, plain RET, no argument besides ECX): sets the counted
// value base vtable; returns this.
__declspec(naked) inline void* __fastcall RefValueConstructOriginal(void* /*object*/) EI_ORIGINAL(0x00419720)
// C3o. 0041DD50 (rewritten as RefPtrFromRawCounted; thiscall, RET 4): a counted pointer constructed from a raw object
// (its count raised when not null); returns the pointer.
__declspec(naked) inline RefPtr* __fastcall RefPtrFromRawOriginal(RefPtr* /*p*/, void*, CRefObject* /*object*/)
    EI_ORIGINAL(0x0041DD50)
// C3o. 00448A10 (rewritten as ScaleVector; fastcall: ECX out, EDX v, RET 4 the float): out = v * s; returns out.
__declspec(naked) inline Vector3* __fastcall VectorScaleCopyOriginal(Vector3* /*out*/, const Vector3* /*v*/, float /*s*/)
    EI_ORIGINAL(0x00448A10)
