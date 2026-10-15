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

// 0050EAB0 (RET 8): the map object reads its network state (its slot 49 between the state's begin
// / end; `delta` 0 the full state, 1 a delta) from a memory stream. Nonzero when it was read.
__declspec(naked) inline int __fastcall MapObjectReadStateOriginal(void* /*object*/, void*, int /*delta*/,
                                                                   void* /*from*/) EI_ORIGINAL(0x0050EAB0)
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
// 00555840 (the unit's server controller, CUnit + 0x240; plain RET): the controller drops its action and orders (state 6,
// commands 9). 00555700 (RET 4): it plays the animation named by its argument (callers pass a FigureAnimation, whose
// name comes first, or a name) as its action. Both rewritten (CUnitServer::StopAction / PlayAnimation).
__declspec(naked) inline void __fastcall ControllerStopActionOriginal(void* /*controller*/) EI_ORIGINAL(0x00555840)
__declspec(naked) inline void __fastcall ControllerPlayAnimationOriginal(void* /*controller*/, void*, const void* /*animation*/)
    EI_ORIGINAL(0x00555700)
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
// 0052AF20 (fastcall: ECX the list, EDX the stream; RET 4): reads a count, then appends that many counted references to
// the CList of objects, each loaded with 0050E220 in `context`. EAX is not a result.
__declspec(naked) inline void __fastcall CountedListLoadOriginal(void* /*list*/, void* /*stream*/, void* /*context*/)
    EI_ORIGINAL(0x0052AF20)
// 0050EA40: runs the object's virtual slot 50 between a begin / end pair on the member at +0x1C4, then clears bit 1 of
// the flags at +8 (plain RET, EAX not a result). 005179E0: sets up the object's animated figure for a model name (RET 4;
// EAX not a result). 0050D450: applies the pending animation change of a figure state once (flag at +0x74; plain RET).
__declspec(naked) inline void __fastcall ObjectPrepareOriginal(void* /*object*/) EI_ORIGINAL(0x0050EA40)
// 0050EA70: the map object's delta (slot 32 first when flag bits 0x11 at +0x1DC are set, then the network state's
// write mode 1 around slot 50), flags_ bit 1 cleared (plain RET, EAX not a result).
__declspec(naked) inline void __fastcall MapObjectSendDeltaOriginal(void* /*object*/) EI_ORIGINAL(0x0050EA70)
// 0054B580 (rewritten as CUnitServer::IsFighting; Rebirth patches it, so callers go through the address): whether the
// controller's order counts as fighting (plain RET, AL).
__declspec(naked) inline bool __fastcall UnitServerIsFightingOriginal(void* /*controller*/) EI_ORIGINAL(0x0054B580)
__declspec(naked) inline void __fastcall ObjectSetupFigureOriginal(void* /*object*/, void*, const char* /*model*/)
    EI_ORIGINAL(0x005179E0)
__declspec(naked) inline void __fastcall FigureApplyPendingOriginal(void* /*figure state*/) EI_ORIGINAL(0x0050D450)
// A 3D object's figure (thiscall on the object): 00516DA0 builds it from model `model` and the part names `parts` (a
// CStringArray; RET 0xC, nonzero on success); 00518840 gives its animation state (+0x98) the model's animations (RET 8,
// returns 1); 00518860 restarts that state (plain RET). On the figure the object holds at +0x94: 0051C970 loads its
// textures (plain RET), 0051C250 swaps texture `from` for `to` (null: none; RET 8).
__declspec(naked) inline int __fastcall Object3DMakeFigureOriginal(void* /*object*/, void*, const char* /*model*/,
                                                                   void* /*parts*/, int /*flag*/) EI_ORIGINAL(0x00516DA0)
__declspec(naked) inline void __fastcall Object3DSetAnimationsOriginal(void* /*object*/, void*, const char* /*model*/,
                                                                       int /*flag*/) EI_ORIGINAL(0x00518840)
__declspec(naked) inline void __fastcall Object3DRestartAnimationOriginal(void* /*object*/) EI_ORIGINAL(0x00518860)
__declspec(naked) inline void __fastcall FigureLoadTexturesOriginal(void* /*figure*/) EI_ORIGINAL(0x0051C970)
__declspec(naked) inline void __fastcall FigureSwapTextureOriginal(void* /*figure*/, void*, const char* /*from*/,
                                                                   const char* /*to*/) EI_ORIGINAL(0x0051C250)
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
// 0051C9F0: the client half (CObject3DClientSpecific): with 0 every drawn part takes back the colour it kept, with 1 it
// keeps it and turns white (RET 4, no result).
class CObject3DClientSpecific;
__declspec(naked) inline void __fastcall ClientPartColoursOriginal(CObject3DClientSpecific* /*half*/, void*, int /*flash*/)
    EI_ORIGINAL(0x0051C9F0)
