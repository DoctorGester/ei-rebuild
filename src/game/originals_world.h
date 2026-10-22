#pragma once

// Original functions forwarded from several files (one declaration per address; the bodies
// jump to game.exe). Types are forward declared; include the class headers where members are used.

#include "ei/hook.h"
#include "lib/vecmath.h"
#include "game_data.h"

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
class CPlayer;
struct CDiplomacy;
struct Vector3;
struct VertexPacket;
struct DecalTexCoord;
namespace lib { template <typename T> struct AlignedArray; }

// 00565E40: the client world (007AF5B8) picks the unit drawn at a screen point (thiscall, RET 4: the address of
// the point's x, y; EAX = the unit or null). The battle screen passes its own (x, y) argument slots.
class CTexture;
class CMaterial;
// 005800C0 (rewritten as GameFlow::UpdateFromServer; patched by Rebirth, so called here): the game
// flow (007AFE20) applies a multiplayer client's update from the server (thiscall, plain RET).
// 0055FB00 (RET 4): the world's object with this id when it is a unit (type 0x50), else null.

// C3h. 005809D0: the game flow loads the quests of a zone folder (RET 4, AL = loaded). The flow is the global at
// 007AF788; ECX is not read, so the callers' flow argument was a dummy.

// C3i part 2. 0058EEA0: CWorldServer::SetTime(float) (RET 4; the argument is a raw dword, float callers pass
// std::bit_cast). 00595BA0: the map path text for a name into `out` (RET 8, returns out).
__declspec(naked) inline void* __fastcall ServerMapPathOriginal(void* /*world*/, void*, void* /*out*/, const char* /*name*/)
    EI_ORIGINAL(0x00595BA0)

// C3j. 00578E20: the game flow object (global 007AF788; plain RET, ECX unused, EAX = flow, callers use it).
// 00595480: CWorldServer::NewZone(map, mob, zone) (RET 0xC, EAX 0 = failed).

// 0057DEE0 (thiscall on the game flow 007AFE20, RET 4): the flow's party setup step (the unit database rebuilt).

// 0055E940 (CWorld's slot 24, called directly on the client world 007AF5B8; RET 4): a new item made like `item` by the
// item factory and given its values; EAX is the copy.

// A quest variable ("q." prefix) changed: 005F7FC0 tells the quest log (007C7D50), 00565380 the client world (007AF5B8)
// (both RET 8: name, value bits; EAX not a result).

// A-W15. 006C68B0 (RET 0xC; not rewritten): the landscape loads the map file `path` (the old map freed first) with
// the texture pages of `allod`; `cave` is read as a byte. All of EAX: 0 when the file or a page does not load.
__declspec(naked) inline int __fastcall LandscapeLoadMapOriginal(void* /*terrain*/, void*, const char* /*path*/,
                                                                const char* /*allod*/, bool /*cave*/) EI_ORIGINAL(0x006C68B0)
// A-X5. 0056C750 (CWorldClient::ProjectDecal, world_client_decals.cpp; called through its address so its
// wrapper runs; thiscall on the client world, RET 0x24): lays a decal on the ground (the quad `corners`, one
// 64-byte block of four corners, projected along `direction` from `point`) into the three arrays. Shadows
// (0051CE60) and the ground prints (006CEDE0).
class CFigureMesh;
// A-W13. The resource databases' switch (0057CB50, world_game.cpp): each database's compiler-generated
// reset (destroyed and constructed again in place: units 0057CBE0, skills 0057CD60, spell tables 0057CE80,
// items 0057D340) and its load from "<resources><name>" (RET 8: 0053A200, 00520220, and the Rebirth-patched
// 00684550 and 006A67A0).
struct UnitDatabase;
struct SkillDatabase;
class CBaseManager;
class CStream;
class CWorldClient;
// A-W13. The client world's two small managers (world.h): 004E2850 the array manager's Clear (plain RET: its object
// and every listed one released, the list emptied).
struct ClientArrayManager;
void __fastcall VisFogManagerDestroy(ClientArrayManager* /*manager*/);
// 004E28D0 (vis_fog_manager.cpp): a new fog object, counted, at the array manager's list end.
CEngineObject* __fastcall VisFogManagerAdd(ClientArrayManager* manager);
struct PlatformSet;
// A-E2: called by WinMain's main loop (app_main.cpp) and by the zone change (world_game.cpp) through their
// addresses so that their wrappers run. 005641F0: the client world's tick (CWorldClient::Segment; EAX 0 when the
// connection is lost). 0058DFF0: the server world's update (CWorldServer::Update; EAX 0 when it stops).
// 0057C700 (patched by Rebirth): the game flow (007AFE20) runs its pending request (GameFlow::ProcessRequest).
__declspec(naked) inline int __fastcall ClientSegmentOriginal(void* /*client*/) EI_ORIGINAL(0x005641F0)
__declspec(naked) inline int __fastcall ServerUpdateOriginal(CWorldServer* /*server*/) EI_ORIGINAL(0x0058DFF0)

// The server world (007AFF60; CWorldServer, world_*.cpp), thiscall, through its addresses so the wrappers run:
// 0058E520 AddTimedObject (RET 4);
// 0058E850 CastSpell (RET 0x18: spell, caster, from, target, to, extra); 0058EF20 CreateUnits (RET 0x20: player, records,
// position, rotation by value, placements); 005911C0 AwardExperience (RET 8: player, the amount as float bits);
// 00591800 UpdateMercBriefings (plain RET); 00593AA0 FindObject (RET 4: an id; the object or null).
__declspec(naked) inline void __fastcall WorldCreateUnitsOriginal(void* /*world*/, void*, void* /*player*/, void* /*records*/,
                                                                  const Vector3* /*position*/, Quaternion /*rotation*/,
                                                                  void* /*placements*/) EI_ORIGINAL(0x0058EF20)
__declspec(naked) inline CEngineObject* __fastcall WorldFindObjectOriginal(void* /*world*/, void*, unsigned /*id*/)
    EI_ORIGINAL(0x00593AA0)
