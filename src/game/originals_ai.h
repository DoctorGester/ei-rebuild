#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CAIMap;
struct CRegionMap;
class CEngineObject;
class CSectorMap;
class CUnitLayer;
class CUnit;
struct AIAttention;
struct AIPath;
struct DeltaField;
struct Vector3;

__declspec(naked) inline void __fastcall AnimateOriginal(char*, void*, std::uint32_t) EI_ORIGINAL(0x005A7160)
__declspec(naked) inline void __fastcall FinishDraw(char*, void*, void* /*renderer*/) EI_ORIGINAL(0x005A7290)
// 005B0370 (RET 0x14): CRegionMap::NearbyBlocks through its original address (the list is a
// BlockCostList, ai_map.h).
__declspec(naked) inline void __fastcall NearbyBlocksOriginal(CRegionMap*, void*, int /*sizeClass*/, bool /*flag*/,
                                                            int /*x*/, int /*y*/, void* /*out*/) EI_ORIGINAL(0x005B0370)
__declspec(naked) inline void __fastcall PathToMotionOrder(void*, void*, void* /*out*/) EI_ORIGINAL(0x005B22C0)
__declspec(naked) inline void __fastcall PathClear(void*) EI_ORIGINAL(0x005B23A0)
// 005CCBF0 (fastcall on the brain's calls heard, brain + 0x178, an AICallArray): drops the calls that carry a hostility
// mask (+0x10), destroying each (00558D00). Plain RET.
__declspec(naked) inline void __fastcall CallsDropClaims(void* /*calls*/) EI_ORIGINAL(0x005CCBF0)
// 005CBBC0 (thiscall on the brain's memory, brain + 0xD0; RET 0xC): the memory's calls (+0xA8) hear a call of `kind`
// carrying the hostility `mask` and `text` (its point left as the stack had it).
__declspec(naked) inline void __fastcall MemoryAddCall(void* /*memory*/, void*, std::uint32_t /*mask*/, int /*kind*/,
                                                     const char* /*text*/) EI_ORIGINAL(0x005CBBC0)
__declspec(naked) inline bool __fastcall PlanPath(CAIMap*, void*, CUnit*, int, int, int, int, AIPath*, int, int)
    EI_ORIGINAL(0x005B9C90)
__declspec(naked) inline bool __fastcall MapFindPath(void*, void*, CUnit*, const void* /*target*/, void* /*path*/, float /*range*/, float /*minDistance*/)
    EI_ORIGINAL(0x005BB350)
// 005C0DF0 / 005C1010 (rewritten as CSectorMap::Add / Remove): put an object into / take it out of the
// sector grid cell of its position; AL = whether it was done.
// The sector map of a world / AI map held as raw memory.
inline CSectorMap* AsSectorMap(void* map) { return static_cast<CSectorMap*>(map); }
inline CEngineObject* AsEngineObject(const void* object) { return static_cast<CEngineObject*>(const_cast<void*>(object)); }
// 005C0BD0 (rewritten as CSectorMap::FindUnitsInRadius; RET 0xC): appends the units within the radius of the
// point to the list (EAX on return is not a result).
__declspec(naked) inline void __fastcall SectorMapFindUnitsInRadius(void* /*sectors*/, void*, const void* /*center*/,
                                                                    void* /*list*/, float /*radius*/)
    EI_ORIGINAL(0x005C0BD0)
__declspec(naked) inline bool __fastcall SectorMapAdd(CSectorMap*, void*, CEngineObject*) EI_ORIGINAL(0x005C0DF0)
// 005C11D0 CSectorMap::Move (patched by Rebirth's FixMoveObject, so called through its address; RET 8, AL).
__declspec(naked) inline bool __fastcall SectorMapMove(void* /*sectors*/, void*, CEngineObject*, const Vector3*)
    EI_ORIGINAL(0x005C11D0)
