#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"
#include "lib/vecmath.h"

#include <cstddef>
#include <cstdint>

class CEngineObject;
class CItem;
class CUnit;
class CWorldServer;
class Terrain;
namespace net { class ClientSession; }
namespace mfc { class CString; }
struct ObjectIdMap;
struct SectorGrid;
struct WorldPlayer;
struct CDiplomacy;
struct Vector3;
struct VertexPacket;
struct DecalTexCoord;
namespace lib { template <typename T> struct AlignedArray; }

// 00572820 / 005728D0 (plain RET): the client's sector grids' storage (world.h SectorGrid) for its
// +0x08 x +0x0C cells: one list per cell (operator new, the count before them, the list instantiation's
// constructor through the eh vector iterator), then the row pointers. The constructor 005612F0 and
// Setup (1 x 1, then 4 x 4).
__declspec(naked) inline void __fastcall GridAllocate(SectorGrid* /*grid*/) EI_ORIGINAL(0x00572820)
__declspec(naked) inline void __fastcall GridAllocate2(SectorGrid* /*grid*/) EI_ORIGINAL(0x005728D0)
// 00572450 / 005724F0 (RET 8): the grid resized to (width, height) when its storage has another size:
// the cells' lists destroyed (eh vector iterator), the storage freed, then GridAllocate / GridAllocate2.
__declspec(naked) inline void __fastcall GridResize(SectorGrid* /*grid*/, void*, int /*width*/, int /*height*/)
    EI_ORIGINAL(0x00572450)
__declspec(naked) inline void __fastcall GridResize2(SectorGrid* /*grid*/, void*, int /*width*/, int /*height*/)
    EI_ORIGINAL(0x005724F0)

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
__declspec(naked) inline Vector3* __fastcall HoverPickSurface(void*, void*, Vector3*, int, int) EI_ORIGINAL(0x0056C350)
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
// The figure models (FigureModel, 0x20 bytes): 005A9A50 the constructor (returns this), 005A9C90 the destructor,
// 005A9EB0 (RET 0xC) loads the .mod / .bon / .anm parts (+0 / +4 / +8, slot 2) from the files' data that are not null.
__declspec(naked) inline void* __fastcall FigureModelConstructOriginal(void* /*model*/) EI_ORIGINAL(0x005A9A50)
__declspec(naked) inline void __fastcall FigureModelDestructOriginal(void* /*model*/) EI_ORIGINAL(0x005A9C90)
__declspec(naked) inline void __fastcall FigureModelLoadOriginal(void* /*model*/, void*, const void* /*mod*/,
                                                                 const void* /*bon*/, const void* /*anm*/)
    EI_ORIGINAL(0x005A9EB0)
__declspec(naked) inline void __fastcall MenuFrame578910(void*) EI_ORIGINAL(0x00578910)
__declspec(naked) inline double __fastcall HoverPlayerVariable(void*, void*, const char*) EI_ORIGINAL(0x00578B50)
// 005927C0 (rewritten as CWorldServer::CreateObject; RET 4): the server makes (or updates) the world object of a .mob
// record; EAX the object's id. Patched by Rebirth.
__declspec(naked) inline int __fastcall WorldMakeObjectOriginal(void* /*world*/, void*, void* /*spec*/)
    EI_ORIGINAL(0x005927C0)
// 005942D0 (rewritten as CWorldServer::Loot; RET 8, patched by Rebirth): `player` takes what the dead body or
// container `object` holds; AL whether there was anything to take. Called on the global server (007AFF60).
__declspec(naked) inline bool __fastcall WorldLootOriginal(void* /*world*/, void*, CEngineObject* /*object*/,
                                                           WorldPlayer* /*player*/) EI_ORIGINAL(0x005942D0)
// 00595AB0 (rewritten as CWorldServer::LoadMobFile; RET 4): loads a .mob file's objects once; EAX 1 when loaded (or
// already loaded), else the fatal error's result.
__declspec(naked) inline int __fastcall WorldLoadMobFileOriginal(void* /*world*/, void*, const char* /*file*/)
    EI_ORIGINAL(0x00595AB0)
