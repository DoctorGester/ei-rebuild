#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <windows.h>
#include "lib/mfc.h"

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
