#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CUnit;
class CUnitServer;
class CWorldServer;
struct AnimationStep;
struct AnimationSteps;
struct AttackParams;
struct OrderPlan;
struct PathCell;
struct UnitAction;

// 005287A0 / 00528800: UnitStats Load / Save (RET 4: the stream; no result).
__declspec(naked) inline void __fastcall UnitStatsLoadOriginal(void* /*stats*/, void*, void* /*stream*/) EI_ORIGINAL(0x005287A0)
__declspec(naked) inline void __fastcall UnitStatsSaveOriginal(void* /*stats*/, void*, void* /*stream*/) EI_ORIGINAL(0x00528800)
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
// 00557E70: destructor of the animation-step array / controller list (ECX = object, plain RET, no result).
__declspec(naked) inline void __fastcall AnimationStepsDestroyOriginal(void* /*steps*/) EI_ORIGINAL(0x00557E70)

// C3h. 005300D0: unit.SetController(player) (RET 4, EAX not a result).
__declspec(naked) inline void __fastcall UnitSetControllerOriginal(void* /*unit*/, void*, void* /*player*/)
    EI_ORIGINAL(0x005300D0)
// 00530280: the ground (or water surface) height under (x, y) for the unit, in ST0 (RET 8).
__declspec(naked) inline double __fastcall UnitGroundHeightOriginal(void* /*unit*/, void*, float /*x*/, float /*y*/)
    EI_ORIGINAL(0x00530280)

// C3j. 00544CE0 / 00544D50: AnimationSteps adds a step (kind byte, value; RET 8) / an emote step (kind byte; RET 4); no result.
__declspec(naked) inline void __fastcall StepsAddStepOriginal(void* /*steps*/, void*, int /*kind*/, int /*value*/)
    EI_ORIGINAL(0x00544CE0)
__declspec(naked) inline void __fastcall StepsEmoteOriginal(void* /*steps*/, void*, int /*kind*/) EI_ORIGINAL(0x00544D50)
// 00544E70: AnimationSteps equal-length test against another (RET 4, AL).
__declspec(naked) inline bool __fastcall StepsQueueHoldsOriginal(void* /*steps*/, void*, const void* /*other*/)
    EI_ORIGINAL(0x00544E70)
// 00556870: AnimationSteps copy constructor (RET 4, returns this).
__declspec(naked) inline void* __fastcall StepsCopyConstructOriginal(void* /*steps*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x00556870)
// 0054C8B0: unit controller checks an order against its state (RET 4, AL).
__declspec(naked) inline bool __fastcall UnitCheckOrderOriginal(void* /*controller*/, void*, const void* /*action*/)
    EI_ORIGINAL(0x0054C8B0)
// 00556D10: cell list constructor (block size; RET 4, returns this).
__declspec(naked) inline void* __fastcall CellListConstructOriginal(void* /*list*/, void*, int /*blockSize*/)
    EI_ORIGINAL(0x00556D10)
// 0059F3D0: sets a figure part's owner object (stores at +0x98; RET 4, no result).
__declspec(naked) inline void __fastcall FigurePartSetOwnerOriginal(void* /*part*/, void*, void* /*owner*/)
    EI_ORIGINAL(0x0059F3D0)

// C3k. A counted reference is a pointer slot whose object keeps a count at +4 and has Release as virtual slot 4.
// 0052B030 releases the object and clears the slot (plain RET); 0052A8F0 assigns an object unless it is the one
// already held (RET 4); 00511B90 stores the object, adds a reference and then releases the old one (RET 4). None of
// them leaves a result in EAX.
__declspec(naked) inline void __fastcall CountedRefResetOriginal(void* /*slot*/) EI_ORIGINAL(0x0052B030)
__declspec(naked) inline void __fastcall CountedRefAssignOriginal(void* /*slot*/, void*, void* /*object*/)
    EI_ORIGINAL(0x0052A8F0)
__declspec(naked) inline void __fastcall CountedRefStoreOriginal(void* /*slot*/, void*, void* /*object*/)
    EI_ORIGINAL(0x00511B90)
// 0050E220 / 0050E290: a counted reference's Load(stream, context) (RET 8: the id is read from the stream and resolved
// in `context`) and Save(stream) (RET 4: the object's id at +0xC, or -1); EAX is not a result.
__declspec(naked) inline void __fastcall CountedRefLoadOriginal(void* /*ref*/, void*, void* /*stream*/, void* /*context*/)
    EI_ORIGINAL(0x0050E220)
__declspec(naked) inline void __fastcall CountedRefSaveOriginal(void* /*ref*/, void*, void* /*stream*/)
    EI_ORIGINAL(0x0050E290)
// 0050EA40: runs the object's virtual slot 50 between a begin / end pair on the member at +0x1C4, then clears bit 1 of
// the flags at +8 (plain RET, EAX not a result). 005179E0: sets up the object's animated figure for a model name (RET 4;
// EAX not a result). 0050D450: applies the pending animation change of a figure state once (flag at +0x74; plain RET).
__declspec(naked) inline void __fastcall ObjectPrepareOriginal(void* /*object*/) EI_ORIGINAL(0x0050EA40)
__declspec(naked) inline void __fastcall ObjectSetupFigureOriginal(void* /*object*/, void*, const char* /*model*/)
    EI_ORIGINAL(0x005179E0)