// 005380D0 (unit_client_speech.cpp, rewritten; through its address for the wrapper): the client controller plays a
// speech (RET 4, AL: false when the same one played within 5 s). 005392F0 (library: STLport list::erase of the speech
// queue): removes `at`, the next node to *out (RET 8).
struct ClientSpeech;
struct ClientSpeechNode;
__declspec(naked) inline bool __fastcall ClientSpeakOriginal(CUnitClientSpecific* /*controller*/, void*,
                                                             ClientSpeech* /*speech*/) EI_ORIGINAL(0x005380D0)
__declspec(naked) inline void __fastcall SpeechEraseOriginal(ClientSpeechNode** /*queue*/, void*,
                                                             ClientSpeechNode** /*out*/, ClientSpeechNode* /*at*/)
    EI_ORIGINAL(0x005392F0)
// 005121B0: the client companion's particle array (CMapObjectClientSpecific +0x28): SetSize(count, growBy) (RET 8, no
// result). 00533EA0: the client controller's destructor body (plain RET).
__declspec(naked) inline void __fastcall CompanionParticlesSetSizeOriginal(void* /*array*/, void*, int /*count*/,
                                                                           int /*growBy*/) EI_ORIGINAL(0x005121B0)
// 00512330 (library, MFC CArray::SetSize): the client companion's sound array (+0x3C; 0x14-byte records): SetSize(count,
// growBy) (RET 8, no result).
__declspec(naked) inline void __fastcall CompanionSoundsSetSizeOriginal(void* /*array*/, void*, int /*count*/,
                                                                       int /*growBy*/) EI_ORIGINAL(0x00512330)
// 00511830 / 005119B0 (map_object_client_effects.cpp, rewritten; through their addresses so the wrappers run): the map
// object's client companion makes a light of `colour` (rgba) and `radius` (RET 0xC; `flag` adds flags_ 0x40) / hides
// it (plain RET). No result.
__declspec(naked) inline void __fastcall CompanionCreateLightOriginal(void* /*companion*/, void*, const float* /*colour*/,
                                                                      float /*radius*/, int /*flag*/) EI_ORIGINAL(0x00511830)
__declspec(naked) inline void __fastcall CompanionHideLightOriginal(void* /*companion*/) EI_ORIGINAL(0x005119B0)
// 0056D740: the client world (007AF5B8) marks a party unit's sight: `range` around the point (x, y) (thiscall, RET 8; no
// result).
__declspec(naked) inline void __fastcall ClientWorldMarkSightOriginal(void* /*world*/, void*, const float* /*point*/,
                                                                      float /*range*/) EI_ORIGINAL(0x0056D740)
// 005271E0 (unit_vision.cpp, rewritten as UnitParams::VisionRange; through its address so the wrapper runs): the
// packed parameters' (CUnit + 0x330) sight range at a daylight of 0..1 (thiscall, RET 4; ST0).
__declspec(naked) inline double __fastcall UnitParamsVisionRangeOriginal(void* /*params*/, void*, float /*daylight*/)
    EI_ORIGINAL(0x005271E0)
__declspec(naked) inline void __fastcall ClientDestructBodyOriginal(CUnitClientSpecific* /*controller*/)
    EI_ORIGINAL(0x00533EA0)

// A-I7. 005310D0 (rewritten as CUnit::RemoveWeapon): clears weapon slot `slot` (RET 4; EAX not read by these callers).
// 00531110 (rewritten as CUnit::SendEquipment): sends the unit's equipment (plain RET). Through their address so the
// wrappers run.
__declspec(naked) inline void __fastcall UnitRemoveWeaponOriginal(CUnit* /*unit*/, void*, int /*slot*/) EI_ORIGINAL(0x005310D0)
__declspec(naked) inline void __fastcall UnitSendEquipmentOriginal(void* /*unit*/) EI_ORIGINAL(0x00531110)
// 0051FE00: the skill database (007AF268) finds the skill level named `name` by _mbsicmp (thiscall, RET 4), or null.
// 00526450 (rewritten as UnitStats::LearnSkill; through its address so the wrapper runs; RET 8).
__declspec(naked) inline void* __fastcall SkillFindOriginal(void* /*database*/, void*, const void* /*name*/)
    EI_ORIGINAL(0x0051FE00)
__declspec(naked) inline void __fastcall StatsLearnSkillOriginal(void* /*stats*/, void*, void* /*database*/, void* /*record*/)
    EI_ORIGINAL(0x00526450)

