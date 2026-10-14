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
struct UnitStats;

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
__declspec(naked) inline void __fastcall UnitRemoveWeaponOriginal(void* /*unit*/, void*, int /*slot*/) EI_ORIGINAL(0x005310D0)
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
                                                                      void* /*request*/, void* /*name*/, void* /*fallback*/,
                                                                      int /*mipCount*/, int /*fill*/, int /*format*/)
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