__declspec(naked) inline void __fastcall FigureApplyPendingOriginal(void* /*figure state*/) EI_ORIGINAL(0x0050D450)
// 0050F7A0 / 0050F900: the map object part of Load(stream) / Save(stream) (both RET 4, EAX not a result).
__declspec(naked) inline void __fastcall MapObjectLoadOriginal(void* /*object*/, void*, void* /*stream*/)
    EI_ORIGINAL(0x0050F7A0)
__declspec(naked) inline void __fastcall MapObjectSaveOriginal(void* /*object*/, void*, void* /*stream*/)
    EI_ORIGINAL(0x0050F900)
// 00516A80: the part of the object whose name matches (case-insensitive; searches the two arrays at +0xD0 / +0xE0)
// (RET 4: the name; EAX the part or null). 00530830: the unit's worn item in a slot (RET 4: the slot; EAX = the item at
// +0x480 + slot * 4, or null).
__declspec(naked) inline CEngineObject* __fastcall ObjectFindPartOriginal(void* /*object*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x00516A80)
__declspec(naked) inline CEngineObject* __fastcall UnitWornItemOriginal(const void* /*unit*/, void*, int /*slot*/)
    EI_ORIGINAL(0x00530830)
// 0052E970: the unit's diplomacy / hostile mask (stored at +0x244, the object at +0x240 told through virtual slot 38,
// flag 0x80000 set, part 0xB marked changed; RET 4, EAX not a result).
__declspec(naked) inline void __fastcall UnitSetHostileMaskOriginal(void* /*unit*/, void*, std::uint32_t /*mask*/)
    EI_ORIGINAL(0x0052E970)
// 00556D40: the cell list's RemoveAll (plain RET; next to its constructor 00556D10). 00557B60: AnimationSteps
// constructor (plain RET, returns `this`).
__declspec(naked) inline void __fastcall CellListRemoveAllOriginal(void* /*list*/) EI_ORIGINAL(0x00556D40)
__declspec(naked) inline void* __fastcall StepsConstructOriginal(void* /*steps*/) EI_ORIGINAL(0x00557B60)

// C3l. 005335E0: the unit prototype map (a hash map keyed by a string; RET 4: key; EAX = the address of the value slot, after
// inserting an entry for a missing name). 0051FE80: SkillDatabase::IndexOf(name) over the 0x44-byte records between +0x0C and +0x10
// (RET 4; EAX = the index, or -1).
__declspec(naked) inline void** __fastcall PrototypeMapAtOriginal(void* /*map*/, void*, const void* /*key*/)
    EI_ORIGINAL(0x005335E0)
__declspec(naked) inline int __fastcall SkillIndexOfOriginal(const void* /*database*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x0051FE80)
// U11. Figure methods of map objects (the 3D object base). 00518F80: animation entry `key & 0xFF` of the figure's
// animation set (0x58-byte entries from set + 0x2C, the set loaded on first use by 00518CA0; RET 4, EAX the entry or
// null). 00517990: the index of the part named `name` (_mbscmp, the names at +0x114 / +0x118; RET 4, -1 when
// none); it indexes the part boxes at +0x160. 005188B0: a part's position in the world (RET 8: out, part; returns out).
__declspec(naked) inline char* __fastcall ObjectAnimationEntryOriginal(const void* /*object*/, void*, std::uint32_t /*key*/)
    EI_ORIGINAL(0x00518F80)
__declspec(naked) inline int __fastcall ObjectFindPartIndexOriginal(const void* /*object*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x00517990)
__declspec(naked) inline float* __fastcall ObjectPartPositionOriginal(const void* /*object*/, void*, float* /*out*/,
                                                                      void* /*part*/) EI_ORIGINAL(0x005188B0)
// A-U2. The figure's animation state (map_object_animation_state.cpp, rewritten; called through their game addresses so
// the verify wrappers see the calls): 0050CA50 plays an animation entry (RET 4), 0050CBA0 queues one (RET 4; EAX the
// list node), 0050D5E0 advances by a number of frames (RET 4). 00535480: the client controller starts the unit's idle
// animation (RET 4: play (byte) or queue; unit_client_animation.cpp). No results unless noted.
class CFigureAnimationState;
class CUnitClientSpecific;
struct FigureAnimation;
__declspec(naked) inline void __fastcall FigurePlayOriginal(CFigureAnimationState* /*state*/, void*,
                                                            const void* /*animation*/) EI_ORIGINAL(0x0050CA50)
__declspec(naked) inline void* __fastcall FigureQueueOriginal(CFigureAnimationState* /*state*/, void*,
                                                              const void* /*animation*/) EI_ORIGINAL(0x0050CBA0)
__declspec(naked) inline void __fastcall FigureAdvanceOriginal(CFigureAnimationState* /*state*/, void*,
                                                               float /*frames*/) EI_ORIGINAL(0x0050D5E0)
__declspec(naked) inline void __fastcall ClientStartIdleAnimationOriginal(CUnitClientSpecific* /*controller*/, void*,
                                                                          int /*play*/) EI_ORIGINAL(0x00535480)