__declspec(naked) inline bool __fastcall SectorMapRemove(CSectorMap*, void*, CEngineObject*) EI_ORIGINAL(0x005C1010)
__declspec(naked) inline void __fastcall ListAppend(void* /*cells*/, void*, const void* /*other*/) EI_ORIGINAL(0x005C5DA0)
__declspec(naked) inline void* __fastcall ListReversed(const void* /*cells*/, void*, void* /*out*/)
    EI_ORIGINAL(0x005C5E60)
__declspec(naked) inline void __fastcall MotionBuild(void*, void*, void* /*order*/) EI_ORIGINAL(0x005C67D0)
__declspec(naked) inline bool __fastcall MotionSample(void* /*motion*/, void*, void* /*world*/, void* /*order*/, float /*time*/, bool* /*turning*/, Vector3* /*position*/, Vector3* /*direction*/, float* /*speed*/, int* /*cellX*/, int* /*cellY*/)
    EI_ORIGINAL(0x005C7430)

// C3j. 005D3760: player mode constructor (plain RET, returns this).
__declspec(naked) inline void* __fastcall ModePlayerConstructOriginal(void* /*mode*/) EI_ORIGINAL(0x005D3760)
// 005D5F60: brain AddMode(mode) (RET 4; no result).
__declspec(naked) inline void __fastcall BrainAddModeOriginal(void* /*brain*/, void*, void* /*mode*/) EI_ORIGINAL(0x005D5F60)
// 00522AE0 (thiscall on 8 damage words, RET 4): the sum over words 1..7 of what exceeds the
// matching protection word (negative differences count 0). Only the low 16 bits are meaningful.
__declspec(naked) inline std::uint32_t __fastcall ExcessDamageOriginal(const std::uint16_t* /*damage*/, void*,
                                                                      const std::uint16_t* /*protection*/) EI_ORIGINAL(0x00522AE0)
// CAIMap lookups at a world position, through their addresses (rewritten in ai_map.cpp / ai_movement.cpp /
// ai_water_patch.cpp; `map` is the CAIMap). 005BA790 IsPassableForClass (RET 8, AL), 005BA9D0 FindFreeCellNear
// (RET 0xC, AL), 005B7EF0 StampWaterPatch (RET 8).
__declspec(naked) inline bool __fastcall IsPassableForClassOriginal(void* /*map*/, void*, int /*sizeClass*/,
                                                                    const Vector3* /*position*/) EI_ORIGINAL(0x005BA790)
__declspec(naked) inline bool __fastcall FindFreeCellNearOriginal(void* /*map*/, void*, CEngineObject* /*unit*/,
                                                                  const Vector3* /*from*/, Vector3* /*target*/)
    EI_ORIGINAL(0x005BA9D0)
__declspec(naked) inline void __fastcall StampWaterPatchOriginal(void* /*map*/, void*, const Vector3* /*position*/,
                                                                 int /*add*/) EI_ORIGINAL(0x005B7EF0)
// 005BC3E0 (CAIMap::LineOfSight, ai_map.cpp) through its address, its ST0 taken unrounded (RET 8).
__declspec(naked) inline double __fastcall LineOfSightOriginal(CAIMap*, void*, const Vector3* /*from*/, const Vector3* /*to*/)
    EI_ORIGINAL(0x005BC3E0)
// 004C9970: CArray<PatrolPoint>::SetSize(size, growBy) (RET 8; elements constructed / destroyed).
__declspec(naked) inline void __fastcall PatrolPointArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x004C9970)
// 005D12B0: patrol mode adds a point (RET 4, no result).
__declspec(naked) inline void __fastcall PatrolAddPointOriginal(void* /*mode*/, void*, const void* /*point*/) EI_ORIGINAL(0x005D12B0)
// 005B14C0: constructs the second part of the search state (plain RET, no result).
__declspec(naked) inline void __fastcall SearchPartConstructOriginal(void* /*search*/) EI_ORIGINAL(0x005B14C0)
// 005C2420: CLocalSearch begin(unit, a, b, c, d) (RET 0x14, AL).
__declspec(naked) inline bool __fastcall LocalSearchBeginOriginal(void* /*search*/, void*, void* /*unit*/, int, int, int, int)
    EI_ORIGINAL(0x005C2420)
