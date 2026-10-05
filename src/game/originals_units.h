#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CUnit;
class CUnitServer;
class CWorldServer;
struct AnimationStep;
struct AnimationSteps;
struct AttackParams;
struct OrderPlan;
struct PathCell;
struct UnitAction;

__declspec(naked) inline int __fastcall PrototypeClass(void*, void*, const char*) EI_ORIGINAL(0x00513240)
__declspec(naked) inline int __fastcall SkillRecordIndex(const void*, void*, const void*) EI_ORIGINAL(0x0051FF00)
__declspec(naked) inline void __fastcall UnitParameterBlock(void*, void*, void*, void*, int) EI_ORIGINAL(0x005270E0)
__declspec(naked) inline void __fastcall DifficultyScaleParams(AttackParams*, void*, int /*keyed*/)
    EI_ORIGINAL(0x0052E0D0)
__declspec(naked) inline void __fastcall UnitDestructBody(void*) EI_ORIGINAL(0x0052E310)
__declspec(naked) inline void __fastcall UnitPathSpeeds(CUnit*) EI_ORIGINAL(0x0052E9D0)
__declspec(naked) inline void __fastcall UnitSetAnimation(CUnit*, void*, int /*animation*/, int /*variant*/, int /*time*/, int /*flags*/)
    EI_ORIGINAL(0x0052EA90)
// 0050E9F0 (rewritten as CUnit::MarkChanged; RET 4): sets the changed bit of a part (-1: none) and the
// object's dirty flag. EAX on return is the object's world (null when there is none), which tail-calling
// rewrites hand on.
__declspec(naked) inline void* __fastcall UnitMarkChanged(void* /*unit*/, void*, int /*part*/) EI_ORIGINAL(0x0050E9F0)
__declspec(naked) inline int __fastcall HoverHostileTo(CUnit*, void*, CUnit*) EI_ORIGINAL(0x00530150)
__declspec(naked) inline char* __fastcall UnitFindAnimation(CUnit*, void*, std::uint32_t, int) EI_ORIGINAL(0x005301B0)
__declspec(naked) inline char* __fastcall UnitAnimationEntry(CUnit*, void*, std::uint32_t) EI_ORIGINAL(0x00530240)
__declspec(naked) inline double __fastcall UnitSize(CUnit*) EI_ORIGINAL(0x00530560)
// 00530930 (rewritten as CUnit::GetPrototype; plain RET): the unit's prototype record (0x120 bytes, name first), or null.
__declspec(naked) inline void* __fastcall UnitPrototypeOf(const void* /*unit*/) EI_ORIGINAL(0x00530930)
__declspec(naked) inline void __fastcall UnitUpdateSizeClass(CUnit*) EI_ORIGINAL(0x00530570)
__declspec(naked) inline void __stdcall ConstructPathCells(PathCell*, int) EI_ORIGINAL(0x00539980)
__declspec(naked) inline void __fastcall PlanReset(OrderPlan*) EI_ORIGINAL(0x00544FD0)
__declspec(naked) inline void __fastcall ActionClear(UnitAction*) EI_ORIGINAL(0x00545F40)
__declspec(naked) inline void __fastcall CtlSetStanding(CUnitServer*, void*, int) EI_ORIGINAL(0x005467B0)
__declspec(naked) inline void __fastcall CtlLiftNearby(CUnitServer*, void*, void*, const float*, int)
    EI_ORIGINAL(0x005468D0)
__declspec(naked) inline void __fastcall CtlShowCurrentStep(CUnitServer*) EI_ORIGINAL(0x00546C10)
__declspec(naked) inline void __fastcall CtlSetState(CUnitServer*, void*, int) EI_ORIGINAL(0x00547090)
__declspec(naked) inline void __fastcall CtlStandStill(CUnitServer*) EI_ORIGINAL(0x00547280)
__declspec(naked) inline void __fastcall StepsCopyB(AnimationSteps*, void*, const AnimationSteps*)
    EI_ORIGINAL(0x005483C0)
__declspec(naked) inline void __fastcall CtlUpdateSneak(CUnitServer*) EI_ORIGINAL(0x00548520)
__declspec(naked) inline bool __fastcall TickPathTo(CUnitServer*, void*, const float*, std::uint32_t)
    EI_ORIGINAL(0x00548700)
__declspec(naked) inline void __fastcall TickOrderDone(CUnitServer*, void*, int) EI_ORIGINAL(0x00549690)
__declspec(naked) inline double __fastcall CtlActionDistance(CUnitServer*, void*, const UnitAction*)
    EI_ORIGINAL(0x0054C070)
__declspec(naked) inline bool __fastcall CtlCanReachTarget(CUnitServer*, void*, const UnitAction*)
    EI_ORIGINAL(0x0054C800)
__declspec(naked) inline void __fastcall CtlReportPath(CUnitServer*, void*, void*, int) EI_ORIGINAL(0x0054EB30)
__declspec(naked) inline AnimationSteps* __fastcall CtlIdleAnimation(CUnitServer*) EI_ORIGINAL(0x00550F30)
__declspec(naked) inline void __fastcall CtlBuildMoveSteps(CUnitServer*, void*, AnimationSteps*, int)
    EI_ORIGINAL(0x005511A0)
__declspec(naked) inline AnimationSteps* __fastcall CtlNextAnimation(CUnitServer*) EI_ORIGINAL(0x00551400)
__declspec(naked) inline void __fastcall DataGainExperience(void*, void*, float) EI_ORIGINAL(0x00553380)
__declspec(naked) inline void __fastcall StepsAppend(AnimationSteps*, void*, const AnimationSteps*)
    EI_ORIGINAL(0x00556950)
__declspec(naked) inline int __fastcall StepsAdd(AnimationSteps*, void*, const AnimationStep*) EI_ORIGINAL(0x00557CC0)
__declspec(naked) inline void __fastcall StepsSetAtGrow(AnimationSteps*, void*, int, const AnimationStep*)
    EI_ORIGINAL(0x00558950)
__declspec(naked) inline void __fastcall WorldTimeSettings(CWorldServer*, void*, float* /*time*/, float* /*speed*/)
    EI_ORIGINAL(0x0055FBD0)

// 00530730 / 00531070: equip armour (RET 4, returns the item it replaced or null) / add weapon (RET 4, slot index,
// 1 when already there, -1 when full). 00554450: the unit controller resyncs with its unit (plain RET, no result).
__declspec(naked) inline void* __fastcall UnitEquipArmourOriginal(void* /*unit*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00530730)
__declspec(naked) inline int __fastcall UnitAddWeaponOriginal(void* /*unit*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00531070)
__declspec(naked) inline void __fastcall ControllerRepositionOriginal(void* /*controller*/) EI_ORIGINAL(0x00554450)