// 0066ED30 (rewritten as CPlayerServer::SendPosition04; RET 0xC): the player's server sends message 0x04 (a position
// and two dwords: the start view's bits or 0, then the start kind); EAX the send's result.
__declspec(naked) inline int __fastcall PlayerSendPosition04Original(void* /*server*/, void*, const Vector3* /*at*/,
                                                                     std::uint32_t, std::uint32_t) EI_ORIGINAL(0x0066ED30)
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
// A-W15. 006C68B0 (RET 0xC; not rewritten): the landscape loads the map file `path` (the old map freed first) with
// the texture pages of `allod`; `cave` is read as a byte. All of EAX: 0 when the file or a page does not load.
__declspec(naked) inline int __fastcall LandscapeLoadMapOriginal(void* /*terrain*/, void*, const char* /*path*/,
                                                                const char* /*allod*/, bool /*cave*/) EI_ORIGINAL(0x006C68B0)
inline void* ClientWorldObject() { return reinterpret_cast<void*>(0x007AF5B8); }
// The local player's client glue object (007AFEA5; its thiscall methods 00578xxx do not read it).
inline void* ClientGlueObject() { return reinterpret_cast<void*>(0x007AFEA5); }
// The game's glue object before it (007AFEA4; GameDirectoryOriginal and the like do not read it).
inline void* GameGlueObject() { return reinterpret_cast<void*>(0x007AFEA4); }
// 00578D50 (the game's glue, not read; AL): whether the game is paused. 00578D30 (RET 4): pauses
// or resumes it (the low byte of `paused` is all its callee reads). The battle screen's keys and
// the clock's buttons.
__declspec(naked) inline bool __fastcall GameIsPausedOriginal(void* /*glue*/) EI_ORIGINAL(0x00578D50)
__declspec(naked) inline void __fastcall GameSetPausedOriginal(void* /*glue*/, void*, int /*paused*/)
    EI_ORIGINAL(0x00578D30)
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
// 0056CE40 (RET 0xC): the client world shows a number over the unit with id `id` (the value, the kind bits).
__declspec(naked) inline void __fastcall ClientShowNumberOriginal(void* /*world*/, void*, int /*id*/, int /*value*/,
                                                                int /*kind*/) EI_ORIGINAL(0x0056CE40)
// A-W13. CFigureMesh::CreateMesh 0059F210 (returns 1) and Clear 0059F2B0 (figure_mesh_life.cpp), called through
// their addresses by the rewritten constructor, clear and destructor bodies so that their wrappers run.
class CFigureMesh;
__declspec(naked) inline int __fastcall FigureMeshCreateOriginal(CFigureMesh* /*mesh*/) EI_ORIGINAL(0x0059F210)
__declspec(naked) inline void __fastcall FigureMeshClearOriginal(CFigureMesh* /*mesh*/) EI_ORIGINAL(0x0059F2B0)
// A-W13. The resource databases' switch (0057CB50, world_game.cpp): each database's compiler-generated
// reset (destroyed and constructed again in place: units 0057CBE0, skills 0057CD60, spell tables 0057CE80,
// items 0057D340) and its load from "<resources><name>" (RET 8: 0053A200, 00520220, and the Rebirth-patched
// 00684550 and 006A67A0).
struct UnitDatabase;
struct SkillDatabase;
__declspec(naked) inline void __fastcall UnitDatabaseResetOriginal(UnitDatabase* /*database*/) EI_ORIGINAL(0x0057CBE0)
__declspec(naked) inline void __fastcall SkillDatabaseResetOriginal(SkillDatabase* /*database*/) EI_ORIGINAL(0x0057CD60)
__declspec(naked) inline void __fastcall SpellTablesResetOriginal(void* /*tables*/) EI_ORIGINAL(0x0057CE80)
__declspec(naked) inline void __fastcall ItemDatabaseResetOriginal(void* /*database*/) EI_ORIGINAL(0x0057D340)
__declspec(naked) inline void __fastcall UnitDatabaseLoadOriginal(UnitDatabase* /*database*/, void*, const char* /*resources*/,
                                                                 const char* /*name*/) EI_ORIGINAL(0x0053A200)
__declspec(naked) inline void __fastcall SkillDatabaseLoadOriginal(SkillDatabase* /*database*/, void*, const char* /*resources*/,
                                                                  const char* /*name*/) EI_ORIGINAL(0x00520220)
