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

// 005287A0 / 00528800: UnitStats Load / Save (RET 4: the stream; no result).
__declspec(naked) inline void __fastcall UnitStatsLoadOriginal(void* /*stats*/, void*, void* /*stream*/) EI_ORIGINAL(0x005287A0)
__declspec(naked) inline void __fastcall UnitStatsSaveOriginal(void* /*stats*/, void*, void* /*stream*/) EI_ORIGINAL(0x00528800)
__declspec(naked) inline int __fastcall PrototypeClass(void*, void*, const char*) EI_ORIGINAL(0x00513240)
__declspec(naked) inline int __fastcall SkillRecordIndex(const void*, void*, const void*) EI_ORIGINAL(0x0051FF00)
__declspec(naked) inline void __fastcall UnitParameterBlock(void*, void*, void*, void*, int) EI_ORIGINAL(0x005270E0)
__declspec(naked) inline void __fastcall UnitDestructBody(void*) EI_ORIGINAL(0x0052E310)
// 00530930 (rewritten as CUnit::GetPrototype; plain RET): the unit's prototype record (0x120 bytes, name first), or null.
__declspec(naked) inline void* __fastcall UnitPrototypeOf(const void* /*unit*/) EI_ORIGINAL(0x00530930)
__declspec(naked) inline void __stdcall ConstructPathCells(PathCell*, int) EI_ORIGINAL(0x00539980)
__declspec(naked) inline void __fastcall ActionClear(UnitAction*) EI_ORIGINAL(0x00545F40)
__declspec(naked) inline void __fastcall StepsCopyB(AnimationSteps*, void*, const AnimationSteps*)
    EI_ORIGINAL(0x005483C0)
__declspec(naked) inline void __fastcall StepsAppend(AnimationSteps*, void*, const AnimationSteps*)
    EI_ORIGINAL(0x00556950)
__declspec(naked) inline int __fastcall StepsAdd(AnimationSteps*, void*, const AnimationStep*) EI_ORIGINAL(0x00557CC0)
__declspec(naked) inline void __fastcall StepsSetAtGrow(AnimationSteps*, void*, int, const AnimationStep*)
    EI_ORIGINAL(0x00558950)

// 00530730 / 00531070: equip armour (RET 4, returns the item it replaced or null) / add weapon (RET 4, slot index,
// 1 when already there, -1 when full). 00554450: the unit controller resyncs with its unit (plain RET, no result).
__declspec(naked) inline void* __fastcall UnitEquipArmourOriginal(void* /*unit*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00530730)
__declspec(naked) inline int __fastcall UnitAddWeaponOriginal(void* /*unit*/, void*, void* /*item*/)
    EI_ORIGINAL(0x00531070)
// 00557E70: destructor of the animation-step array / controller list (ECX = object, plain RET, no result).
__declspec(naked) inline void __fastcall AnimationStepsDestroyOriginal(void* /*steps*/) EI_ORIGINAL(0x00557E70)

// C3h. 005300D0: unit.SetController(player) (RET 4, EAX not a result).
__declspec(naked) inline void __fastcall UnitSetControllerOriginal(void* /*unit*/, void*, void* /*player*/)
    EI_ORIGINAL(0x005300D0)

// 00544E70: AnimationSteps equal-length test against another (RET 4, AL).
__declspec(naked) inline bool __fastcall StepsQueueHoldsOriginal(void* /*steps*/, void*, const void* /*other*/)
    EI_ORIGINAL(0x00544E70)