// A-U4 (object3d_figure_setup.cpp). The figure's animation state (CObject3D + 0x98, thiscall): 0050D7C0 rescales it to
// x, y, z given as float bits (RET 0xC), 0050D320 sets its model (sets +0x74, the figure manager's slot 15 and 005AC880;
// RET 4), 0050C990 releases every part it animates and empties the list (plain RET). None returns a result.
__declspec(naked) inline void __fastcall AnimationStateSetScaleOriginal(void* /*state*/, void*, std::uint32_t, std::uint32_t,
                                                                        std::uint32_t) EI_ORIGINAL(0x0050D7C0)
__declspec(naked) inline void __fastcall AnimationStateSetModelOriginal(void* /*state*/, void*, const char* /*model*/)
    EI_ORIGINAL(0x0050D320)
__declspec(naked) inline void __fastcall AnimationStateReleasePartsOriginal(void* /*state*/) EI_ORIGINAL(0x0050C990)
// 00517240 (rewritten as CObject3D::LoadPart; through its address so the wrapper runs): the 3D object's part
// `name` of the cached `model` (RET 0xC; returns the part, or null).
__declspec(naked) inline void* __fastcall Object3DLoadPartOriginal(void* /*object*/, void*, const char* /*name*/,
                                                                   void* /*model*/, std::uint32_t /*arg*/)
    EI_ORIGINAL(0x00517240)
// 0050CC20: the animation state's node (bone) named `name`, or null (thiscall, RET 4); its +0x74 is the node's position.
__declspec(naked) inline void* __fastcall AnimationStateFindNodeOriginal(void* /*state*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x0050CC20)
// 0050D920: the animation state appends the name (+0x1C) of each part it animates to the CStringArray `out` (thiscall,
// RET 4; no result).
__declspec(naked) inline void __fastcall AnimationStatePartNamesOriginal(void* /*state*/, void*, void* /*out*/)
    EI_ORIGINAL(0x0050D920)
// 0050D0E0: the animation state animates `part` too (thiscall, RET 4; no result).
__declspec(naked) inline void __fastcall AnimationStateAddPartOriginal(void* /*state*/, void*, void* /*part*/)
    EI_ORIGINAL(0x0050D0E0)
// 005AA5F0 (rewritten as FindFigureModel, figure_models.cpp): the figure manager's model named `name` (an STLport
// string; thiscall, RET 4). 005AC010 (rewritten as LoadFigureBounds): a figure part's bounding volume into `out`
// (thiscall on the manager, RET 0x1C; returns 1, 0 without data). Through their addresses so the wrappers run.
__declspec(naked) inline void* __fastcall FigureManagerFindModelOriginal(void* /*manager*/, void*, const void* /*name*/)
    EI_ORIGINAL(0x005AA5F0)
__declspec(naked) inline int __fastcall FigureManagerPartBoundsOriginal(void* /*manager*/, void*, void* /*out*/, void* /*model*/,
                                                                        const char* /*directory*/, const char* /*name*/,
                                                                        std::uint32_t, std::uint32_t, std::uint32_t)
    EI_ORIGINAL(0x005AC010)
// 005AC2B0 (thiscall on the figure manager, RET 0x20): a new figure part object (the manager's CreateObject, class 0x42,
// or 0x41 when `detached`, the object's flags_ & 0x1000, is 0) named `part` (+0x120) of the model `modelName` (+0x124);
// the cached model, the scale x, y, z (float bits) and `arg` are not read. Returns the part.
__declspec(naked) inline void* __fastcall FigureManagerLoadPartOriginal(void* /*manager*/, void*, const char* /*modelName*/,
                                                                        const char* /*part*/, void* /*model*/, std::uint32_t,
                                                                        std::uint32_t, std::uint32_t, std::uint32_t /*arg*/,
                                                                        std::uint32_t /*detached*/) EI_ORIGINAL(0x005AC2B0)