__declspec(naked) inline void __fastcall SpellTablesLoadOriginal(void* /*tables*/, void*, const char* /*resources*/,
                                                                const char* /*name*/) EI_ORIGINAL(0x00684550)
__declspec(naked) inline void __fastcall ItemDatabaseLoadOriginal(void* /*database*/, void*, const char* /*resources*/,
                                                                 const char* /*name*/) EI_ORIGINAL(0x006A67A0)
// A-W13. Called through their addresses by the client world's item and spell block loaders (world_client_maps.cpp)
// so that their wrappers run: 005630B0 CWorldClient::ReferenceItemsAndSpells; 005679A0 LoadManagerObjects (ECX the
// manager, EDX the stream; 1 at the end mark, 0 when the stream fails).
class CBaseManager;
class CStream;
class CWorldClient;
__declspec(naked) inline void __fastcall ClientReferenceItemsAndSpellsOriginal(CWorldClient* /*world*/) EI_ORIGINAL(0x005630B0)
__declspec(naked) inline int __fastcall ClientLoadManagerObjectsOriginal(CBaseManager* /*manager*/, CStream* /*stream*/)
    EI_ORIGINAL(0x005679A0)
// A-W13. The client world's two small managers (world.h): 004E2850 the array manager's Clear (plain RET: its object
// and every listed one released, the list emptied); 00562EC0 / 00562DF0 their destructor bodies, called through their
// addresses by the deleting destructors.
struct ClientArrayManager;
struct ClientListManager;
__declspec(naked) inline void __fastcall ClientArrayManagerClearOriginal(ClientArrayManager* /*manager*/) EI_ORIGINAL(0x004E2850)
__declspec(naked) inline void __fastcall ClientListManagerDestructOriginal(ClientListManager* /*manager*/) EI_ORIGINAL(0x00562EC0)
__declspec(naked) inline void __fastcall ClientArrayManagerDestructOriginal(ClientArrayManager* /*manager*/) EI_ORIGINAL(0x00562DF0)
// 00561A60: the platform set's destructor body (PlatformSet::DestructBody), called through its address by the
// deleting destructor.
struct PlatformSet;
__declspec(naked) inline void __fastcall PlatformSetDestructOriginal(PlatformSet* /*set*/) EI_ORIGINAL(0x00561A60)
// A-E2: called by WinMain's main loop (app_main.cpp) and by the zone change (world_game.cpp) through their
// addresses so that their wrappers run. 005641F0: the client world's tick (CWorldClient::Segment; EAX 0 when the
// connection is lost). 0058DFF0: the server world's update (CWorldServer::Update; EAX 0 when it stops).
// 0057C700 (patched by Rebirth): the game flow (007AFE20) runs its pending request (GameFlow::ProcessRequest).
__declspec(naked) inline int __fastcall ClientSegmentOriginal(void* /*client*/) EI_ORIGINAL(0x005641F0)
// 00569E40 (patched by Rebirth: LandscapeDrawRadius): the client world draws a frame (CWorldClient::Render).
__declspec(naked) inline int __fastcall ClientRenderOriginal(void* /*client*/) EI_ORIGINAL(0x00569E40)
__declspec(naked) inline int __fastcall ServerUpdateOriginal(CWorldServer* /*server*/) EI_ORIGINAL(0x0058DFF0)
__declspec(naked) inline void __fastcall FlowProcessRequestOriginal(void* /*flow*/) EI_ORIGINAL(0x0057C700)
// C3m. 00596030 (rewritten as CWorldServer::LoadMapObjects; RET 8): loads a .mob file's objects (second argument:
// keep their ids).
__declspec(naked) inline int __fastcall WorldLoadMapObjects(CWorldServer*, void*, const char*, int) EI_ORIGINAL(0x00596030)

// C3m. 00593FC0 (rewritten as CWorldServer::InvokeAlarm; the world, thiscall): raises an alarm of `type` at `point`.
// Base of the views of the world.
struct WorldAlarmOriginal {
    void InvokeAlarm(int type, const Vector3* point, int raise);
};
__declspec(naked) inline void WorldAlarmOriginal::InvokeAlarm(int, const Vector3*, int) EI_ORIGINAL(0x00593FC0)
// C3n. 0041D260 (rewritten as ClientSession::SendCommand; thiscall, RET 8): the client session sends a command to the
// server (0 zone, 1 briefing, 2 kick, 3 ban).
__declspec(naked) inline void __fastcall SessionCommand(net::ClientSession* /*session*/, void*, int /*kind*/,
                                                       const char* /*argument*/) EI_ORIGINAL(0x0041D260)