// 00556870: AnimationSteps copy constructor (RET 4, returns this).
__declspec(naked) inline void* __fastcall StepsCopyConstructOriginal(void* /*steps*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x00556870)
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
// 0054B580 (rewritten as CUnitServer::IsFighting; Rebirth patches it, so callers go through the address): whether the
// controller's order counts as fighting (plain RET, AL).
__declspec(naked) inline bool __fastcall UnitServerIsFightingOriginal(void* /*controller*/) EI_ORIGINAL(0x0054B580)
// A 3D object's figure (thiscall on the object): 00516DA0 builds it from model `model` and the part names `parts` (a
// CStringArray; RET 0xC, nonzero on success); 00518840 gives its animation state (+0x98) the model's animations (RET 8,
// returns 1).
__declspec(naked) inline void __fastcall Object3DSetAnimationsOriginal(void* /*object*/, void*, const char* /*model*/,
                                                                       int /*flag*/) EI_ORIGINAL(0x00518840)
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
// A-U2. The figure's animation state (map_object_animation_state.cpp, rewritten): 0050CA50 plays an animation entry
// (RET 4), 0050CBA0 queues one (RET 4; EAX the list node). 00535480: the client controller starts the unit's idle
// animation (RET 4: play (byte) or queue; unit_client_animation.cpp). No results unless noted.
class CFigureAnimationState;
class CUnitClientSpecific;
struct FigureAnimation;
__declspec(naked) inline void __fastcall FigurePlayOriginal(CFigureAnimationState* /*state*/, void*,
                                                            const void* /*animation*/) EI_ORIGINAL(0x0050CA50)
__declspec(naked) inline void* __fastcall FigureQueueOriginal(CFigureAnimationState* /*state*/, void*,
                                                              const void* /*animation*/) EI_ORIGINAL(0x0050CBA0)
class CObject3DClientSpecific;
// 005392F0 (library: STLport list::erase of the speech
// queue): removes `at`, the next node to *out (RET 8).
struct ClientSpeech;
struct ClientSpeechNode;
__declspec(naked) inline void __fastcall SpeechEraseOriginal(ClientSpeechNode** /*queue*/, void*,
                                                             ClientSpeechNode** /*out*/, ClientSpeechNode* /*at*/)
    EI_ORIGINAL(0x005392F0)
// 005121B0: the client companion's particle array (CMapObjectClientSpecific +0x28): SetSize(count, growBy) (RET 8, no
// result).
__declspec(naked) inline void __fastcall CompanionParticlesSetSizeOriginal(void* /*array*/, void*, int /*count*/,
                                                                           int /*growBy*/) EI_ORIGINAL(0x005121B0)
// 00512330 (library, MFC CArray::SetSize): the client companion's sound array (+0x3C; 0x14-byte records): SetSize(count,
// growBy) (RET 8, no result).
__declspec(naked) inline void __fastcall CompanionSoundsSetSizeOriginal(void* /*array*/, void*, int /*count*/,
                                                                       int /*growBy*/) EI_ORIGINAL(0x00512330)
// 00511830 (map_object_client_effects.cpp, rewritten): the map object's client companion makes a light of `colour`
// (rgba) and `radius` (RET 0xC; `flag` adds flags_ 0x40). No result.
__declspec(naked) inline void __fastcall CompanionCreateLightOriginal(void* /*companion*/, void*, const float* /*colour*/,
                                                                      float /*radius*/, int /*flag*/) EI_ORIGINAL(0x00511830)

// 0051FE00: the skill database (007AF268) finds the skill level named `name` by _mbsicmp (thiscall, RET 4), or null.
__declspec(naked) inline void* __fastcall SkillFindOriginal(void* /*database*/, void*, const void* /*name*/)
    EI_ORIGINAL(0x0051FE00)

// 0050CC20: the animation state's node (bone) named `name`, or null (thiscall, RET 4); its +0x74 is the node's position.
__declspec(naked) inline void* __fastcall AnimationStateFindNodeOriginal(void* /*state*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x0050CC20)
// 0050D920: the animation state appends the name (+0x1C) of each part it animates to the CStringArray `out` (thiscall,
// RET 4; no result).
__declspec(naked) inline void __fastcall AnimationStatePartNamesOriginal(void* /*state*/, void*, void* /*out*/)
    EI_ORIGINAL(0x0050D920)
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
__declspec(naked) inline void __fastcall UnitModelDestroyOriginal(void* /*record*/) EI_ORIGINAL(0x0053EE70)
__declspec(naked) inline void __fastcall UnitPrototypesResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E2A0)
__declspec(naked) inline void __fastcall UnitNpcsResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E520)
__declspec(naked) inline void __fastcall UnitModelsResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E730)
__declspec(naked) inline void __fastcall UnitLevelsResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x0053E9B0)
// A-U10 (unit_db.cpp). The skill database's STLport vectors: 005209D0 / 00520B20 resize the vector of skills / levels
// (RET 4: count). No results.
__declspec(naked) inline void __fastcall SkillVectorResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x005209D0)
__declspec(naked) inline void __fastcall SkillLevelVectorResizeOriginal(void* /*vector*/, void*, std::uint32_t /*count*/)
    EI_ORIGINAL(0x00520B20)
