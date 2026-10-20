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

// C3i part 2. 004C4FD0: CArray<SubCall>::SetSize(count, growBy) (RET 8).
__declspec(naked) inline void __fastcall SubCallArraySetSizeOriginal(void* /*array*/, void*, int /*count*/, int /*growBy*/)
    EI_ORIGINAL(0x004C4FD0)

// C3k. 004C2030: script group (CArray with vtable 0073D11C) constructor (plain RET, returns `this`). 004C59C0 / 004CD2B0:
// CArray<ScriptIfThen> / CArray<SSCCmd>::SetSize(size, growBy) (both RET 8, no result).
__declspec(naked) inline void* __fastcall ScriptGroupConstructOriginal(void* /*group*/) EI_ORIGINAL(0x004C2030)
__declspec(naked) inline void __fastcall IfThenArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x004C59C0)
__declspec(naked) inline void __fastcall CmdArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x004CD2B0)

// The link figures' STLport map<string, ...> members (link_figure.h; library instantiations):
// 004F4B90 the part headers' operator[] (RET 4, EAX = the value, a zeroed 0x50-byte header when new),
// 004F4CE0 the part ranges' operator[] (RET 4, EAX = the value).
struct LinkPartHeader;
struct LinkPartRanges;
// 004F4E40: the part ranges' erase(iterator) (RET 4; the node, a LinkRangeMap::Node, goes back to the node pool).
__declspec(naked) inline void __fastcall LinkRangeMapEraseOriginal(stlp::map<stlp::string, LinkPartRanges>* /*map*/, void*,
                                                                   void* /*node*/) EI_ORIGINAL(0x004F4E40)

// A-E1. 004ABA70 (RET 4): hashtable<pair<const int, CParty>>::_M_delete_node on the party manager's map: the node's
// party destroyed, the node back to the node pool.
__declspec(naked) inline void __fastcall PartyMapDeleteNodeOriginal(void* /*map*/, void*, void* /*node*/)
    EI_ORIGINAL(0x004ABA70)
// C3m. 004CE210 (rewritten as ScriptGroupDestroy): a script group destroyed (its commands, then the group).
__declspec(naked) inline void __fastcall GroupDestroy(void*, void*, int) EI_ORIGINAL(0x004CE210)
// C3m. CArray::SetSize (__thiscall (size, growBy)) of the script arrays: 004C4410 float args, 004C46F0 object args,
// 004C49F0 group args, 004C4CE0 bindings, 004C3980 group values, 004CD770 definitions (0xC0 records), 004CDB30 areas
// (ScriptArea); 004CC800 is ConstructElements<ScriptObjectRef> (__stdcall (first, count); zeroes).
__declspec(naked) inline void __fastcall BindingsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4CE0)
__declspec(naked) inline void __fastcall FloatArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C4410)
__declspec(naked) inline void __fastcall ObjectArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C46F0)
__declspec(naked) inline void __fastcall GroupArgsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C49F0)
__declspec(naked) inline void __fastcall GroupValuesSetSize(void*, void*, int, int) EI_ORIGINAL(0x004C3980)
__declspec(naked) inline void __fastcall DefinitionsSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CD770)
__declspec(naked) inline void __fastcall AreasSetSize(void*, void*, int, int) EI_ORIGINAL(0x004CDB30)
__declspec(naked) inline void __stdcall ConstructRefs(void*, int) EI_ORIGINAL(0x004CC800)

