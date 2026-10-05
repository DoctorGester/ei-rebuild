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

// C3h. 005809D0: the game flow loads the quests of a zone folder (RET 4, AL = loaded). The flow is the global at
// 007AF788; ECX is not read, so the callers' flow argument was a dummy.
__declspec(naked) inline bool __stdcall FlowLoadQuestsOriginal(const char* /*zone*/) EI_ORIGINAL(0x005809D0)

// C3i part 2. 0058EEA0: CWorldServer::SetTime(float) (RET 4; the argument is a raw dword, float callers pass
// std::bit_cast). 00595BA0: the map path text for a name into `out` (RET 8, returns out).
__declspec(naked) inline void __fastcall WorldSetTimeOriginal(void* /*world*/, void*, std::uint32_t /*timeBits*/)
    EI_ORIGINAL(0x0058EEA0)
__declspec(naked) inline void* __fastcall ServerMapPathOriginal(void* /*world*/, void*, void* /*out*/, const char* /*name*/)
    EI_ORIGINAL(0x00595BA0)

// C3j. 00578E20: the game flow object (global 007AF788; plain RET, ECX unused, EAX = flow, callers use it).
__declspec(naked) inline void* __cdecl GameFlowOriginal() EI_ORIGINAL(0x00578E20)
// 00578920: menu call through the global at 007AF784 (ECX is loaded from it; RET 4, one stack argument, EAX not a result).
__declspec(naked) inline void __stdcall MenuSetOriginal(int) EI_ORIGINAL(0x00578920)
// 00578940: tail-jumps to a menu call through the global at 007AF738 (no arguments, ECX loaded from it, EAX not a result).
__declspec(naked) inline void __cdecl MenuCallOriginal() EI_ORIGINAL(0x00578940)
// 0055FC50: the world's time of day in ST0 (ECX = world, plain RET).
__declspec(naked) inline double __fastcall WorldDayTimeOriginal(void* /*world*/) EI_ORIGINAL(0x0055FC50)
// 0058EEE0: sets the time speed (RET 4; the float is only copied, so it is passed as raw dword bits; EAX not a result).
__declspec(naked) inline void __fastcall WorldSetTimeSpeedOriginal(void* /*world*/, void*, std::uint32_t /*speedBits*/)
    EI_ORIGINAL(0x0058EEE0)
// 00595480: CWorldServer::NewZone(map, mob, zone) (RET 0xC, EAX 0 = failed).
__declspec(naked) inline int __fastcall WorldNewZoneOriginal(void* /*world*/, void*, const char* /*map*/, const char* /*mob*/,
                                                             const char* /*zone*/) EI_ORIGINAL(0x00595480)
