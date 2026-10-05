#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CAIMap;
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
__declspec(naked) inline int __fastcall SearchCost(void*, void*, int, int) EI_ORIGINAL(0x005C3090)
__declspec(naked) inline void __fastcall SearchFinish(void*, void*) EI_ORIGINAL(0x005C30E0)
__declspec(naked) inline void __fastcall ListAppend(PathCellList*, void*, PathCellList*) EI_ORIGINAL(0x005C5DA0)
__declspec(naked) inline PathCellList* __fastcall ListReversed(PathCellList*, void*, PathCellList*)
    EI_ORIGINAL(0x005C5E60)
__declspec(naked) inline void __fastcall MotionBuild(void*, void*, void* /*order*/) EI_ORIGINAL(0x005C67D0)
__declspec(naked) inline bool __fastcall MotionSample(void* /*motion*/, void*, void* /*world*/, void* /*order*/, float /*time*/, bool* /*turning*/, float* /*position*/, float* /*direction*/, float* /*speed*/, int* /*cellX*/, int* /*cellY*/)
    EI_ORIGINAL(0x005C7430)
__declspec(naked) inline void __fastcall PathShiftTime(void*, void*, int) EI_ORIGINAL(0x005C7CB0)
