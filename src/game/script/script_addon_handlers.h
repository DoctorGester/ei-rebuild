#pragma once

// Rebirth's extra script commands and functions: the handlers (script_addon_*.cpp) and the helpers they share
// (script_addon.cpp). A handler returns true when the call is done, false to let the game's own code for the id run
// (only Random does that, for one argument).

#include "game/script/script_addon.h"
#include "lib/printf_args.h"

class UnitServer;

namespace scriptaddon {

using Handler = bool (*)(Call* call);


// Truncation toward zero; NaN and out-of-range values give INT_MIN.
int TruncateOrMin(float value);
// sqrt(dx * dx + dy * dy) with dx = x1 - x0 and dy = y1 - y0.
float Distance(float x0, float y0, float x1, float y1);
// Case-insensitive comparison of ASCII text.
int Stricmp(std::string_view a, std::string_view b);
// The object's class is a unit's.
bool IsUnit(EngineObject* object);
// Rebirth reports a handler's problem as a "Scripts" error ("<function>: <message>") in its console log only when the
// mod setting EnableShowingScriptErrors is on, which no mod sets, so nothing is shown. Returns true: the call counts
// as done.
EI_PRINTF_FORMAT(2, 3) bool Report(const char* function, const char* format, ...);
// With a std::string or std::string_view among the arguments: its text is passed for the %s.
template <class... Args>
    requires libprintf::HasClass<Args...>
bool Report(const char* function, const char* format, Args&&... args) {
    return Report(function, format, libprintf::Arg(libprintf::Owned(std::forward<Args>(args)))...);
}
// Report(function, "Invalid argument(s) passed to function").
bool InvalidArguments(const char* function);


// Ours (script_effects.cpp): SetParticleInput(id, input, value [, y, z]), FireParticleEvent(id, event).
bool SetParticleInput(Call* call);
bool FireParticleEvent(Call* call);

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

// script_addon_math.cpp
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
