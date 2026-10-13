#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CItem;
class CUnit;
class CWorldServer;
struct ObjectIdMap;
struct WorldPlayer;
struct Vector3;
struct VertexPacket;
struct DecalTexCoord;
namespace lib { template <typename T> struct AlignedArray; }

// 0047B670 (plain RET): an object id map with its highest id (the world's, the player's known objects): every
// object released (slot 4) until a null one, the nodes freed, count and highest id 0.
__declspec(naked) inline void __fastcall ObjectMapClearOriginal(void* /*map*/) EI_ORIGINAL(0x0047B670)
__declspec(naked) inline double __fastcall SurfaceHeightRaw(void* /*owner*/, void*, float /*x*/, float /*y*/)
    EI_ORIGINAL(0x00564070)
__declspec(naked) inline CUnit* __fastcall HoverObjectAt(void*, void*, const int*) EI_ORIGINAL(0x005659D0)
// 00565E40: the client world (007AF5B8) picks the unit drawn at a screen point (thiscall, RET 4: the address of
// the point's x, y; EAX = the unit or null). The battle screen passes its own (x, y) argument slots.
__declspec(naked) inline CUnit* __fastcall WorldUnitAtPointOriginal(void* /*world*/, void*, int* /*point*/)
    EI_ORIGINAL(0x00565E40)
__declspec(naked) inline void __fastcall ClientSetPlayer(void*, void*, int) EI_ORIGINAL(0x00566CE0)
__declspec(naked) inline float* __fastcall HoverPickSurface(void*, void*, float*, int, int) EI_ORIGINAL(0x0056C350)
__declspec(naked) inline void __fastcall CountedRelease(void*, void*, int) EI_ORIGINAL(0x00575510)
// 00512030 (library: STLport list<id>::erase(iterator), the client's selection list +0x7BC; thiscall, RET 8: the result
// slot, the node).
__declspec(naked) inline void __fastcall IdListEraseOriginal(void* /*list*/, void*, void** /*result*/, void* /*node*/)
    EI_ORIGINAL(0x00512030)
// A 3D object's client half (CObject3DClientSpecific, __fastcall on it): 0051E200 whether shadows are drawn for its
// object's kind (0079BD0C above 0 and the kind's switch; AL), 0051E280 drops its shadow (geometry, texture, bounds).
__declspec(naked) inline bool __fastcall ShadowsShownOriginal(void* /*half*/) EI_ORIGINAL(0x0051E200)
__declspec(naked) inline void __fastcall ShadowClearOriginal(void* /*half*/) EI_ORIGINAL(0x0051E280)
// 005655D0 / 005655F0 (thiscall on the client world, RET 4): a new texture of `kind` from its texture manager (+0x18C,
// 00691D40) / a new material from its material manager (+0x188, 0068E970); null without the manager.
class CTexture;
class CMaterial;
__declspec(naked) inline CTexture* __fastcall WorldNewTextureOriginal(void* /*world*/, void*, int /*kind*/)
    EI_ORIGINAL(0x005655D0)
__declspec(naked) inline CMaterial* __fastcall WorldNewMaterialOriginal(void* /*world*/, void*, int /*kind*/)
    EI_ORIGINAL(0x005655F0)
// 0056F360 (library: STLport vector<12-byte texture record>::resize(count, value); thiscall, RET 8, no result).
__declspec(naked) inline void __fastcall TextureRecordsResizeOriginal(void* /*vector*/, void*, unsigned /*count*/,
                                                                      const void* /*value*/) EI_ORIGINAL(0x0056F360)
// The figure models (LinkFigureSet, 0x20 bytes): 005A9A50 the constructor (returns this), 005A9C90 the destructor,
// 005A9EB0 (RET 0xC) loads the .mod / .bon / .anm parts (+0 / +4 / +8, slot 2) from the files' data that are not null.
__declspec(naked) inline void* __fastcall FigureModelConstructOriginal(void* /*model*/) EI_ORIGINAL(0x005A9A50)
__declspec(naked) inline void __fastcall FigureModelDestructOriginal(void* /*model*/) EI_ORIGINAL(0x005A9C90)
__declspec(naked) inline void __fastcall FigureModelLoadOriginal(void* /*model*/, void*, const void* /*mod*/,
                                                                 const void* /*bon*/, const void* /*anm*/)
    EI_ORIGINAL(0x005A9EB0)
__declspec(naked) inline void __fastcall MenuFrame578910(void*) EI_ORIGINAL(0x00578910)
__declspec(naked) inline double __fastcall HoverPlayerVariable(void*, void*, const char*) EI_ORIGINAL(0x00578B50)
// 005927C0 (RET 4): the server makes (or updates) the world object of a .mob record. Patched by Rebirth.
__declspec(naked) inline void __fastcall WorldMakeObjectOriginal(void* /*world*/, void*, void* /*spec*/)
    EI_ORIGINAL(0x005927C0)
__declspec(naked) inline void __fastcall WorldPrepareMap(void* /*world*/, void*, const char* /*name*/)
    EI_ORIGINAL(0x0057ED60)
// 005800C0 (rewritten as GameFlow::UpdateFromServer; patched by Rebirth, so called here): the game
// flow (007AFE20) applies a multiplayer client's update from the server (thiscall, plain RET).
__declspec(naked) inline void __fastcall FlowUpdateFromServerOriginal(void* /*flow*/) EI_ORIGINAL(0x005800C0)
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

