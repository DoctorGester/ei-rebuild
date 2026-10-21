#pragma once

// AddNewScriptCommands: addon's handlers (script_addon_*.cpp) and the helpers they share
// (script_addon.cpp). Each handler returns what addon's returns: true when the call is done, false
// to let the game's own code for the id run (only Random does that, for one argument).

#include "script_addon.h"

class CUnitServer;   // combat.h

namespace scriptaddon {

using Handler = bool (*)(Call* call);

// ---- Helpers ------------------------------------------------------------------------------------

// _ftol2_sse (10090EB0): truncation toward zero (CVTTSD2SI); NaN and out-of-range values give
// 0x80000000.
int Ftol(float value);
// FLD / FSTP of a value into *dst, as the handlers store their results.
void Store(float* dst, float value);
// _ftol2's x87 path (10090EE6, called directly by NearestUnit and UnitsNearPoint): truncation of
// the 64-bit value, its low 32 bits (here through the game's _ftol, the same FISTP).
int TruncateX87(float value);
// 10010CC5: dx = x1 - x0 and dy = y1 - y0 rounded to float, dx * dx + dy * dy rounded to float,
// then its square root (double) rounded to float.
float Distance(float x0, float y0, float x1, float y1);
// _stricmp in the "C" locale.
int Stricmp(const char* a, const char* b);
// 10010D04: the object's class (virtual slot 12) is a unit's (0x50).
bool IsUnit(CEngineObject* object);
// 10010E2D: a unit's data object (+0x240), null for a missing reference, object or non-unit.
CUnitServer* UnitData(const ScriptObjectRef* ref);
// 10010D12: addon reports a handler's problem as a "Scripts" error ("<function>: <message>") in
// its console log only when the mod setting EnableShowingScriptErrors is on, which no Rebirth mod
// sets, so nothing is shown. Returns true: the call counts as done.
bool Report(const char* function, const char* format, ...);
// 10010D7B: Report(function, "Invalid argument(s) passed to function").
bool InvalidArguments(const char* function);

// ---- Handlers -----------------------------------------------------------------------------------

// script_addon_math.cpp
bool Sin(Call* call);
bool Cos(Call* call);
bool Pow(Call* call);
bool DistancePointPoint(Call* call);
bool Add(Call* call);
bool Mul(Call* call);
bool And(Call* call);
bool Or(Call* call);
bool Max(Call* call);
bool Min(Call* call);
bool Abs(Call* call);
bool Round(Call* call);
bool Ceil(Call* call);
bool Floor(Call* call);
bool Random(Call* call);

// script_addon_strings.cpp
bool GetObjectStringID(Call* call);
bool FloatToStr(Call* call);
bool StrToFloat(Call* call);
bool StrCat(Call* call);
bool StrLen(Call* call);
bool RandStr(Call* call);
bool StrHas(Call* call);
bool StrMid(Call* call);
bool StrLower(Call* call);
bool StrUpper(Call* call);
bool Say(Call* call);
bool ConsoleString(Call* call);

// script_addon_units.cpp
bool GetPlayerOfUnit(Call* call);
bool GetUnitExp(Call* call);
bool GetArmor(Call* call);
bool NearestUnit(Call* call);
bool UnitsNearPoint(Call* call);
bool RandObj(Call* call);
bool PlayerExists(Call* call);
bool GetDexterity(Call* call);
bool GetIntelligence(Call* call);
bool GetStrength(Call* call);
bool GetSkill(Call* call);
bool GetActions(Call* call);
bool GetActionsBase(Call* call);
bool GetEncumbrance(Call* call);
bool GetMaxEncumbrance(Call* call);
bool GetPerk(Call* call);
bool GetPrototype(Call* call);
bool GetRace(Call* call);
bool GetObjectTypeID(Call* call);
bool GetObjectsOnMap(Call* call);
bool IsPlayer(Call* call);
bool IsDead(Call* call);
bool IsAlive(Call* call);

// script_addon_commands.cpp
bool GSSetPlayerVar(Call* call);
bool GSGetPlayerVar(Call* call);
bool GiveExp(Call* call);
bool GiveUnitExp(Call* call);
bool StartCameraSave(Call* call);
bool StopCameraSave(Call* call);
bool ResetTarget(Call* call);
bool SendCurrentParty(Call* call);
bool Loot(Call* call);
bool GameOver(Call* call);
bool GameOverMsg(Call* call);
bool IsFileExists(Call* call);
bool GSListPlayerVars(Call* call);
bool GiveUnitWeapon(Call* call);
bool GiveUnitArmor(Call* call);
bool ShowAllUnits(Call* call);
bool PauseGame(Call* call);
bool SetWeather(Call* call);
bool AddMobEx(Call* call);

} // namespace scriptaddon