// A-U10 (map_object_animation_bones.cpp). 0050AAF0 (thiscall on an animation track, RET 4): loads its key frames from
// *data (an entry of a figure model part; advanced past it). No result.
__declspec(naked) inline void __fastcall AnimationTrackLoadOriginal(void* /*track*/, void*, void** /*data*/)
    EI_ORIGINAL(0x0050AAF0)
// A-U10 (map_object_animation_bones.cpp). Rewritten: 0050C230 FigureBone::Place (RET 4),
// 005ACB80 LoadFigureBones (thiscall on the figure manager, RET 8: model, bone;
// 1 / 0), 005AC880 LoadSkeletonLinks (thiscall on the figure manager, RET 8: state, model name; 1 or FatalError's).
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
// C3m. 0050E660 (rewritten as CMapObject::DestructBody): the map object's destructor body.
__declspec(naked) inline void __fastcall DestructMapObjectAt(void*) EI_ORIGINAL(0x0050E660)

// C3n. 0052F820 / 0052F8B0 (rewritten as CUnit::CanCastSpell / CanUseQuickItem; thiscall, RET 8): whether the unit can
// cast the spell / use the quick item of a slot at `point` (0 when it can).
__declspec(naked) inline int __fastcall UnitCanCastSpell(CUnit* /*unit*/, void*, int /*slot*/, const float* /*point*/)
    EI_ORIGINAL(0x0052F820)
__declspec(naked) inline int __fastcall UnitCanUseQuickItem(CUnit* /*unit*/, void*, int /*slot*/, const float* /*point*/)
    EI_ORIGINAL(0x0052F8B0)

// C3o. Map object / unit statistics originals whose copies differed in their return type.
// 0050E2D0 (rewritten as CMapObject::Construct; thiscall, plain RET): the map object base constructor; returns this.
__declspec(naked) inline CMapObject* __fastcall MapObjectConstructOriginal(void* /*object*/) EI_ORIGINAL(0x0050E2D0)
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
// 00531180 / 00531820 (rewritten as CUnit::AddQuickItem / AddSpell; RET 4): adds a quick item / a spell; EAX the slot,
// 1 when already there, -1 when full (0 for a null item). 00531220 (rewritten as CUnit::AddCarried; RET 4): adds a
// carried (quest) item; EAX not a result.
__declspec(naked) inline int __fastcall UnitAddQuickItemOriginal(void* /*unit*/, void*, void* /*item*/) EI_ORIGINAL(0x00531180)
__declspec(naked) inline void __fastcall UnitAddCarriedOriginal(void* /*unit*/, void*, void* /*item*/) EI_ORIGINAL(0x00531220)
__declspec(naked) inline int __fastcall UnitAddSpellOriginal(void* /*unit*/, void*, void* /*spell*/) EI_ORIGINAL(0x00531820)
// 005327D0 (rewritten as CUnit::ResistanceTo; RET 4): the unit's factor (+0x4B0..+0x4C4) for the kind in the hit's (or
// animation entry's) flags at +0x14, 1 by default; FLD of a float, so declared float. The step code reads it as the speed
// factor of an animation entry.
__declspec(naked) inline float __fastcall UnitResistanceToOriginal(void* /*unit*/, void*, const void* /*hit*/)
    EI_ORIGINAL(0x005327D0)
