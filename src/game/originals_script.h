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

__declspec(naked) inline CEngineObject* __fastcall RefConstruct(ScriptObjectRef*, void*, const ScriptObjectRef*)
    EI_ORIGINAL(0x004B2500)
__declspec(naked) inline int __fastcall ExecuteOrig(ScriptRun*, void*, CScript*) EI_ORIGINAL(0x004B6760)
__declspec(naked) inline void __fastcall QueueReset(void*, void*) EI_ORIGINAL(0x004BE6E0)

// 004D23A0 (RET 8, EAX 0/1): adds / loads a script line (CScript::Load, rewritten; called through its address).
__declspec(naked) inline int __fastcall ScriptLoadLine(void* /*script*/, void*, const mfc::CString* /*line*/,
                                                       int /*flag*/) EI_ORIGINAL(0x004D23A0)

// C3h. 004C1290: script group / reference array assignment (RET 4, EAX not a result); 004C2290: its destructor (plain RET).
__declspec(naked) inline void __fastcall GroupAssignOriginal(void* /*group*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x004C1290)
__declspec(naked) inline void __fastcall ScriptGroupDestructOriginal(void* /*group*/) EI_ORIGINAL(0x004C2290)

// C3i. 004CC7D0: CArray<ScriptObjectRef> DestructElements, releasing each reference (RET 8: first, count; EAX not a result).
__declspec(naked) inline void __stdcall ScriptRefElementsDestruct(void* /*first*/, int /*count*/) EI_ORIGINAL(0x004CC7D0)

// C3i part 2. 004C21E0 / 004C2680 / 004C3B00: CArray GetAt(out, index) for references / strings / groups (RET 8, returns
// out). 004C4FD0: CArray<SubCall>::SetSize(count, growBy) (RET 8).
__declspec(naked) inline void* __fastcall ScriptRefAtOriginal(const void* /*array*/, void*, void* /*out*/, int /*index*/)
    EI_ORIGINAL(0x004C21E0)
__declspec(naked) inline void* __fastcall ScriptStringAtOriginal(const void* /*array*/, void*, void* /*out*/, int /*index*/)
    EI_ORIGINAL(0x004C2680)
__declspec(naked) inline void* __fastcall ScriptGroupAtOriginal(const void* /*array*/, void*, void* /*out*/, int /*index*/)
    EI_ORIGINAL(0x004C3B00)
__declspec(naked) inline void __fastcall SubCallArraySetSizeOriginal(void* /*array*/, void*, int /*count*/, int /*growBy*/)
    EI_ORIGINAL(0x004C4FD0)

// C3k. 004C2030: script group (CArray with vtable 0073D11C) constructor (plain RET, returns `this`). 004C59C0 / 004CD2B0:
// CArray<ScriptIfThen> / CArray<SSCCmd>::SetSize(size, growBy) (both RET 8, no result).
__declspec(naked) inline void* __fastcall ScriptGroupConstructOriginal(void* /*group*/) EI_ORIGINAL(0x004C2030)
__declspec(naked) inline void __fastcall IfThenArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x004C59C0)
__declspec(naked) inline void __fastcall CmdArraySetSizeOriginal(void* /*array*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x004CD2B0)
// 004C8AF0 / 004C9100: RemoveTail of the script's temporary group / object lists; the removed value is copied to `out`
// (RET 4, EAX = out).
__declspec(naked) inline void* __fastcall TempGroupsRemoveTailOriginal(void* /*list*/, void*, void* /*out*/)
    EI_ORIGINAL(0x004C8AF0)
__declspec(naked) inline void* __fastcall TempObjectsRemoveTailOriginal(void* /*list*/, void*, void* /*out*/)
    EI_ORIGINAL(0x004C9100)

// The link figures' STLport map<string, ...> members (link_figure.h; library instantiations):
// 004F4B90 the part headers' operator[] (RET 4, EAX = the value, a zeroed 0x50-byte header when new),
// 004F4CE0 the part ranges' operator[] (RET 4, EAX = the value).
struct LinkPartHeader;
struct LinkPartRanges;
__declspec(naked) inline LinkPartHeader* __fastcall LinkHeaderMapAtOriginal(stlp::map<stlp::string, LinkPartHeader>* /*map*/,
                                                                            void*, const stlp::string* /*key*/)
    EI_ORIGINAL(0x004F4B90)
__declspec(naked) inline LinkPartRanges* __fastcall LinkRangeMapAtOriginal(stlp::map<stlp::string, LinkPartRanges>* /*map*/,
                                                                           void*, const stlp::string* /*key*/)
    EI_ORIGINAL(0x004F4CE0)
// 004F4E40: the part ranges' erase(iterator) (RET 4; the node, a LinkRangeMap::Node, goes back to the node pool).
__declspec(naked) inline void __fastcall LinkRangeMapEraseOriginal(stlp::map<stlp::string, LinkPartRanges>* /*map*/, void*,
                                                                   void* /*node*/) EI_ORIGINAL(0x004F4E40)

// A-S7. 004D8EB0: STLport vector<char>::resize(n, value) (RET 8, EAX not a result): cut, or filled with *value
// (0042D4E0 grows it). Used by the .mob script decoder and CWorldServer::LoadZoneScripts.
__declspec(naked) inline void __fastcall CharVectorResizeOriginal(stlp::vector<char>* /*v*/, void*, unsigned /*n*/,
                                                                  const char* /*value*/) EI_ORIGINAL(0x004D8EB0)

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

// C3o. 004CE1E0 (rewritten as ScriptRefDestroy; thiscall, RET 4): a script object reference's scalar deleting destructor
// (the object released through its slot 4; the reference freed when flags & 1); returns the reference.
__declspec(naked) inline void* __fastcall ScriptRefDestroyOriginal(void* /*ref*/, void*, int /*flags*/)
    EI_ORIGINAL(0x004CE1E0)
