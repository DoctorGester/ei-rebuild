#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>
#include <windows.h>

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