// C3n. 00455660 (rewritten as CDiplomacy::GetHostileMask; thiscall, RET 4): the bit mask of the players hostile to `player`.
__declspec(naked) inline int __fastcall DiplomacyHostileMask(CDiplomacy* /*diplomacy*/, void*, int /*player*/)
    EI_ORIGINAL(0x00455660)

// C3n batch 3. The client world (007AF5B8) and the game: 005699F0 (rewritten as CWorldClient::LocalView; plain RET) the
// client world's local view (a LocalViewData, world.h); 00578BA0 (rewritten as LocalPlayerData, world_rules.cpp; plain RET)
// the game glue (007AFEA5) gives the local player's data (the unit records at +0x30 / +0x34). Through their addresses so
// the wrappers run.
__declspec(naked) inline std::uint8_t* __fastcall ClientWorldLocalViewOriginal(void* /*client*/) EI_ORIGINAL(0x005699F0)
__declspec(naked) inline std::uint8_t* __fastcall GameLocalPlayerDataOriginal(void* /*glue*/) EI_ORIGINAL(0x00578BA0)
// The game flow (007AFE20; GameFlow, world_game.cpp / world_briefing.cpp), through its addresses so the wrappers run:
// 0057F190 UpdateMultiplayerVariable, 0057F280 BeginMultiplayerGame, 00581660 CloseSessions (plain RET, no result);
// 0057FF40 MarkBriefing and 005810A0 RegisterQuests (thiscall, RET 4: a zone folder / name); 0057FDE0 BriefingVariableInto
// (stdcall, RET 8: the CString it constructs into `out` from the briefing's name; returns `out`).
__declspec(naked) inline void __fastcall FlowUpdateMultiplayerVariableOriginal(void* /*flow*/) EI_ORIGINAL(0x0057F190)
__declspec(naked) inline void __fastcall FlowBeginMultiplayerGameOriginal(void* /*flow*/) EI_ORIGINAL(0x0057F280)
__declspec(naked) inline void __fastcall FlowCloseSessionsOriginal(void* /*flow*/) EI_ORIGINAL(0x00581660)
__declspec(naked) inline void __fastcall FlowMarkBriefingOriginal(void* /*flow*/, void*, const char* /*zone*/)
    EI_ORIGINAL(0x0057FF40)
__declspec(naked) inline void __fastcall FlowRegisterQuestsOriginal(void* /*flow*/, void*, const char* /*zone*/)
    EI_ORIGINAL(0x005810A0)
__declspec(naked) inline mfc::CString* __stdcall BriefingVariableIntoOriginal(mfc::CString* /*out*/, const char* /*briefing*/)
    EI_ORIGINAL(0x0057FDE0)
// The server world (007AFF60; CWorldServer, world_*.cpp), thiscall, through its addresses so the wrappers run:
// 0058D3D0 WakeUnitsAround (RET 8: position, radius); 0058DC90 SetGameResult (RET 4); 0058E520 AddTimedObject (RET 4);
// 0058E850 CastSpell (RET 0x18: spell, caster, from, target, to, extra); 0058EF20 CreateUnits (RET 0x20: player, records,
// position, rotation by value, placements); 005911C0 AwardExperience (RET 8: player, the amount as float bits);
// 00591800 UpdateMercBriefings (plain RET); 00593AA0 FindObject (RET 4: an id; the object or null).
__declspec(naked) inline void __fastcall WorldWakeUnits(void* /*world*/, void*, const Vector3* /*position*/, float /*radius*/)
    EI_ORIGINAL(0x0058D3D0)
__declspec(naked) inline void __fastcall WorldSetGameResultOriginal(void* /*world*/, void*, int /*result*/)
    EI_ORIGINAL(0x0058DC90)
