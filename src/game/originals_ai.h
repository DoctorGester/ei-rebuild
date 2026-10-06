#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CAIMap;
class CEngineObject;
class CSectorMap;
class CUnit;
struct AIPath;
struct PathCellList;
struct Vector3;

__declspec(naked) inline void __fastcall AnimateOriginal(char*, void*, std::uint32_t) EI_ORIGINAL(0x005A7160)
__declspec(naked) inline void __fastcall FinishDraw(char*, void*, void* /*renderer*/) EI_ORIGINAL(0x005A7290)
__declspec(naked) inline void __fastcall AlarmReset(void*) EI_ORIGINAL(0x005AEA40)
__declspec(naked) inline void __fastcall PathToMotionOrder(void*, void*, void* /*out*/) EI_ORIGINAL(0x005B22C0)
__declspec(naked) inline void __fastcall PathClear(void*) EI_ORIGINAL(0x005B23A0)
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
__declspec(naked) inline int __fastcall SearchCost(void*, void*, int, int) EI_ORIGINAL(0x005C3090)
__declspec(naked) inline void __fastcall SearchFinish(void*, void*) EI_ORIGINAL(0x005C30E0)
__declspec(naked) inline void __fastcall ListAppend(PathCellList*, void*, PathCellList*) EI_ORIGINAL(0x005C5DA0)
__declspec(naked) inline PathCellList* __fastcall ListReversed(PathCellList*, void*, PathCellList*)
    EI_ORIGINAL(0x005C5E60)
__declspec(naked) inline void __fastcall MotionBuild(void*, void*, void* /*order*/) EI_ORIGINAL(0x005C67D0)
__declspec(naked) inline bool __fastcall MotionSample(void* /*motion*/, void*, void* /*world*/, void* /*order*/, float /*time*/, bool* /*turning*/, float* /*position*/, float* /*direction*/, float* /*speed*/, int* /*cellX*/, int* /*cellY*/)
    EI_ORIGINAL(0x005C7430)
__declspec(naked) inline void __fastcall PathShiftTime(void*, void*, int) EI_ORIGINAL(0x005C7CB0)

// C3i. 005C35F0: CLocalSearch reset (plain RET, EAX not a result; the old `(void*, void*)` forms passed a dummy EDX).
__declspec(naked) inline void __fastcall LocalSearchResetOriginal(void* /*search*/) EI_ORIGINAL(0x005C35F0)

// C3j. 005D3760: player mode constructor (plain RET, returns this).
__declspec(naked) inline void* __fastcall ModePlayerConstructOriginal(void* /*mode*/) EI_ORIGINAL(0x005D3760)
// 005D5F60 / 005D4DF0 / 005D6140: brain AddMode(mode) (RET 4) / ClearModes() (plain RET) / RemoveMode(index) (RET 4); no result.
__declspec(naked) inline void __fastcall BrainAddModeOriginal(void* /*brain*/, void*, void* /*mode*/) EI_ORIGINAL(0x005D5F60)
__declspec(naked) inline void __fastcall BrainClearModesOriginal(void* /*brain*/) EI_ORIGINAL(0x005D4DF0)
__declspec(naked) inline void __fastcall BrainRemoveModeOriginal(void* /*brain*/, void*, int /*index*/) EI_ORIGINAL(0x005D6140)
// 005D12B0: patrol mode adds a point (RET 4, no result).
__declspec(naked) inline void __fastcall PatrolAddPointOriginal(void* /*mode*/, void*, const void* /*point*/) EI_ORIGINAL(0x005D12B0)
// 005AEA60: AI voice raises its level to at least `level` (RET 4, no result).
__declspec(naked) inline void __fastcall VoiceSayOriginal(void* /*voice*/, void*, int /*level*/) EI_ORIGINAL(0x005AEA60)
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
// 005CA050 (rewritten as CopyPoint2): called through its address where the UpdateMemory check counts
// the inline notices (verify_unit_ai_brain.cpp).
__declspec(naked) inline float* __fastcall CopyPoint2Original(float* /*out*/, const float* /*point*/) EI_ORIGINAL(0x005CA050)
// 00552CF0 (rewritten as UnitController::AddHostileMask), through its address so its wrapper sees the calls.
__declspec(naked) inline std::uintptr_t __fastcall ControllerAddHostileMaskOriginal(void* /*controller*/, void*, std::uint32_t /*mask*/)
    EI_ORIGINAL(0x00552CF0)
// 005CC9D0 (rewritten as AICallArray::Update), through its address so its wrapper sees UpdateMemory's calls (RET 8).
__declspec(naked) inline void __fastcall AiCallsUpdateOriginal(void* /*calls*/, void*, CUnit* /*unit*/, void* /*memory*/)
    EI_ORIGINAL(0x005CC9D0)
