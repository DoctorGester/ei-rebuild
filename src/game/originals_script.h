#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <windows.h>
#include "lib/mfc.h"
#include "lib/stlp.h"

class CEngineObject;
class CScript;
class GameBuffer;
struct ScriptObjectRef;
struct ScriptRun;
struct ScriptVars;

__declspec(naked) inline CEngineObject* __fastcall RefConstruct(ScriptObjectRef*, void*, const ScriptObjectRef*)
    EI_ORIGINAL(0x004B2500)
__declspec(naked) inline void __fastcall RemoveDeletedOrig(ScriptVars*) EI_ORIGINAL(0x004B3940)
__declspec(naked) inline BOOL __fastcall ExecuteOrig(ScriptRun*, void*, CScript*) EI_ORIGINAL(0x004B6760)
__declspec(naked) inline void __fastcall QueueReset(void*, void*) EI_ORIGINAL(0x004BE6E0)
__declspec(naked) inline void __fastcall FreeTemporariesOrig(CScript*) EI_ORIGINAL(0x004BEC80)
__declspec(naked) inline int __fastcall LoadScriptFile(void*, void*, const char*) EI_ORIGINAL(0x004D36F0)
__declspec(naked) inline unsigned __fastcall BufferReadPastEnd(GameBuffer* /*buffer*/, void*, void* /*to*/, unsigned /*size*/)
    EI_ORIGINAL(0x004DA170)
__declspec(naked) inline void __fastcall GameBufferCopy(GameBuffer* /*to*/, void*, const GameBuffer* /*from*/)
    EI_ORIGINAL(0x004DA9E0)
__declspec(naked) inline void __fastcall TextsUnload(void*, void*, const char*) EI_ORIGINAL(0x004DEDD0)

// 004D23A0 (RET 8, EAX 0/1): adds / loads a script line (CScript::Load, rewritten; called through its address).
__declspec(naked) inline int __fastcall ScriptLoadLine(void* /*script*/, void*, const mfc::CString* /*line*/,
                                                       int /*flag*/) EI_ORIGINAL(0x004D23A0)

// C3h. 004C1290: script group / reference array assignment (RET 4, EAX not a result); 004C2290: its destructor (plain RET).
__declspec(naked) inline void __fastcall GroupAssignOriginal(void* /*group*/, void*, const void* /*source*/)
    EI_ORIGINAL(0x004C1290)
__declspec(naked) inline void __fastcall ScriptGroupDestructOriginal(void* /*group*/) EI_ORIGINAL(0x004C2290)
// 004B3200: the group's objects replace the contents of a CPtrList of counted references (RET 4: the list; EAX not a
// result).
__declspec(naked) inline void __fastcall ScriptGroupToListOriginal(const void* /*group*/, void*, void* /*list*/)
    EI_ORIGINAL(0x004B3200)

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

// C3j. 004B5EF0: ScriptGroup destructor body (plain RET, no result).
__declspec(naked) inline void __fastcall ScriptGroupBodyDestructOriginal(void* /*group*/) EI_ORIGINAL(0x004B5EF0)

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
// 004BE490 (rewritten as ScriptDropInstancesAndRuns, script_cleanup.cpp; plain RET): the zone reset calls it
// through its address so the verify wrapper sees the call.
__declspec(naked) inline void __fastcall ScriptDropInstancesAndRunsOriginal(CScript* /*script*/) EI_ORIGINAL(0x004BE490)

// The link figures' STLport map<string, ...> members (link_figure.h; library instantiations):
// 004F4B90 the part headers' operator[] (RET 4, EAX = the value, a zeroed 0x50-byte header when new),
// 004F4CE0 the part ranges' operator[] (RET 4, EAX = the value), 004F5C90 the part ranges' find
// (RET 8, *out = the node or the header, EAX = out).
struct LinkStringMap;
struct LinkPartHeader;
struct LinkPartRanges;
namespace stlp {
class string;
}
__declspec(naked) inline LinkPartHeader* __fastcall LinkHeaderMapAtOriginal(LinkStringMap* /*map*/, void*,
                                                                            const stlp::string* /*key*/)
    EI_ORIGINAL(0x004F4B90)
__declspec(naked) inline LinkPartRanges* __fastcall LinkRangeMapAtOriginal(LinkStringMap* /*map*/, void*,
                                                                           const stlp::string* /*key*/)
    EI_ORIGINAL(0x004F4CE0)
__declspec(naked) inline void** __fastcall LinkRangeMapFindOriginal(LinkStringMap* /*map*/, void*, void** /*out*/,
                                                                    const stlp::string* /*key*/)
    EI_ORIGINAL(0x004F5C90)
// 004F4E40: the part ranges' erase(iterator) (RET 4; the node goes back to the node pool).
struct LinkMapNode;
__declspec(naked) inline void __fastcall LinkRangeMapEraseOriginal(LinkStringMap* /*map*/, void*, LinkMapNode* /*node*/)
    EI_ORIGINAL(0x004F4E40)

// A-S7. 004D8EB0: STLport vector<char>::resize(n, value) (RET 8, EAX not a result): cut, or filled with *value
// (0042D4E0 grows it). Used by the .mob script decoder and CWorldServer::LoadZoneScripts.
__declspec(naked) inline void __fastcall CharVectorResizeOriginal(stlp::vector<char>* /*v*/, void*, unsigned /*n*/,
                                                                  const char* /*value*/) EI_ORIGINAL(0x004D8EB0)