// 0057DEE0 (thiscall on the game flow 007AFE20, RET 4): the flow's party setup step (the unit database rebuilt).
__declspec(naked) inline void __fastcall FlowPartyStepOriginal(void* /*flow*/, void*, int /*step*/) EI_ORIGINAL(0x0057DEE0)
// 00578E30 (rewritten as ShowBolt, world_weather.cpp; RET 0x18): the client glue (007AFEA4) shows a lightning bolt
// from (fromX, fromY, fromZ) to (toX, toY, toZ) (two Vector3 by value).
__declspec(naked) inline void __fastcall ShowBoltOriginal(void* /*glue*/, void*, float /*fromX*/, float /*fromY*/,
                                                          float /*fromZ*/, float /*toX*/, float /*toY*/, float /*toZ*/)
    EI_ORIGINAL(0x00578E30)

// 0055E940 (CWorld's slot 24, called directly on the client world 007AF5B8; RET 4): a new item made like `item` by the
// item factory and given its values; EAX is the copy.
__declspec(naked) inline CItem* __fastcall WorldCopyItemOriginal(void* /*world*/, void*, CItem* /*item*/) EI_ORIGINAL(0x0055E940)

// A quest variable ("q." prefix) changed: 005F7FC0 tells the quest log (007C7D50), 00565380 the client world (007AF5B8)
// (both RET 8: name, value bits; EAX not a result).
__declspec(naked) inline void __fastcall QuestLogUpdateOriginal(void* /*log*/, void*, const char* /*name*/,
                                                                std::uint32_t /*value*/) EI_ORIGINAL(0x005F7FC0)
__declspec(naked) inline void __fastcall ClientQuestUpdateOriginal(void* /*world*/, void*, const char* /*name*/,
                                                                   std::uint32_t /*value*/) EI_ORIGINAL(0x00565380)

// A-I10. 00578A40: the client glue (007AFEA5, not read) names the zone exit `exit` leads to (thiscall, RET 4; a C string,
// "none" for none).
__declspec(naked) inline const char* __fastcall GameExitZoneOriginal(void* /*glue*/, void*, int /*exit*/) EI_ORIGINAL(0x00578A40)
// D3b. 00573080: CArray<ZoneExit>::SetSize(size, growBy) (RET 8, no result; -1 keeps growBy, new records constructed).
__declspec(naked) inline void __fastcall ZoneExitsSetSizeOriginal(void* /*exits*/, void*, int /*size*/, int /*growBy*/)
    EI_ORIGINAL(0x00573080)
// 00591070 (RET 4; A-H14, shared by world_game.cpp and player_server_handlers.cpp): CWorldServer's
// step after a player's party records were synced and the party sent (the world in ECX).
__declspec(naked) inline void __fastcall WorldAfterPartyCopyOriginal(void* /*world*/, void*, WorldPlayer* /*player*/)
    EI_ORIGINAL(0x00591070)

// The client world (007AF5B8; thiscall, RET 4, results not used by the callers so far):
// 005651A0 (CWorldClient::RefreshPlatforms, called through its address for its wrapper) puts a filed object's platform
// faces where it now stands (dropped, then re-added for classes 0x54 / 0x5D through 00560820); 0056D7E0 queues an object by id (a list and a hash
// map; nothing when it is queued already).
__declspec(naked) inline void __fastcall ClientRefreshObjectOriginal(void* /*world*/, void*, CEngineObject* /*object*/)
    EI_ORIGINAL(0x005651A0)
__declspec(naked) inline void __fastcall ClientQueueObjectOriginal(void* /*world*/, void*, CEngineObject* /*object*/)
    EI_ORIGINAL(0x0056D7E0)
// A-X4. 006C5620: the landscape's Reset (Terrain::Reset, rewritten), called through its address by the
// client world's Setup and by the landscape's Create / Destroy so that its verify wrapper runs.
__declspec(naked) inline void __fastcall LandscapeResetOriginal(void* /*terrain*/) EI_ORIGINAL(0x006C5620)
inline void* ClientWorldObject() { return reinterpret_cast<void*>(0x007AF5B8); }
// The local player's client glue object (007AFEA5; its thiscall methods 00578xxx do not read it).
inline void* ClientGlueObject() { return reinterpret_cast<void*>(0x007AFEA5); }
// The game's glue object before it (007AFEA4; GameDirectoryOriginal and the like do not read it).
inline void* GameGlueObject() { return reinterpret_cast<void*>(0x007AFEA4); }
// A-X5. 0056C750 (CWorldClient::ProjectDecal, world_client_decals.cpp; called through its address so its
// wrapper runs; thiscall on the client world, RET 0x24): lays a decal on the ground (the quad `corners`, one
// 64-byte block of four corners, projected along `direction` from `point`) into the three arrays. Shadows
// (0051CE60) and the ground prints (006CEDE0).
__declspec(naked) inline void __fastcall ClientProjectDecalOriginal(void* /*world*/, void*, const Vector3* /*direction*/,
                                                                    const Vector3* /*point*/, float /*depth*/,
                                                                    VertexPacket* const* /*corners*/,
                                                                    lib::AlignedArray<VertexPacket>* /*points*/,
                                                                    lib::AlignedArray<DecalTexCoord>* /*texCoords*/,
                                                                    lib::AlignedArray<std::uint16_t>* /*indices*/,
                                                                    int /*skipOnPlatform*/, int /*objects*/)
    EI_ORIGINAL(0x0056C750)
