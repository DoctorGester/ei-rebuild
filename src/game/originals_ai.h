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

// 005CCBF0 (fastcall on the brain's calls heard, brain + 0x178, an AICallArray): drops the calls that carry a hostility
// mask (+0x10), destroying each (00558D00). Plain RET.
__declspec(naked) inline bool __fastcall PlanPath(CAIMap*, void*, CUnit*, int, int, int, int, AIPath*, int, int)
    EI_ORIGINAL(0x005B9C90)
__declspec(naked) inline bool __fastcall MapFindPath(void*, void*, CUnit*, const void* /*target*/, void* /*path*/, float /*range*/, float /*minDistance*/)
    EI_ORIGINAL(0x005BB350)
// 005C0DF0 / 005C1010 (rewritten as CSectorMap::Add / Remove): put an object into / take it out of the
// sector grid cell of its position; AL = whether it was done.
// The sector map of a world / AI map held as raw memory.
inline CSectorMap* AsSectorMap(void* map) { return static_cast<CSectorMap*>(map); }
inline CEngineObject* AsEngineObject(const void* object) { return static_cast<CEngineObject*>(const_cast<void*>(object)); }
__declspec(naked) inline bool __fastcall SectorMapAdd(CSectorMap*, void*, CEngineObject*) EI_ORIGINAL(0x005C0DF0)
// 005C11D0 CSectorMap::Move (patched by Rebirth's FixMoveObject, so called through its address; RET 8, AL).
__declspec(naked) inline bool __fastcall SectorMapMove(void* /*sectors*/, void*, CEngineObject*, const Vector3*)
    EI_ORIGINAL(0x005C11D0)
__declspec(naked) inline bool __fastcall SectorMapRemove(CSectorMap*, void*, CEngineObject*) EI_ORIGINAL(0x005C1010)

// 00522AE0 (thiscall on 8 damage words, RET 4): the sum over words 1..7 of what exceeds the
// matching protection word (negative differences count 0). Only the low 16 bits are meaningful.
__declspec(naked) inline std::uint32_t __fastcall ExcessDamageOriginal(const std::uint16_t* /*damage*/, void*,
                                                                      const std::uint16_t* /*protection*/) EI_ORIGINAL(0x00522AE0)

// An AI path's network state (AIPath, ai_map.h): 005C7DB0 writes it into the delta field (RET 4, EAX not a result).
__declspec(naked) inline void __fastcall PathWriteOriginal(void* /*path*/, void*, DeltaField* /*field*/) EI_ORIGINAL(0x005C7DB0)

// The brain's save-game helpers, called through their addresses so the verify wrappers see the calls
// (unit_ai_brain.cpp, unit_ai_save.cpp). 005D9010 / 005D91B0: looks load / save (fastcall: ECX the
// looks, EDX the stream; plain RET). 005CE920: an attack option's Save; 005D9B80: an attack option
// array's load (fastcall, plain RET). 005D9840: the alarm records' SetSize (RET 8). None
// returns a result.
__declspec(naked) inline void __fastcall AlarmRecordsSetSizeOriginal(void* /*array*/, void*, int, int) EI_ORIGINAL(0x005D9840)

// The Player mode's fighting and party code (rewritten in unit_ai_player.cpp), called through their
// addresses so the verify wrappers see the calls (all thiscall on the mode; RET 4 / 8 / 8 / 8).
struct AIBrain;
__declspec(naked) inline void __fastcall PlayerUpdateCombatOriginal(void* /*mode*/, void*, AIBrain*) EI_ORIGINAL(0x005D4170)
// 005B8BA0 (rewritten as CAIMap::CellMark; RET 0x14): the passability mark of cell (x, y) at `level` (scratch state
// carried from level to level); EAX the mark.
__declspec(naked) inline std::uint32_t __fastcall AIMapCellMarkOriginal(CAIMap*, void*, void* /*scratch*/, int /*level*/,
                                                                        int /*levelHeight*/, int /*x*/, int /*y*/)
    EI_ORIGINAL(0x005B8BA0)