__declspec(naked) inline void __fastcall WorldAddTimed(void* /*world*/, void*, CEngineObject* /*object*/) EI_ORIGINAL(0x0058E520)
__declspec(naked) inline void __fastcall WorldCastSpellOriginal(void* /*world*/, void*, const void* /*spell*/,
                                                                CEngineObject* /*caster*/, const Vector3* /*from*/,
                                                                CEngineObject* /*target*/, const Vector3* /*to*/, int /*extra*/)
    EI_ORIGINAL(0x0058E850)
__declspec(naked) inline void __fastcall WorldCreateUnitsOriginal(void* /*world*/, void*, void* /*player*/, void* /*records*/,
                                                                  const Vector3* /*position*/, Quaternion /*rotation*/,
                                                                  void* /*placements*/) EI_ORIGINAL(0x0058EF20)
__declspec(naked) inline void __fastcall WorldAwardExperienceOriginal(void* /*world*/, void*, WorldPlayer* /*player*/,
                                                                      std::uint32_t /*amount bits*/) EI_ORIGINAL(0x005911C0)
__declspec(naked) inline void __fastcall WorldUpdateMercBriefingsOriginal(void* /*world*/) EI_ORIGINAL(0x00591800)
__declspec(naked) inline CEngineObject* __fastcall WorldFindObjectOriginal(void* /*world*/, void*, unsigned /*id*/)
    EI_ORIGINAL(0x00593AA0)

// C3n batch 5. 006C8680 (rewritten as Terrain::HeightAt; thiscall, RET 0xC): the ground height at (x, y) on `layer`, in ST0 and used
// unrounded. 006D43E0 (TweenArray::RemoveAll, an mfc::CArray of tween entries; plain RET): frees the array, size and capacity 0
// (the world countdown list at +0x70 is one).
__declspec(naked) inline double __fastcall TerrainHeightAtOriginal(Terrain* /*terrain*/, void*, float /*x*/, float /*y*/,
                                                                  int /*layer*/) EI_ORIGINAL(0x006C8680)
__declspec(naked) inline void __fastcall TweenArrayRemoveAllOriginal(void* /*tweens*/) EI_ORIGINAL(0x006D43E0)

// S13a. The server destructor's (0058AAD0) members not ported yet (all plain RET, thiscall on the member):
// 0058A140 the first script's destructor (CScript, +0x12874), 0058A450 the AI map's (+0x170). The second
// script's destructor is inlined; its member destructors are MFC rows of their own: 004C92C0 (+0x1CC),
// 004C8E20 (+0x1B0), 004CB650 (+0x194), 004C7560 (+0x178 and +0x70), 004C8520 (+0x15C), 004C7E30 (+0x140),
// 004CB310 (+0x8C), 004C6760 (+0x4C), 004C6410 (+0x30), 004C6120 (+0x14), 004CB150 (+0).
__declspec(naked) inline void __fastcall ScriptDestructOriginal(void* /*script*/) EI_ORIGINAL(0x0058A140)
__declspec(naked) inline void __fastcall AIMapDestructOriginal(void* /*map*/) EI_ORIGINAL(0x0058A450)
__declspec(naked) inline void __fastcall ScriptMember4C92C0(void*) EI_ORIGINAL(0x004C92C0)
__declspec(naked) inline void __fastcall ScriptMember4C8E20(void*) EI_ORIGINAL(0x004C8E20)
__declspec(naked) inline void __fastcall ScriptMember4CB650(void*) EI_ORIGINAL(0x004CB650)
__declspec(naked) inline void __fastcall ScriptMember4C7560(void*) EI_ORIGINAL(0x004C7560)
__declspec(naked) inline void __fastcall ScriptMember4C8520(void*) EI_ORIGINAL(0x004C8520)
__declspec(naked) inline void __fastcall ScriptMember4C7E30(void*) EI_ORIGINAL(0x004C7E30)
__declspec(naked) inline void __fastcall ScriptMember4CB310(void*) EI_ORIGINAL(0x004CB310)
__declspec(naked) inline void __fastcall ScriptMember4C6760(void*) EI_ORIGINAL(0x004C6760)
__declspec(naked) inline void __fastcall ScriptMember4C6410(void*) EI_ORIGINAL(0x004C6410)
__declspec(naked) inline void __fastcall ScriptMember4C6120(void*) EI_ORIGINAL(0x004C6120)
__declspec(naked) inline void __fastcall ScriptMember4CB150(void*) EI_ORIGINAL(0x004CB150)
