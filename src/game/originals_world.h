#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CUnit;
class CWorldServer;
struct ObjectIdMap;

__declspec(naked) inline double __fastcall SurfaceHeightRaw(void* /*owner*/, void*, float /*x*/, float /*y*/)
    EI_ORIGINAL(0x00564070)
__declspec(naked) inline CUnit* __fastcall HoverObjectAt(void*, void*, const int*) EI_ORIGINAL(0x005659D0)
__declspec(naked) inline void __fastcall ClientSetPlayer(void*, void*, int) EI_ORIGINAL(0x00566CE0)
__declspec(naked) inline float* __fastcall HoverPickSurface(void*, void*, float*, int, int) EI_ORIGINAL(0x0056C350)
__declspec(naked) inline void __fastcall CountedRelease(void*, void*, int) EI_ORIGINAL(0x00575510)
__declspec(naked) inline void __fastcall MenuFrame578910(void*) EI_ORIGINAL(0x00578910)
__declspec(naked) inline double __fastcall HoverPlayerVariable(void*, void*, const char*) EI_ORIGINAL(0x00578B50)
__declspec(naked) inline void __fastcall WorldPrepareMap(void* /*world*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x0057ED60)
// 0057F440 (rewritten as IsMultiplayerClient): no arguments (a plain RET, ECX is not read); AL/EAX = 1 when
// the network game exists and is not the host (flag 0x80000000 of its +4 clear), else 0.
__declspec(naked) inline bool __cdecl IsNetworkClientOriginal() EI_ORIGINAL(0x0057F440)
__declspec(naked) inline void* __fastcall WorldMapTitle(void* /*world*/, void*, void* /*out CString*/)
    EI_ORIGINAL(0x005814C0)
__declspec(naked) inline void __fastcall UpdatePlayerTimesAt(CWorldServer*, void*, float) EI_ORIGINAL(0x0058D280)
__declspec(naked) inline void __fastcall TickAt(CWorldServer*, void*, int /*force*/) EI_ORIGINAL(0x0058DCD0)
__declspec(naked) inline void* __fastcall ItemRecordConstruct(void*, void*) EI_ORIGINAL(0x0058FBB0)
// 005935C0 (rewritten as CWorldServer::RemoveObject; RET 4): removes the object with this id from the world
// (EAX on return is not a result).
__declspec(naked) inline void __fastcall WorldRemoveObject(void* /*world*/, void*, int /*id*/) EI_ORIGINAL(0x005935C0)
// 0055FB00 (RET 4): the world's object with this id when it is a unit (type 0x50), else null.
__declspec(naked) inline CEngineObject* __fastcall WorldFindUnit(void* /*world*/, void*, int /*id*/)
    EI_ORIGINAL(0x0055FB00)
// 0058E5D0 (rewritten as CWorldServer::RemoveTimedObject; RET 4): EAX on return is not a result.
__declspec(naked) inline void __fastcall WorldRemoveTimed(void* /*world*/, void*, int /*id*/) EI_ORIGINAL(0x0058E5D0)
// 00594180 (rewritten as CWorldServer::GetPlayer; RET 4): the player record with this id, or null.
__declspec(naked) inline void* __fastcall WorldGetPlayer(void* /*world*/, void*, int /*id*/) EI_ORIGINAL(0x00594180)
__declspec(naked) inline void __fastcall IdMapEraseOriginal(ObjectIdMap*, void*, const unsigned*)
    EI_ORIGINAL(0x00599A80)