// A-U11 (unit_db.cpp, unit_db_destroy.cpp). The unit database's records: 0053EBC0 / 0053ED30 / 0053EE70 destroy a
// prototype / NPC / model record (rewritten in unit_db_destroy.cpp; thiscall, plain RET); 0053E2A0 / 0053E520 / 0053E730 /
// 0053E9B0 resize the vectors of prototypes / NPCs / models / level records (STLport, RET 4: count). No results.
__declspec(naked) inline void __fastcall UnitPrototypeDestroyOriginal(void* /*record*/) EI_ORIGINAL(0x0053EBC0)
__declspec(naked) inline void __fastcall UnitNpcDestroyOriginal(void* /*record*/) EI_ORIGINAL(0x0053ED30)
__declspec(naked) inline void __fastcall UnitModelDestroyOriginal(void* /*record*/) EI_ORIGINAL(0x0053EE70)
__declspec(naked) inline void __fastcall UnitPrototypesResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E2A0)
__declspec(naked) inline void __fastcall UnitNpcsResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E520)
__declspec(naked) inline void __fastcall UnitModelsResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E730)
__declspec(naked) inline void __fastcall UnitLevelsResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E9B0)
// A-U10 (unit_db.cpp). The skill database's STLport vectors: 00520D20 / 00520DE0 destroy a skill / skill level record
// (thiscall, plain RET), 005209D0 / 00520B20 resize the vector of skills / levels (RET 4: count). No results.
__declspec(naked) inline void __fastcall SkillRecordDestroyOriginal(void* /*record*/) EI_ORIGINAL(0x00520D20)
__declspec(naked) inline void __fastcall SkillLevelDestroyOriginal(void* /*record*/) EI_ORIGINAL(0x00520DE0)
__declspec(naked) inline void __fastcall SkillVectorResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x005209D0)
__declspec(naked) inline void __fastcall SkillLevelVectorResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x00520B20)
// A-U10 (map_object_animation_bones.cpp). 0050AAF0 (thiscall on an animation track, RET 4): loads its key frames from
// *data (an entry of a figure model part; advanced past it). No result.
__declspec(naked) inline void __fastcall AnimationTrackLoadOriginal(void* /*track*/, void*, void** /*data*/)
    EI_ORIGINAL(0x0050AAF0)
// A-U10 (map_object_animation_bones.cpp). Rewritten, called through their addresses so the wrappers run: 0050C230
// FigureBone::Place (RET 4), 0050ACD0 AnimationTrack::Evaluate (RET 4), 005ACB80 LoadFigureBones (thiscall on the figure manager, RET 8: model, bone;
// 1 / 0), 005AC880 LoadSkeletonLinks (thiscall on the figure manager, RET 8: state, model name; 1 or FatalError's).
__declspec(naked) inline void __fastcall FigureBonePlaceOriginal(void* /*bone*/, void*, const void* /*origin*/)
    EI_ORIGINAL(0x0050C230)
__declspec(naked) inline void __fastcall AnimationTrackEvaluateOriginal(void* /*track*/, void*, float /*time*/)
    EI_ORIGINAL(0x0050ACD0)
__declspec(naked) inline int __fastcall FigureManagerLoadBonesOriginal(void* /*manager*/, void*, void* /*model*/,
                                                                       void* /*bone*/) EI_ORIGINAL(0x005ACB80)
__declspec(naked) inline int __fastcall FigureManagerLinkSkeletonOriginal(void* /*manager*/, void*, void* /*state*/,
                                                                          const char* /*model*/) EI_ORIGINAL(0x005AC880)
// 00693180 (rewritten as ComposeTexture, texture_compose.cpp; through its address so the wrapper runs): the texture
// manager composes the skin `request` into the cached texture `name` (or `fallback`), its name into `out` (thiscall,
// RET 0x1C; 1 / 0).
__declspec(naked) inline int __fastcall TextureManagerComposeOriginal(void* /*manager*/, void*, void* /*out*/,
                                                                      const void* /*request*/, const void* /*name*/,
                                                                      const void* /*fallback*/, int /*mipCount*/,
                                                                      int /*fill*/, int /*format*/)
    EI_ORIGINAL(0x00693180)
// 006B7110 (thiscall on the sound system, RET 8): the names of its files matching `pattern` in every resource it
// searches, appended to the CStringArray `out` (emptied first). No result.
__declspec(naked) inline void __fastcall SoundSystemListFilesOriginal(void* /*sounds*/, void*, void* /*out*/,
                                                                      const char* /*pattern*/) EI_ORIGINAL(0x006B7110)

struct LeverRecord;   // lever.h
// 0052CA40 (thiscall on the lever table 0079C1A0, RET 4): the record of model `name` (its map's
// operator[], STLport library code: an unknown name adds a null record).
__declspec(naked) inline const LeverRecord* __fastcall LeverRecordOriginal(void* /*table*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x0052CA40)
struct LockKey;   // unit.h
// 00522A80 LockKey::Opens (unit_stats.cpp; Rebirth's FixLeversActivate patches it, so callers go
// through its address): thiscall on the key, RET 4, BOOL.
__declspec(naked) inline int __fastcall LockOpensOriginal(const LockKey* /*key*/, void*, const LockKey* /*lock*/)
    EI_ORIGINAL(0x00522A80)
