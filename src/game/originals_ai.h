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
