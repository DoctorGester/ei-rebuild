#pragma once

// AddNewScriptCommands: addon's handlers (script_addon_*.cpp) and the helpers they share
// (script_addon.cpp). Each handler returns what addon's returns: true when the call is done, false
// to let the game's own code for the id run (only Random does that, for one argument).

#include "script_addon.h"

namespace scriptaddon {

using Handler = bool (*)(Call* call);

// ---- Helpers ------------------------------------------------------------------------------------

// _ftol2_sse (10090EB0): truncation toward zero (CVTTSD2SI); NaN and out-of-range values give
// 0x80000000.
int Ftol(float value);
// FNSTSW after FLD a, FCOMP b, as AH: 0x01 C0 (a < b), 0x04 C2, 0x40 C3 (a == b); all three when
// unordered.
int Status(float a, float b);
// FLD / FSTP of a value into *dst, as the handlers store their results.
void Store(float* dst, float value);
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

} // namespace scriptaddon