class CTorchObjectSpec;   // server_specific_manager.h
struct DeltaField;        // net_delta.h
// 0052C390 (thiscall on the torch's spec, RET 4): reads the torch's network field 4 into the spec and
// applies it through the spec's slots 16, 17, 18, 13 and 15.
__declspec(naked) inline void __fastcall TorchSpecReadNetOriginal(CTorchObjectSpec* /*spec*/, void*, DeltaField* /*field*/)
    EI_ORIGINAL(0x0052C390)
// C3m. 0050E660 (rewritten as CMapObject::DestructBody): the map object's destructor body.
__declspec(naked) inline void __fastcall DestructMapObjectAt(void*) EI_ORIGINAL(0x0050E660)
// C3m. 0050E770 (rewritten as CMapObject::CreateMapObject): CMapObject's virtual slot 2 (Create): back to a blank map
// object. Returns 1.
__declspec(naked) inline int __fastcall MapObjectCreate(void*) EI_ORIGINAL(0x0050E770)
// C3m. 0050E840 (rewritten as CMapObject::DestroyMapObject): CMapObject's Destroy (slot 3), called directly.
__declspec(naked) inline void __fastcall MapObjectDestroy(void*) EI_ORIGINAL(0x0050E840)
// C3m. 00524060 / 00524110 (rewritten as UnitStats::LegsFactor / ArmsFactor; through their game addresses): the
// legs' and arms' factor for a body part index (ST0).
__declspec(naked) inline float __fastcall LegsFactorAt(UnitStats*, void*, int) EI_ORIGINAL(0x00524060)
__declspec(naked) inline float __fastcall ArmsFactorAt(UnitStats*, void*, int) EI_ORIGINAL(0x00524110)
// C3m. 00525800 (rewritten as UnitStats::HealSlots; through its address so the wrapper runs).
__declspec(naked) inline bool __fastcall HealSlotsAt(UnitStats*, void*, float, float) EI_ORIGINAL(0x00525800)
// C3m. 00525E90 (rewritten as UnitStats::AttributeWithItems). UnitStats: the value of skill `skill` for the unit, in ST0
// (declared double).
__declspec(naked) inline double __fastcall StatsSkillValue(UnitStats*, void*, int, CUnit*) EI_ORIGINAL(0x00525E90)

// C3m. The unit's server controller (thiscall, plain RET): 00555180 (rewritten as CUnitServer::Idle) and 00555200
// (rewritten as CUnitServer::StopQueued). Cast the controller's pointer; the views derive from this.
struct ControllerOriginals {
    void Idle();
    void StopQueued();
};
__declspec(naked) inline void ControllerOriginals::Idle() EI_ORIGINAL(0x00555180)
__declspec(naked) inline void ControllerOriginals::StopQueued() EI_ORIGINAL(0x00555200)
// C3n. 005144D0 / 00514930 (rewritten as UnitPrototypeTable::FindByName; thiscall, RET 4): the unit prototype record
// named `name` (inserting an empty entry for an unknown name; null then). Two copies of one function.
__declspec(naked) inline char* __fastcall UnitPrototypeFind(UnitPrototypeTable* /*table*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x005144D0)
__declspec(naked) inline char* __fastcall UnitPrototypeFindCopy(UnitPrototypeTable* /*table*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x00514930)
// C3n. 00523370 (rewritten as UnitVitals::Reset; plain RET): the vitals back to their defaults; returns the vitals.
__declspec(naked) inline void* __fastcall UnitVitalsReset(UnitVitals* /*vitals*/) EI_ORIGINAL(0x00523370)
// C3n. 00523E80 / 00523F50 (rewritten as UnitStats::HitPointCurve / ManaCurve; plain RET): the maximum hit points /
// mana the statistics give, in ST0 (declared double).
__declspec(naked) inline double __fastcall UnitStatsHitPointCurve(UnitStats* /*stats*/) EI_ORIGINAL(0x00523E80)
__declspec(naked) inline double __fastcall UnitStatsManaCurve(UnitStats* /*stats*/) EI_ORIGINAL(0x00523F50)
// C3n. 00524560 (rewritten as UnitStats::Recompute; thiscall, RET 8): the statistics' derived values for `unit`.
__declspec(naked) inline void __fastcall UnitStatsRecompute(UnitStats* /*stats*/, void*, UnitVitals* /*vitals*/,
                                                            CUnit* /*unit*/) EI_ORIGINAL(0x00524560)
// C3n. 00527EB0 (rewritten as UnitStats::SpellSchoolSkill; thiscall, RET 4): the skill of a spell school, in ST0.
__declspec(naked) inline double __fastcall UnitStatsSpellSchoolSkill(UnitStats* /*stats*/, void*, int /*school*/)
    EI_ORIGINAL(0x00527EB0)