// 005C6000: fixes up a path cell list chain (plain RET, no result).
__declspec(naked) inline void __fastcall PathCellListFixUpOriginal(void* /*list*/) EI_ORIGINAL(0x005C6000)
// 005C98C0: AI events noise(source, loudness, kind, at) (RET 0x10; a null `at` means the source's position; no result).
__declspec(naked) inline void __fastcall AiEventsNoiseOriginal(void* /*events*/, void*, void* /*source*/, float /*loudness*/,
                                                               int /*kind*/, const void* /*at*/) EI_ORIGINAL(0x005C98C0)
// 005CAA00: AI events update a controller's view into its perception memory (RET 8, no result).
__declspec(naked) inline void __fastcall AiEventsUpdateMemoryOriginal(void* /*events*/, void*, void* /*controller*/,
                                                                      void* /*memory*/) EI_ORIGINAL(0x005CAA00)
// 005CB7F0: AI memory IsEnemy(unit, object) (RET 8, EAX 0/1).
__declspec(naked) inline int __fastcall AiMemoryIsEnemyOriginal(void* /*memory*/, void*, void* /*unit*/, void* /*object*/)
    EI_ORIGINAL(0x005CB7F0)
// 005CBBE0: AI memory Notice(point, level, kind) (RET 0xC, no result).
__declspec(naked) inline void __fastcall AiMemoryNoticeOriginal(void* /*memory*/, void*, const void* /*point*/, int /*level*/,
                                                                int /*kind*/) EI_ORIGINAL(0x005CBBE0)

// C3k. 004CB770: destructor of the AI look array (vtable 0073D460; frees the buffer; plain RET, EAX not a result).
__declspec(naked) inline void __fastcall AiLookArrayDestructOriginal(void* /*array*/) EI_ORIGINAL(0x004CB770)
// 00552CF0 (rewritten as UnitController::AddHostileMask), through its address so its wrapper sees the calls.
__declspec(naked) inline std::uintptr_t __fastcall ControllerAddHostileMaskOriginal(void* /*controller*/, void*, std::uint32_t /*mask*/)
    EI_ORIGINAL(0x00552CF0)
// 005CC9D0 (rewritten as AICallArray::Update), through its address so its wrapper sees UpdateMemory's calls (RET 8).
__declspec(naked) inline void __fastcall AiCallsUpdateOriginal(void* /*calls*/, void*, CUnit* /*unit*/, void* /*memory*/)
    EI_ORIGINAL(0x005CC9D0)
// An AI path's network state (AIPath, ai_map.h): 005C7DB0 writes it into the delta field (RET 4, EAX not a result).
__declspec(naked) inline void __fastcall PathWriteOriginal(void* /*path*/, void*, DeltaField* /*field*/) EI_ORIGINAL(0x005C7DB0)

// The brain's save-game helpers, called through their addresses so the verify wrappers see the calls
// (unit_ai_brain.cpp, unit_ai_save.cpp). 005D9010 / 005D91B0: looks load / save (fastcall: ECX the
// looks, EDX the stream; plain RET). 005CC1E0 / 005CC350: the perception memory's load / save
// (thiscall on brain + 0xD0, RET 4). 005CE920: an attack option's Save; 005D9B80: an attack option
// array's load (fastcall, plain RET). 005D9840: the alarm records' SetSize (RET 8). None
// returns a result.
__declspec(naked) inline void __fastcall LooksLoadOriginal(void* /*looks*/, void* /*stream*/) EI_ORIGINAL(0x005D9010)
__declspec(naked) inline void __fastcall LooksSaveOriginal(const void* /*looks*/, void* /*stream*/) EI_ORIGINAL(0x005D91B0)
__declspec(naked) inline void __fastcall MemoryLoadOriginal(void* /*memory*/, void*, void* /*stream*/) EI_ORIGINAL(0x005CC1E0)
__declspec(naked) inline void __fastcall MemorySaveOriginal(void* /*memory*/, void*, void* /*stream*/) EI_ORIGINAL(0x005CC350)
__declspec(naked) inline void __fastcall AttackOptionSaveOriginal(const void* /*option*/, void*, void* /*stream*/)
    EI_ORIGINAL(0x005CE920)
