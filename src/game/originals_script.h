#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include "lib/mfc.h"
#include "lib/stlp.h"

class CEngineObject;
class CScript;
class GameBuffer;
struct ScriptDefinition;
struct ScriptGroup;
struct ScriptObjectRef;
struct ScriptRun;
struct ScriptVars;

__declspec(naked) inline int __fastcall ExecuteOrig(ScriptRun*, void*, CScript*) EI_ORIGINAL(0x004B6760)

// 004D23A0 (RET 8, EAX 0/1): adds / loads a script line (CScript::Load, rewritten; called through its address).
__declspec(naked) inline int __fastcall ScriptLoadLine(void* /*script*/, void*, const mfc::CString* /*line*/,
                                                       int /*flag*/) EI_ORIGINAL(0x004D23A0)

// C3h. 004C2290: a script group's destructor (plain RET).
__declspec(naked) inline void __fastcall ScriptGroupDestructOriginal(void* /*group*/) EI_ORIGINAL(0x004C2290)

// The link figures' STLport map<string, ...> members (link_figure.h; library instantiations):
// 004F4B90 the part headers' operator[] (RET 4, EAX = the value, a zeroed 0x50-byte header when new),
// 004F4CE0 the part ranges' operator[] (RET 4, EAX = the value).
struct LinkPartHeader;
struct LinkPartRanges;
// C3m. CArray::SetSize (__thiscall (size, growBy)) of the script arrays: 004C3980 group values, 004CD770 definitions
// (0xC0 records), 004CDB30 areas (ScriptArea).