// C3n. 0052F820 / 0052F8B0 (rewritten as CUnit::CanCastSpell / CanUseQuickItem; thiscall, RET 8): whether the unit can
// cast the spell / use the quick item of a slot at `point` (0 when it can).
__declspec(naked) inline int __fastcall UnitCanCastSpell(CUnit* /*unit*/, void*, int /*slot*/, const float* /*point*/)
    EI_ORIGINAL(0x0052F820)
__declspec(naked) inline int __fastcall UnitCanUseQuickItem(CUnit* /*unit*/, void*, int /*slot*/, const float* /*point*/)
    EI_ORIGINAL(0x0052F8B0)
// C3n. 00530120 (rewritten as CUnit::SetSide; thiscall, RET 4): the unit's side.
__declspec(naked) inline void __fastcall UnitSetSide(CUnit* /*unit*/, void*, int /*side*/) EI_ORIGINAL(0x00530120)
// C3n. 0052E940 (rewritten as CUnit::AddHostileMask; thiscall, RET 4): adds players to the unit's hostile mask.
__declspec(naked) inline void __fastcall UnitAddHostileMask(CUnit* /*unit*/, void*, std::uint32_t /*mask*/)
    EI_ORIGINAL(0x0052E940)
// C3n. 00523960 (rewritten as UnitStats::AddScaledExperience; thiscall, RET 4): the statistics gain experience (scaled).
__declspec(naked) inline void __fastcall UnitStatsAddExperience(UnitStats* /*stats*/, void*, float /*amount*/)
    EI_ORIGINAL(0x00523960)
// C3n. 005302F0 (rewritten as CUnit::ModeSpeed; plain RET): the speed (or height) of the unit's movement mode, in ST0.
__declspec(naked) inline double __fastcall UnitModeSpeed(CUnit* /*unit*/) EI_ORIGINAL(0x005302F0)
// C3n. 00530840 (rewritten as CUnit::UnequipArmour; thiscall, RET 4): the unit takes off the armour of a type.
__declspec(naked) inline void __fastcall UnitUnequipArmour(CUnit* /*unit*/, void*, int /*type*/) EI_ORIGINAL(0x00530840)
// C3n. 005308D0 (rewritten as CUnit::SetPrototype23CByName; thiscall, RET 4): sets the unit's second prototype by name.
__declspec(naked) inline void __fastcall UnitSetPrototype23CByName(CUnit* /*unit*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x005308D0)
// C3n. 00530970 (rewritten as CUnit::GetPrototypeModel; plain RET): the unit's model record (a UnitModelRecord).
__declspec(naked) inline char* __fastcall UnitPrototypeModel(CUnit* /*unit*/) EI_ORIGINAL(0x00530970)
// C3n. 00530A60 (rewritten as CUnit::IsBehind; thiscall, RET 4): whether the unit stands behind `target` (AL).
__declspec(naked) inline int __fastcall UnitIsBehind(CUnit* /*unit*/, void*, CEngineObject* /*target*/) EI_ORIGINAL(0x00530A60)
// C3n. 005313B0 (rewritten as CUnit::RemoveCarried; thiscall, RET 4): drops the carried item at an index.
__declspec(naked) inline void __fastcall UnitRemoveCarried(CUnit* /*unit*/, void*, int /*index*/) EI_ORIGINAL(0x005313B0)
// C3n. 005315B0 (rewritten as CUnit::ReleaseAllItems; plain RET): the unit lets go of all its items.
__declspec(naked) inline void __fastcall UnitReleaseAllItems(CUnit* /*unit*/) EI_ORIGINAL(0x005315B0)
// C3n. 005318B0 / 00531920 / 005319A0 (rewritten as CUnit::GetWeapon / GetQuickItem / GetSpell; thiscall, RET 4): the item
// or spell the unit holds in a slot (null when empty).
__declspec(naked) inline CEngineObject* __fastcall UnitWeapon(CUnit* /*unit*/, void*, std::uint32_t /*slot*/)
    EI_ORIGINAL(0x005318B0)
__declspec(naked) inline CEngineObject* __fastcall UnitQuickItem(CUnit* /*unit*/, void*, std::uint32_t /*slot*/)
    EI_ORIGINAL(0x00531920)
__declspec(naked) inline CEngineObject* __fastcall UnitSpell(CUnit* /*unit*/, void*, std::uint32_t /*slot*/)
    EI_ORIGINAL(0x005319A0)