__declspec(naked) inline void __fastcall AttackOptionsLoadOriginal(void* /*options*/, void* /*stream*/) EI_ORIGINAL(0x005D9B80)
__declspec(naked) inline void __fastcall AlarmRecordWriteOriginal(const void* /*record*/, void*, void* /*stream*/)
    EI_ORIGINAL(0x0045D760)
__declspec(naked) inline void __fastcall AlarmRecordsSetSizeOriginal(void* /*array*/, void*, int, int) EI_ORIGINAL(0x005D9840)

// The Player mode's fighting and party code (rewritten in unit_ai_player.cpp), called through their
// addresses so the verify wrappers see the calls (all thiscall on the mode; RET 4 / 8 / 8 / 8).
struct AIBrain;
__declspec(naked) inline void __fastcall PlayerUpdateCombatOriginal(void* /*mode*/, void*, AIBrain*) EI_ORIGINAL(0x005D4170)
__declspec(naked) inline void __fastcall PlayerCollectPartyOriginal(void* /*mode*/, void*, AIBrain*, void* /*list*/)
    EI_ORIGINAL(0x005D4600)
__declspec(naked) inline void __fastcall PlayerAddPartyTargetsOriginal(void* /*mode*/, void*, AIBrain*, void* /*list*/)
    EI_ORIGINAL(0x005D3F50)
// 005CD3A0 (thiscall on a grid's cells, VisibilityGrid + 8; RET 8): the cell block reallocated to
// width x height (not cleared) unless it already has that size.
__declspec(naked) inline void __fastcall GridCellsResizeOriginal(void* /*cells*/, void*, int /*width*/, int /*height*/)
    EI_ORIGINAL(0x005CD3A0)
// C3n. The AI map (CAIMap, the global 007B00D0 = world + 0x170; thiscall): 005BA830 CanLeaveCell (RET 4: the unit's
// cell; AL). 005BA9D0 FindFreeCellNearOriginal is above.
__declspec(naked) inline bool __fastcall AIMapCanLeaveCellOriginal(void* /*map*/, void*, CUnit* /*unit*/) EI_ORIGINAL(0x005BA830)
// C3n. The AI brain (AIBrain, the unit controller + 0x980; thiscall): 005D7790 StopAction (plain RET).
__declspec(naked) inline void __fastcall BrainStopActionOriginal(void* /*brain*/) EI_ORIGINAL(0x005D7790)
// 005B36B0 (rewritten as CUnitLayer::Add; RET 0x10): stamps `owner`'s shape into the unit occupancy layer at cell
// (x, y). `owner` is only a key: a unit, or the water patch's packed cell key
// (ai_water_patch.cpp). No result.
__declspec(naked) inline void __fastcall UnitLayerAddOriginal(CUnitLayer* /*layer*/, void*, CUnit* /*owner*/,
                                                              const void* /*shape*/, int /*x*/, int /*y*/)
    EI_ORIGINAL(0x005B36B0)
// 005B8BA0 (rewritten as CAIMap::CellMark; RET 0x14): the passability mark of cell (x, y) at `level` (scratch state
// carried from level to level); EAX the mark.
__declspec(naked) inline std::uint32_t __fastcall AIMapCellMarkOriginal(CAIMap*, void*, void* /*scratch*/, int /*level*/,
                                                                        int /*levelHeight*/, int /*x*/, int /*y*/)
    EI_ORIGINAL(0x005B8BA0)
