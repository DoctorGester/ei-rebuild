#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CItem;
class CMapObject;
class CUnit;
class CUnitServer;
class CWorldServer;
struct AnimationStep;
struct AnimationSteps;
struct AttackParams;
struct OrderPlan;
struct MoveModeState;
struct PathCell;
struct SightTarget;
struct UnitAction;
struct PartyUnitRecord;
struct UnitPrototypeTable;
struct UnitStatsBuilder;
struct UnitStats;
struct UnitVitals;

__declspec(naked) inline int __fastcall SkillRecordIndex(const void*, void*, const void*) EI_ORIGINAL(0x0051FF00)
__declspec(naked) inline void __fastcall UnitParameterBlock(void*, void*, void*, void*, int) EI_ORIGINAL(0x005270E0)
__declspec(naked) inline void __fastcall UnitDestructBody(void*) EI_ORIGINAL(0x0052E310)

// C3k. A counted reference is a pointer slot whose object keeps a count at +4 and has Release as virtual slot 4.
// 0052B030 releases the object and clears the slot (plain RET; no result in EAX).
__declspec(naked) inline void __fastcall CountedRefResetOriginal(void* /*slot*/) EI_ORIGINAL(0x0052B030)
// 0054B580 (rewritten as CUnitServer::IsFighting; Rebirth patches it, so callers go through the address): whether the
// controller's order counts as fighting (plain RET, AL).
__declspec(naked) inline bool __fastcall UnitServerIsFightingOriginal(void* /*controller*/) EI_ORIGINAL(0x0054B580)
// 00556D40: the cell list's RemoveAll (plain RET; next to its constructor 00556D10).

class CFigureAnimationState;
class CUnitClientSpecific;
struct FigureAnimation;
class CObject3DClientSpecific;

// A-U10 (map_object_animation_bones.cpp). Rewritten: 0050C230 FigureBone::Place (RET 4),
// 005ACB80 LoadFigureBones (thiscall on the figure manager, RET 8: model, bone;
// 1 / 0), 005AC880 LoadSkeletonLinks (thiscall on the figure manager, RET 8: state, model name; 1 or FatalError's).
// 006B7110 (thiscall on the sound system, RET 8): the names of its files matching `pattern` in every resource it
// searches, appended to the CStringArray `out` (emptied first). No result.
__declspec(naked) inline void __fastcall SoundSystemListFilesOriginal(void* /*sounds*/, void*, void* /*out*/,
                                                                      const char* /*pattern*/) EI_ORIGINAL(0x006B7110)

struct LeverRecord;   // lever.h
// 0052CA40 (thiscall on the lever table 0079C1A0, RET 4): the record of model `name` (its map's
// operator[], STLport library code: an unknown name adds a null record).
struct LockKey;   // unit.h
// 00522A80 LockKey::Opens (unit_stats.cpp; Rebirth's FixLeversActivate patches it, so callers go
// through its address): thiscall on the key, RET 4, BOOL.
__declspec(naked) inline int __fastcall LockOpensOriginal(const LockKey* /*key*/, void*, const LockKey* /*lock*/)
    EI_ORIGINAL(0x00522A80)
class CTorchObjectSpec;   // server_specific_manager.h
struct DeltaField;        // net_delta.h


// C3o. Map object / unit statistics originals whose copies differed in their return type.
// 00517700 (rewritten as CObject3D::SetFigureScale; RET 4): sets the scale (+0xC4) and rebuilds the figure when it
// changed; EAX as the original leaves it (00532AB0 CUnit::ResetUnitScale returns it).
__declspec(naked) inline std::uintptr_t __fastcall ObjectSetFigureScaleOriginal(void* /*object*/, void*, const float* /*scale*/)
    EI_ORIGINAL(0x00517700)
// 00524CD0 (rewritten as UnitStats::ResetToBase; RET 4): the current values from the bases, with the load (a float).
__declspec(naked) inline void __fastcall UnitStatsResetToBaseOriginal(void* /*stats*/, void*, float /*load*/)
    EI_ORIGINAL(0x00524CD0)
// 00525AF0 (rewritten as UnitStats::RefillSlots; plain RET): every slot of kind 2 or more back to its maximum; EAX the
// end of the slot array.
__declspec(naked) inline void* __fastcall UnitStatsRefillSlotsOriginal(void* /*stats*/) EI_ORIGINAL(0x00525AF0)