// C3n. 00532D40 (rewritten as CUnit::Level; plain RET): the unit's level.
__declspec(naked) inline int __fastcall UnitLevel(CUnit* /*unit*/) EI_ORIGINAL(0x00532D40)
// C3n. 00546C40 (rewritten as CUnitServer::PlaySteps; thiscall, RET 4): the controller plays an animation-step list.
__declspec(naked) inline void __fastcall CtlPlaySteps(CUnitServer* /*controller*/, void*, const AnimationSteps* /*steps*/)
    EI_ORIGINAL(0x00546C40)
// C3n. 0054BCB0 / 0054C6B0 (rewritten as CUnitServer::FindSightTarget / CanPlanAction; thiscall, RET 8 / RET 4): the
// controller finds the target an order sights / whether it can plan the order (AL).
__declspec(naked) inline bool __fastcall CtlFindSightTarget(CUnitServer* /*controller*/, void*, const UnitAction* /*order*/,
                                                            SightTarget* /*out*/) EI_ORIGINAL(0x0054BCB0)
__declspec(naked) inline bool __fastcall CtlCanPlanAction(CUnitServer* /*controller*/, void*, const UnitAction* /*order*/)
    EI_ORIGINAL(0x0054C6B0)
// C3n. 00550100 (rewritten as CUnitServer::BuildOrderSteps; thiscall, RET 0x10): the animation steps of an order.
__declspec(naked) inline void __fastcall CtlBuildOrderSteps(CUnitServer* /*controller*/, void*, AnimationSteps* /*steps*/,
                                                            int* /*duration*/, int /*kind*/, std::uint32_t /*source*/)
    EI_ORIGINAL(0x00550100)
// C3n. 00550A80 (rewritten as CUnitServer::BuildDeathAnimation; thiscall, RET 0x10): the death (or fall) animation.
__declspec(naked) inline void __fastcall CtlBuildDeathAnimation(CUnitServer* /*controller*/, void*, AnimationSteps* /*steps*/,
                                                                std::uint32_t /*direction*/, int /*dying*/, int /*fall*/)
    EI_ORIGINAL(0x00550A80)
// C3n. 005516D0 (rewritten as CUnitServer::BuildTransition; thiscall, RET 0xC): the steps from one move mode to another (AL).
__declspec(naked) inline bool __fastcall CtlBuildTransition(CUnitServer* /*controller*/, void*, AnimationSteps* /*steps*/,
                                                            const MoveModeState* /*from*/, const MoveModeState* /*to*/)
    EI_ORIGINAL(0x005516D0)
// C3n. 00551A40 (rewritten as CUnitServer::AnimationDuration; thiscall, RET 4): the ticks of the unit's animation `index`.
__declspec(naked) inline int __fastcall CtlAnimationDuration(CUnitServer* /*controller*/, void*, std::uint32_t /*index*/)
    EI_ORIGINAL(0x00551A40)
// C3n. 00551AE0 (rewritten as CUnitServer::TryTame; thiscall, RET 8): the controller's unit tries to be tamed by `tamer`.
__declspec(naked) inline int __fastcall ControllerTame(CUnitServer* /*controller*/, void*, CUnit* /*tamer*/, float /*skill*/)
    EI_ORIGINAL(0x00551AE0)
// C3n. 00552780 (rewritten as CUnitServer::Shout; thiscall, RET 4): the unit shouts / remarks (a code).
__declspec(naked) inline void __fastcall ControllerShout(CUnitServer* /*controller*/, void*, std::uint32_t /*code*/)
    EI_ORIGINAL(0x00552780)
// C3n. 00552C60 (rewritten as CUnitServer::UseItemCharge; thiscall, RET 8): the item spends `cost` of its charge.
__declspec(naked) inline void __fastcall CtlUseItemCharge(CUnitServer* /*controller*/, void*, CItem* /*item*/, float /*cost*/)
    EI_ORIGINAL(0x00552C60)
// C3n. 00552E00 (rewritten as CUnitServer::PropagateHostility; plain RET): the unit's hostility spreads to its allies.
__declspec(naked) inline void __fastcall CtlPropagateHostility(CUnitServer* /*controller*/) EI_ORIGINAL(0x00552E00)

// C3o. Map object / unit statistics originals whose copies differed in their return type.
// 0050E2D0 (rewritten as CMapObject::Construct; thiscall, plain RET): the map object base constructor; returns this.
__declspec(naked) inline CMapObject* __fastcall MapObjectConstructOriginal(void* /*object*/) EI_ORIGINAL(0x0050E2D0)
// 0050F5B0 (rewritten as CMapObject::ClearEffects; plain RET): removes every effect; EAX is what its last call (slot 27)
// leaves. No game caller reads it.
__declspec(naked) inline std::uintptr_t __fastcall MapObjectClearEffectsOriginal(void* /*object*/) EI_ORIGINAL(0x0050F5B0)
// 00517700 (rewritten as CObject3D::SetFigureScale; RET 4): sets the scale (+0xC4) and rebuilds the figure when it
// changed; EAX as the original leaves it (00532AB0 CUnit::ResetUnitScale returns it).
__declspec(naked) inline std::uintptr_t __fastcall ObjectSetFigureScaleOriginal(void* /*object*/, void*, const float* /*scale*/)
    EI_ORIGINAL(0x00517700)
// 005233E0 / 00523550 (rewritten as UnitStats::Reset / UnitParams::Reset; plain RET): back to the defaults; EAX 0.
__declspec(naked) inline int __fastcall UnitStatsResetOriginal(void* /*stats*/) EI_ORIGINAL(0x005233E0)
__declspec(naked) inline int __fastcall UnitParamsResetOriginal(void* /*params*/) EI_ORIGINAL(0x00523550)
// 005136D0 (rewritten as UnitStatsBuilder::BuildFromPrototype; RET 0xC): the vitals and statistics of prototype
// `name`; 0 when `vitals` or `stats` is null.
__declspec(naked) inline int __fastcall UnitStatsBuildFromPrototype(UnitStatsBuilder* /*builder*/, void*, UnitVitals* /*vitals*/,
                                                                   UnitStats* /*stats*/, const char* /*name*/)
    EI_ORIGINAL(0x005136D0)
// 0065F5F0 (rewritten as PartyUnitRecord::Construct; plain RET): the record's texts, item lists and defaults (not its
// statistics); returns the record.
__declspec(naked) inline PartyUnitRecord* __fastcall PartyUnitRecordConstructOriginal(PartyUnitRecord* /*record*/)
    EI_ORIGINAL(0x0065F5F0)
// 00524CD0 (rewritten as UnitStats::ResetToBase; RET 4): the current values from the bases, with the load (a float).
__declspec(naked) inline void __fastcall UnitStatsResetToBaseOriginal(void* /*stats*/, void*, float /*load*/)
    EI_ORIGINAL(0x00524CD0)
// 00525AF0 (rewritten as UnitStats::RefillSlots; plain RET): every slot of kind 2 or more back to its maximum; EAX the
// end of the slot array.
__declspec(naked) inline void* __fastcall UnitStatsRefillSlotsOriginal(void* /*stats*/) EI_ORIGINAL(0x00525AF0)
// 00531180 / 00531820 (rewritten as CUnit::AddQuickItem / AddSpell; RET 4): adds a quick item / a spell; EAX the slot,
// 1 when already there, -1 when full (0 for a null item). 00531220 (rewritten as CUnit::AddCarried; RET 4): adds a
// carried (quest) item; EAX not a result.
__declspec(naked) inline int __fastcall UnitAddQuickItemOriginal(void* /*unit*/, void*, void* /*item*/) EI_ORIGINAL(0x00531180)
__declspec(naked) inline void __fastcall UnitAddCarriedOriginal(void* /*unit*/, void*, void* /*item*/) EI_ORIGINAL(0x00531220)
__declspec(naked) inline int __fastcall UnitAddSpellOriginal(void* /*unit*/, void*, void* /*spell*/) EI_ORIGINAL(0x00531820)
// 00546B40 (rewritten as CUnitServer::PopFinishedSteps; plain RET): drops the finished steps of the queue; AL whether
// any was dropped (00546FE0 tests it).
__declspec(naked) inline bool __fastcall CtlPopFinishedSteps(CUnitServer* /*controller*/) EI_ORIGINAL(0x00546B40)
// 00554830 (rewritten as CUnitServer::PlanMove; thiscall on the unit controller, RET 0x10): plans a move to `point`;
// `range` is a float.
__declspec(naked) inline void __fastcall ControllerPlanMoveOriginal(void* /*controller*/, void*, const float* /*point*/,
                                                                    int /*moving*/, int /*run*/, float /*range*/)
    EI_ORIGINAL(0x00554830)
// 005327D0 (rewritten as CUnit::ResistanceTo; RET 4): the unit's factor (+0x4B0..+0x4C4) for the kind in the hit's (or
// animation entry's) flags at +0x14, 1 by default; FLD of a float, so declared float. The step code reads it as the speed
// factor of an animation entry.
__declspec(naked) inline float __fastcall UnitResistanceToOriginal(void* /*unit*/, void*, const void* /*hit*/)
    EI_ORIGINAL(0x005327D0)
