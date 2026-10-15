#pragma once

// The world: the server side that runs the game rules (CWorldServer, the global at 007AFF60) and
// the client side that draws it and answers terrain queries (CWorldClient, the global at 007AF5B8).
// Both derive from a common base whose methods live at 0055E000-0055FFFF. Class names come from
// the game's error strings ("CWorldServer::LoadMap", "CWorldClient::ToggleWireframe"); everything
// else is provisional. Only the members used by the rewritten functions are declared.

#include <cstddef>
#include <cstdint>

#include "base_manager.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "engine_object.h"
#include "ground_print.h"
#include "object_diplomacy.h"
#include "player_source.h"
#include "unit_looks.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "maths.h"
#include "originals_core.h"

using BOOL = int;

struct ScriptGroup;
class CAIMap;       // ai_map.h
class CPlayerServer;   // player_messages.h
class CPlayerConnection;   // player_messages.h
class CScript;   // script.h
struct AIEventTable;   // unit_ai.h
namespace netlink {
class NetLink;   // net_link.h
}
namespace net {
struct NetAddress;   // net_classes.h
}
class CBaseManager;
struct CSoundManager;   // sound.h
class CSectorMap;   // sector_map.h
class CSpellManager;   // spell.h
class CLinkFigureLoader;   // model.h
class CTexture;   // texture.h
class CTextureManager;   // texture.h
class Terrain;
class CCamera;   // camera.h
class CMaterial;   // material.h
class CStream;   // core_streams.h
class CParticleManager;   // effects.h
struct FootprintPart;   // ai_map.h: a figure part's box (CFigureMesh +0x188)
class CFileRegistry;   // registry.h
class CHoover;   // object.h
class CWorldServer;

namespace world {
constexpr int kClassUnit = 0x50;   // CEngineObject::GetClassId() of units
}

// hash_map<unsigned, CEngineObject*> plus the highest key ever inserted (several instantiations
// share this shape: 005999E0 / 00599BD0 are its Set).
struct ObjectIdMap {
    stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash> map;   // +0x00
    int maxId;                                                      // +0x14

    // 005999E0 and 00599BD0 (identical code, different template instantiations): map[key] = value
    // and raise maxId.
    void Set(const unsigned& key, CEngineObject* const& value);
    // 0047B670 (A-E1; object_id_map.cpp). Releases every object up to the first empty entry, frees
    // the nodes (the buckets stay) and clears maxId. The player's maps of sent objects use it too.
    void ReleaseAll();
    // The constructor as the worlds inline it (world_construct.cpp): 100 buckets (193), maxId 0.
    void Construct();
};
static_assert(sizeof(ObjectIdMap) == 0x18);

// 0073F530: the vtable the game stores in its CTypedPtrList<CPtrList, CEngineObject*> locals
// (SendFreshObjects, world_map.cpp; world_zone.cpp).
inline void* ObjectPtrListVtable() { return reinterpret_cast<void*>(0x0073F530); }

struct PartyRecords;   // unit.h

// A player (one per human or AI side). Fields named only as far as used here.
struct WorldPlayer {
    std::uint8_t engineObject[0x1C];     // CPlayer's engine object fields (vtable, count, id, ...)
    netlink::NetLink* link;              // +0x01C the link to the player's client (CPlayer::link_), or null
    CPlayerServer* server;               // +0x020 the player's connection (player_messages.h), or null

    int id;                              // +0x024 player number (0 = the local player)
    ObjectIdMap objects;                 // +0x028 its objects by id (the briefing screen walks it)
    ObjectIdMap units;                   // +0x040 its units by id (CPlayer::units_ / maxUnitId_)
    std::uint8_t knownTimed[0x70 - 0x58]; // CPlayer::knownTimed_ / maxTimedId_ (read through CPlayer)
    // +0x070 the player's party (a CParty, party.h, 0x70 bytes to +0xE0: Party()); mercs,
    // mercCount and variables below are its members_ and variables_ seen from here. Loaded and
    // saved by 006618C0 / 00661C10 (world_save.cpp).
    std::uint8_t party[0xA0 - 0x70];
    std::uint8_t* mercs;                 // +0x0A0 0x784-byte records, unit id at +0x1C
    int mercCount;                       // +0x0A4
    std::uint8_t partyItems[0xC4 - 0xA8]; // the party's members_ tail, items_ and money (through Party())
    std::uint8_t variables[0xD8 - 0xC4]; // +0x0C4 named float variables ("gtime", quest flags)
    std::uint8_t partyZone[0xE0 - 0xD8];  // the party's zone_ and +0x6C (through Party())
    // +0x0E0 an item list (CPtrList); the HUD top panel's page 1 lists it (0064A7A0).
    mfc::CTypedPtrList<CEngineObject> items;
    // +0x0FC the key the party is stored under in the party manager (a fresh one from PartyNextId
    // for a new party; the client's login sends it); saved with the world.
    std::uint32_t partyId;
    // +0x100 saved with the world; CPlayer::sessionKind100_: set from the server world's +0x12D64
    // on joining (AcceptNewPlayers); 1: the flow applies the server's variables by stamp.
    std::uint32_t sessionKind;
    int lastScreen;                      // +0x104 the interface screen it comes from (5: the camp)
    // +0x108 the script's GodMode(player, on) (CPlayer::godMode_; the combat code tests it).
    // Saved with the world (one byte).
    std::uint8_t godMode;
    std::uint8_t unused_109[0x10C - 0x109];
    int logTraffic;                      // +0x10C logs the size of every flush (the console's "net")
    int lastTick;                        // +0x110
    mfc::CList<std::uint32_t> ids;       // +0x114 saved with the world from version 0x55 (AddTail 0059CC60)
    std::uint32_t dirty;                 // +0x130 changes still to send to this player

    class CParty* Party() { return reinterpret_cast<class CParty*>(party); }
    // The connection seen through its virtual slots (CPlayerConnection, player_messages.h).
    CPlayerConnection* Connection() { return reinterpret_cast<CPlayerConnection*>(server); }
};
static_assert(offsetof(WorldPlayer, link) == 0x1C);
static_assert(offsetof(WorldPlayer, party) == 0x70);
static_assert(offsetof(WorldPlayer, partyId) == 0xFC);
static_assert(offsetof(WorldPlayer, godMode) == 0x108);
static_assert(offsetof(WorldPlayer, logTraffic) == 0x10C);
static_assert(offsetof(WorldPlayer, lastTick) == 0x110);
static_assert(offsetof(WorldPlayer, ids) == 0x114);
static_assert(offsetof(WorldPlayer, server) == 0x20);
static_assert(offsetof(WorldPlayer, mercs) == 0xA0);
static_assert(offsetof(WorldPlayer, variables) == 0xC4);
static_assert(offsetof(WorldPlayer, items) == 0xE0);
static_assert(offsetof(WorldPlayer, objects) == 0x28);
static_assert(offsetof(WorldPlayer, units) == 0x40);
static_assert(offsetof(WorldPlayer, lastScreen) == 0x104);

// What CWorldClient::LocalView (005699F0) returns: the local view, +0x1C its player.
struct LocalViewData {
    std::uint8_t engineObject[0x1C];     // the CPlayerClient's engine object fields
    WorldPlayer* player;                 // +0x1C
    std::uint8_t clientState[0x84 - 0x20]; // the CPlayerClient's state, handlers and streams
    // +0x84 a local session (GameStartLocalSession sets it): a unit's network owner becomes the local
    // player whatever its id (UnitReadNetState), and the game-state message only presses Enter.
    std::uint8_t localSession;
    std::uint8_t sendParty;              // +0x85 CPlayerClient::sendParty_: the flow sets it once the local player took the requested party (the login then sends the party first), clears it for a new zone
};
static_assert(offsetof(LocalViewData, localSession) == 0x84);
static_assert(offsetof(LocalViewData, sendParty) == 0x85);
static_assert(offsetof(WorldPlayer, dirty) == 0x130);

// Walks the world's player list. The list object is reference counted (+4) while an iterator holds
// it; its MFC list head is at +0x38 and each node's data (+8) is a WorldPlayer.
struct PlayerIterator : ObjectIteratorOriginals {
    CEngineObject* list = nullptr;
    void* next = nullptr;

    WorldPlayer* First(CEngineObject* players) {   // 00404920
        return reinterpret_cast<WorldPlayer*>(ObjectIteratorOriginals::First(static_cast<CBaseManager*>(players)));
    }
    WorldPlayer* Next() { return reinterpret_cast<WorldPlayer*>(ObjectIteratorOriginals::Next()); }   // 00404960
    // The original inlines this: drop the list's count without releasing it.
    ~PlayerIterator() {
        if (list) --list->refCount_;
    }
};
static_assert(sizeof(PlayerIterator) == 8);

// Walks an object map's values (world_server.cpp; two byte-identical instantiations): the current
// node, then the map twice. First gives the first object or null, Next the one after it (null at
// the end and for an iterator already at its end).
struct ObjectMapIterator {
    stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>::Node* node;
    ObjectIdMap* table;
    ObjectIdMap* map;

    CEngineObject* First(ObjectIdMap* objects);   // 0059AF20 / 0059AFF0 (RET 4)
    CEngineObject* Next();                        // 0059AF80 / 0059B050
};
static_assert(sizeof(ObjectMapIterator) == 12);

// A zone exit as the world server keeps it (0x3C bytes); the client keeps the same records.
struct ZoneExit {
    // The record is the map database's MapExit (object_tables.h) as the world keeps it.
    std::uint32_t passTimeDeploy[5];      // +0x00 MapExit's passTime and deploy rectangle (saved as five dwords)
    float x1, y1, x2, y2;                 // +0x14 rectangle
    float angle;                          // +0x24 rotation about the rectangle's centre (radians)
    char* target;                         // +0x28 CString: target zone ("" = none)
    int entrance;                         // +0x2C entrance number in the target zone
    int view;                             // +0x30 MapExit::view (float bits)
    std::uint8_t hasDeployAngle;          // +0x34 saved from version 0x68
    std::uint8_t unused_35[3];
    int deployAngle;                      // +0x38 float bits; saved from version 0x68

    // 004E59F0 (exit_area.cpp). Whether (x, y) lies inside the exit area (rotated by `angle` about
    // the rectangle's centre when it is not 0).
    bool Contains(float x, float y) const;
    // 004E5750 / 004E58A0 (exit_area.cpp, RET 4). From / to a stream (core_streams.h): +0x00..+0x20
    // as dwords, the target with its terminator, +0x2C, +0x30, then +0x34 (a byte), +0x38 and the
    // angle; Read takes the last three only from version 0x68 on (else 0).
    void Read(class CStream* stream);
    void Write(class CStream* stream) const;
    // 004608B0. Scalar deleting destructor (flags & 1: free the record too); returns this.
    ZoneExit* DeletingDestructor(unsigned flags);
    // 00583B50 (RET 4; S13, exit_area.cpp; the compiler's operator=): +0x00..+0x24 as dwords,
    // the target (CString), +0x2C, +0x30, the byte +0x34 and +0x38; returns this.
    ZoneExit* Assign(const ZoneExit* from);
};
static_assert(sizeof(ZoneExit) == 0x3C);
static_assert(offsetof(ZoneExit, target) == 0x28);
static_assert(offsetof(ZoneExit, deployAngle) == 0x38);

// MFC CArray<ZoneExit> (CWorldServer + 0xEC; the map database's zone records hold one at +0x48).
// Its SetSize is the instantiation's own: ZoneExitsSetSizeOriginal (00573080, originals_world.h;
// new records are constructed).
using ZoneExitArray = mfc::CArray<ZoneExit>;

// The client's flying numbers (+0x1B4, 8 bytes; constructor 005E1710): the "numbers" texture
// (counted) and the list of the numbers shown.
class CFlyingHitPoints;     // ui_flying_hit_points.h

// A light colour of the client's tables (CWorldClient + 0x22C ...): r, g, b and 1.
struct LightColour {
    float r, g, b, a;
};
static_assert(sizeof(LightColour) == 0x10);

// Members both world classes share (base class, 0055E000-0055FFFF).
class CWorld : public CEngineObject {
public:
    // Virtual slots after CEngineObject's 13 (server vtable 0073F928, client 0073F41C). Slots
    // 13-24 are the manager context's getters (CParty has the same slots). Slots declared
    // `void Name()` keep their place in the order only (their return types are not declared; give
    // them their signature when calling them); rewritten slots stay plain members, rewrite guide
    // rule 4. Names and evidence: docs/notes/map_objects.md ("Virtual slot names"). Some names avoid
    // free helpers of
    // the same name (World, ItemManager, LightManager, ...) that the members' bodies call.
    virtual void ThisWorld();             // 13: 0055EB40 returns this
    virtual CBaseManager* TimedObjectManager();   // 14: 0055EB90 (+0x48, manager48_; the client's Segment ticks its objects)
    virtual CEngineObject* FigureManager();  // 15: the figure manager (+0x3C)
    virtual void ItemConstructor();       // 16: 0055EBB0 (+0x58)
    virtual void SpellConstructor();      // 17: 0055EBC0 (+0x50)
    virtual CEngineObject* ItemFactory(); // 18: 004BA000 (+0x54, itemManager_)
    virtual CEngineObject* MapObjectManager();   // 19: 0055EB70, the map object manager (+0x40)
    virtual CBaseManager* PlayerList();   // 20: 0055EB50 (+0x38, players_)
    // 21: 0055EB80 (+0x44): the side-specific halves' manager (the client's is a
    // CClientSpecificManager, 0042D690; it makes the class 0x45 objects of CFakeUnit's figures).
    virtual CBaseManager* SpecificManager();
    virtual CSpellManager* SpellManager();   // 22: 0055EBA0 (+0x4C)
    virtual CLinkFigureLoader* FigureLoader();   // 23 (vtable +0x5C): the figure loader (model.h)
    // 24: a copy of an item (client 0055E940 as CParty::CopyItem; server 00594CE0, CWorldServer::
    // CopyItem). Not called CopyItem here: CWorldServer's plain member of that name would hide it.
    virtual CEngineObject* DuplicateItem(CEngineObject* item);
    virtual void TexturePath();           // 25: client 00561890 (the CString +0x224, "textures\"), server 0
    virtual int IsServer();               // 26: 1 for the server (0058A380), 0 for the client (0055EB30)
    // 27: the terrain height at (x, y), in ST0 (RET 8): server CWorldServer::GridHeight 00594220, client 00563F70.
    virtual double GridHeightAt(float x, float y);
    // 28: the server's AI map height (0058A390), client 00564070; in ST0 (RET 8).
    virtual double RawSurfaceHeightAt(float x, float y);
    // 29 / 30: the ground height and the height of the water surface at (x, y), in ST0 (server
    // 0058A3D0 / 0058A3F0 call slots 27 / 28; client 005618E0 / 00561930 also clamp, 00560F00).
    virtual double GroundHeightAt(float x, float y);
    virtual double SurfaceHeightAt(float x, float y);
    // 31 (world_time.cpp). 0055FB30: the time of day becomes `hours` (the base +0x1C is moved so
    // that fmod(tick * secondsPerTick + base, 24) = hours). The server's 0058EEA0 also updates the
    // players' times first (game kinds 1 and 3), marks them dirty (4) and updates the weather.
    virtual void SetTime(float hours);
    void SetTimeImpl(float hours);   // body of the slot above (registered by member pointer)
    // 32 (world_time.cpp). 0055FB50: seconds per tick become 1 / speed (0: stopped; negative: the
    // setting 0079BE5C) times 1/15, keeping the time of day (slot 31). The server's 0058EEE0 as above.
    virtual void SetTimeSpeed(float speed);
    void SetTimeSpeedImpl(float speed);   // body of the slot above (registered by member pointer)
    virtual void* MaterialManager();      // 33: the material manager (0051C970 registers part materials)
    virtual CTextureManager* TextureManager();   // 34: the client's 00561990 (+0x18C)
    // 35: client 005619A0 (+0x190, a CLightManager), server 0. The client's Segment walks its objects.
    virtual CBaseManager* ClientLightManager();
    virtual CParticleManager* ClientParticleManager();   // 36: client 005619B0 (+0x19C, a CParticleManager), server 0
    virtual void ClientManager194();      // 37: client 005619C0 (+0x194), server 0. Not identified
    virtual void ClientManager198();      // 38: client 005619D0 (+0x198), server 0. Not identified
    virtual void Unused39();              // 39: 0055EC40 returns 0 in both
    // 40: the world object with this id, or null (server 00593AA0 FindObject, client 0055FAE0).
    virtual CEngineObject* ObjectWithId(int id);
    // 41: `object` moves to `position` (sector map, network state; net_state.cpp and
    // unit_client_path.cpp reach it through VCall).
    virtual void MoveObject(CEngineObject* object, const float* position);

    // 0055FC10 (world_time.cpp). Whether the hour of the day is before 6 or from 22 on (a NaN
    // counts as night).
    BOOL IsNight();
    // 0055FBD0 (RET 8; world_time.cpp; the world's save and the players' time message). The time
    // of day in hours and the time speed (1 / (seconds per tick * 15)), both stored as floats.
    void GetTime(float* hours, float* speed);
    // 0055FAE0 (RET 4; world_create.cpp; the client's slot 40). The map object manager's (slot 19)
    // object with this id, or null.
    CEngineObject* FindMapObject(int id);
    // 0055FCB0 (world_time.cpp). Stores the wind direction (+0x24) and strength (+0x30).
    void SetWind(const Vector3* direction, float strength);
    // 0055FB00 (RET 4; world_create.cpp). The world object with this id (slot 40) when it is a
    // unit (class 0x50), else null.
    CEngineObject* FindUnit(int id);
    // 0055FC50 (world_time.cpp; the units' sight, the weather, the HUD's unit panel). How dark the
    // time of day is, in ST0: 1 before 8 o'clock (and for a NaN), 0 until 20, then rising to 1 by
    // midnight ((hours - 20) / 4).
    double NightLevel();
    // 0055F8E0 (world_managers.cpp, A-U7). Slot 3 (Destroy) of the base vtable 0073F18C, also
    // called by the client 00563290 and the server 0058B020: every object of the spell manager
    // (slot 22) gets its slot 5 (ClearState), then the managers are deleted whatever their counts
    // (+0x38, +0x40, +0x3C, +0x44, +0x58, +0x54, +0x50, +0x4C, +0x48; +0x34 only at its last
    // reference) and the shared pointers +0x5C / +0x60 dropped.
    void DestroyManagers();
    // 0055F410 (world_sessions.cpp; the server's SetGameResult 2 / 3, the client's 00566D80). A new
    // empty network session (netlink::DriverSession) shared into +0x60, the previous one
    // destroyed (RefPtr::ShareAssign, inlined).
    void OpenSession();
    // 0055F890 (world_sessions.cpp; the server's TeardownZone, the client's SetPlayer). The session
    // +0x60 destroyed (RefPtr::Destroy, inlined) and the pointer cleared.
    void CloseSession();
    // 005942C0 (the server's, from the pause request 0x10) / 00566350 (the client's copy, from
    // message 0x60), byte-identical (world_server.cpp): stores paused_ (+0x6C).
    void SetPaused(std::uint8_t value);
    // 0055EAB0 (world_create.cpp; the two worlds' constructors 005612F0 / 00589ED0). The base
    // constructor: the engine object's fields, the base vtable 0073F18C, no figure loader, helper,
    // session or managers, the clock at 0 and the empty countdown array. Returns this.
    CWorld* ConstructBase();
    // 0055EC80 (S13a, world_destructors.cpp; also the end of the client's 00561B00 and the
    // server's 0058AAD0). The destructor's body: the base vtable, DestroyManagers, then the members
    // last to first (the countdown array's storage, the session and +0x5C as RefPtr::Destroy, the
    // figure loader's reference) and CEngineObject's body.
    void Destruct();
    // 0055EC60 (slot 0 of the base vtable): Destruct, then operator delete when bit 0 of `flags`
    // is set. Returns this.
    CWorld* DeletingDestruct(unsigned flags);

    static constexpr std::uintptr_t kBaseVtableAddress = std::uintptr_t(0x0073F18C);
    static constexpr std::uintptr_t kCountdownsVtable = std::uintptr_t(0x0073F234);

    float timeBase_;                   // +0x1C
    float secondsPerTick_;                // +0x20
    std::uint8_t windDirection_[12];      // +0x24 three floats; saved and loaded as they are
    std::uint32_t windStrength_;          // +0x30 float bits, 0..1; saved and loaded
    CLinkFigureLoader* figureLoader_;     // +0x34 counted (the client's from SetUpGraphics; slot 23 returns it)
    CEngineObject* players_;              // +0x38 player list (vtable slot 20 returns it)
    CEngineObject* manager3C_;            // +0x3C (slot 15)
    CEngineObject* manager40_;            // +0x40 map objects by id (slot 19; its slot 23 creates one)
    CEngineObject* manager44_;            // +0x44 (slot 21)
    CEngineObject* manager48_;            // +0x48 the timed objects (spells, effects; slot 14)
    CEngineObject* manager4C_;            // +0x4C (slot 22)
    CEngineObject* manager50_;            // +0x50 (slot 17)
    CEngineObject* itemManager_;          // +0x54 items (slot 18)
    CEngineObject* manager58_;            // +0x58 (slot 16)
    // +0x5C a counted helper (0xC bytes, vtable 0073F240) WorldCreateManagers shares in.
    RefPtr shared5C_;
    void* netSession_;                    // +0x60 the network session (a counted pointer; OpenSession)
    int tick_;                            // +0x64 game time in ticks
    int updateCount_;                     // +0x068 world updates run (the server's Update)
    // +0x06C paused: the server's Update skips the players and the tick while it is set; the
    // client's Segment skips its own ticks, and the client copies the server's flag from message
    // 0x60 (00566350), telling the HUD.
    std::uint8_t paused_;
    std::uint8_t unused_06D[0x70 - 0x6D];
    std::uint8_t countdowns_[0x84 - 0x70]; // +0x070 countdown list (006D43E0 sets it up, 006D44E0 ticks it; a CArray)
};
static_assert(offsetof(CWorld, shared5C_) == 0x5C);
// 0055EE40 (world_create.cpp; slot 2 of the base vtable 0073F18C, called by both worlds' Setup).
// The world's folders from the configured paths, the clock and wind cleared, the managers made,
// counted and attached (slot 13 with the world, or slot 2), the counted helper at +0x5C, the
// ISAAC generator seeded and the countdowns (+0x70) set up. Returns 1.
int __fastcall WorldCreateManagers(CWorld* world);
static_assert(offsetof(CWorld, updateCount_) == 0x68);
static_assert(offsetof(CWorld, paused_) == 0x6C);
static_assert(offsetof(CWorld, windDirection_) == 0x24);
static_assert(offsetof(CWorld, manager40_) == 0x40);
static_assert(offsetof(CWorld, itemManager_) == 0x54);
static_assert(offsetof(CWorld, players_) == 0x38);
static_assert(offsetof(CWorld, netSession_) == 0x60);
static_assert(offsetof(CWorld, tick_) == 0x64);

// One of the five alarms InvokeAlarm raises (CWorldServer + 0x8C, 16 bytes each; the script's
// IsAlarm / AlarmTime / AlarmPosX / AlarmPosY read them).
struct WorldAlarm {
    std::uint8_t active;                  // +0x00
    std::uint8_t unused_01[3];
    int time;                             // +0x04 tick it was raised
    float x, y;                           // +0x08
};
static_assert(sizeof(WorldAlarm) == 16);

// A missile in flight (0xA8 bytes, constructor 0067E510; created by 0058E670). Reference counted
// through a 16-bit count at +4. Not rewritten.
struct __declspec(novtable) Projectile {
    // Slot 0 (vtable set by the constructor): the scalar deleting destructor (flags 1: free it).
    virtual void* DeletingDestructor(unsigned flags);

    // 0067E6D0. Moves the missile one tick; 0 once it has hit or expired.
    int Update();

    // +0x04 the reference count (16 bits; LoadSave clears the whole dword of a new allocation before
    // its null test and the constructor).
    union {
        std::uint32_t references;
        std::int16_t referenceCount;
    };
};
static_assert(offsetof(Projectile, references) == 4);
static_assert(offsetof(Projectile, referenceCount) == 4);
// 007C84EC: the world the missiles find while they load (LoadSave sets it).
inline CWorldServer*& LoadingProjectileWorld() { return *reinterpret_cast<CWorldServer**>(0x007C84EC); }
// The world server's missiles: an MFC CList of counted pointers (CWorldServer + 0x12E28). Its
// FreeNode (0059C810, world_tick.cpp) releases the node's missile.
using ProjectileList = mfc::CTypedPtrList<Projectile>;

// The stream the world's save and load code reads and writes (the CMemoryStream holding
// mission.sav) is the engine's CStream (core_streams.h); version_ is the save format version.
using WorldStream = CStream;

// An element of the world server's shared-object list (CWorldServer + 0x12E60, 0x20 bytes;
// world_save.cpp): object id, player, a third dword and an MFC CArray of counted object pointers
// (vtable 0073FAC8, base 0073FABC; constructor 005985C0, destructor 0059A100 releases the objects
// through slot 4 and frees the data).
struct SharedObject {
    static void* ObjectsVtable() { return reinterpret_cast<void*>(0x0073FAC8); }
    static void* ObjectsBaseVtable() { return reinterpret_cast<void*>(0x0073FABC); }   // set by the destructor

    int objectId;
    int player;
    std::uint32_t money;                    // +0x08 gold the loot adds to the finder's party
    mfc::CArray<CEngineObject*> objects;    // +0x0C

    // 0059B510 (RET 4; S13, world_save.cpp; the compiler's copy constructor): the three dwords, then
    // the array by its Copy (0059B8F0, the objects counted). Returns this.
    SharedObject* CopyFrom(const SharedObject* from);
    // 0059DDE0 (RET 4; S13, world_save.cpp; the compiler's deleting destructor): the body 00594950
    // (an MFC row: the array's destructor 0059A100 at +0x0C, every object released), then operator
    // delete when bit 0 of the flags is set. Returns this.
    SharedObject* DeletingDestruct(unsigned flags);
    void Load(WorldStream* stream);           // 00589CA0 (RET 4)
};
static_assert(sizeof(SharedObject) == 0x20);
// MFC CList<SharedObject> (its NewNode 0059CA60 constructs the element).
using SharedObjectList = mfc::CList<SharedObject>;

// The game rules side (vtable 0073F928).
class CWorldServer : public CWorld {
public:
    // 00593AA0, vtable slot 40. World object with this id, or null.
    CEngineObject* FindObject(unsigned id);
    // 005927C0 (RET 4; world_units.cpp). Creates the object a creation message (+0x48 its kind)
    // describes: map objects and units (1, 5, 8, 9; 6 with a figure and maybe a campfire; 7) go
    // into the object maps, the update map and the sector map; sounds (3), lights (2) and kind 4
    // become timed objects. Returns the new id, -1 when refused, the message's address for an
    // unknown kind. Rebirth's FixAddMapObject takes index 0 for a unit without a prototype record.
    int CreateObject(std::uint8_t* message);
    // 005935C0. Removes object `id` from the server: out of the sector map and every id map, its
    // slot 5, flagged 1, the command queues, every object (slot 56), the missiles and the players'
    // states forget it, and the maps' reference is released. An unknown id crashes the original
    // (null read); Rebirth's FixRemoveMapObject ignores it.
    void RemoveObject(unsigned id);
    // 00594180. Player number `id`, or null.
    WorldPlayer* GetPlayer(int id);
    // 00594220, vtable slot 27. Ground height from the server's coarse height grid; 0 outside.
    float GridHeight(float x, float y);
    // 00591740. Living mercenaries of player `id`, not counting the leader (0 without the player).
    int CountLivingMercs(int id);
    // 005949D0. Whether object `objectId` is on the shared-object list for player `playerId` (or
    // for everyone). In multiplayer the player is looked up on the game server.
    bool IsObjectShared(int objectId, int playerId);
    // 00594A30 (RET 8: the player first). Fills `group` with the objects on that list for player
    // `playerId`.
    void CollectSharedObjects(int playerId, ScriptGroup* group);
    // 0058B6D0. ORs `mask` into every player's dirty flags.
    void MarkPlayersDirty(std::uint32_t mask);
    // world_time.cpp. 0058EEA0 / 0058EEE0: slots 31 / 32 (see CWorld).
    void SetTime(float hours) override;
    void SetTimeImpl(float hours);   // body of the slot above (registered by member pointer)
    void SetTimeSpeed(float speed) override;
    void SetTimeSpeedImpl(float speed);   // body of the slot above (registered by member pointer)
    // 0058EE20 (world_time.cpp; script and .mob weather). SetWind with the strength clamped to
    // 0..1 (NaN: 0), then the players marked dirty (8).
    void ChangeWind(const Vector3* direction, float strength);
    // 0058CC70 / 0058CC80 / 0058EE00. Setters of cave_, climate_ (any weather stops first) and
    // weather_ / weatherStrength_ (the players marked dirty).
    void SetCave(int value);
    void SetClimate(int value);
    void SetWeather(int weather, float strength);
    // 0058E4C0. Target zone and entrance of exit `index`; FALSE when out of range.
    BOOL GetZoneExit(int index, mfc::CString* target, int* entrance);
    // 0058E520. Registers an object that lives for a limited time (spells, effects): stamps the
    // current tick, adds a reference, marks it new and puts it in timedObjects_.
    void AddTimedObject(CEngineObject* object);
    // 0058E5D0. Flags object `id` deleted, releases it and removes it from timedObjects_.
    void RemoveTimedObject(unsigned id);
    // 0058D5D0. Puts a unit in the update map (+0x104) and sets its update slot.
    void AddToUpdateMap(CEngineObject* unit);
    // The updates an object stays in the update map before it may sleep (updateCountdown): 0x80
    // plus 0..15, which the game takes from bits 5..8 of the object's address (0058D5D0, 0058D3D0,
    // 0058D670, 005927C0, 00593FC0, 00596900), so it follows the heap's layout. Harness runs on the
    // virtual clock take the low 4 bits of the object's id instead, in the original code as well
    // (tests/verify/harness_heap.h), so that every run and both sides agree.
    static int UpdateCountdownFor(const CEngineObject* object);
    static void SpreadUpdateCountdownsById();

    // world_tick.cpp
    // 0058DCD0. One world tick: advances the tick count, runs every timed object (spells,
    // effects) and drops those that are done, advances the time wheel and the countdowns,
    // moves the missiles, runs the script command queues (the first only while +0x160 is set or
    // `force`) and then the units (0058D670).
    void Tick(char force);
    // 0058D670. Runs the objects: with +0x12D64 == 1 the units in the update map (+0x104), each
    // counting down to sleep (+0x174; out of the map when its controller is idle), else every map
    // object (+0x12DA0); then the counted objects (+0x12E44, dropped when their slot 6 returns 0).
    // Objects flagged 1 are removed (005935C0) at the end. Rebirth's FixSpellEffectsOverflow first
    // ticks the effects of sleeping objects (TickSleepingEffects).
    void UpdateUnits();
    // Rebirth's FixSpellEffectsOverflow #0 (no original address): counts the effects of sleeping
    // map objects down and wakes those with an expired one.
    void TickSleepingEffects();
    // 0058D3D0. Puts every unit in the sectors within `radius` of `position` (x, y) in the
    // update map.
    void WakeUnitsAround(const float* position, float radius);
    // 00593FC0. Raises alarm `index` (0..4) at `point` (x, y) unless it is already raised
    // (`force` raises it again; alarm 0 with `force` clears all five first), tells every AI unit
    // without a player, and puts those that react in the update map. Script InvokeAlarm.
    void InvokeAlarm(int index, const float* point, char force);
    // 0058DC90. Records how the game ended (+0x138, only the first time); results 2 and 3 also
    // replace +0x60 (0055F410) and call its slot 4.
    void SetGameResult(int result);
    // 0058D340 (RET 8; the alarm's remarks). Every player with `unit` among its party units
    // (CPlayerServer::IsPartyUnit) is sent the unit's message 0x0B with `value` (SendObject0B).
    void TellParties(CEngineObject* unit, std::uint32_t value);
    // 00593AF0 (RET 4; a unit's death becoming final). Every player with `unit` among its party
    // units is sent the unit's message 0x43 (SendObject43).
    void TellPartiesGone(CEngineObject* unit);
    // 00590160 (RET 8; CollectParty). The items the player holds (WorldPlayer::items) put into
    // `party` (CParty::AddItem(item, 1, 1)) and released, the list emptied. Nothing without a player.
    void StorePlayerItems(WorldPlayer* player, class CParty* party);
    // 00591070 (RET 4; world_quests.cpp; `this` is not read). After a player's party was sent again
    // (the multiplayer zone switch 0057EE10, ResendParty 0066F450): every item of the player's
    // party (+0x70) taken out (CParty::RemoveItem(first, 1)) until none is left.
    void ClearPlayerItems(WorldPlayer* player);
    // 00593FA0 (RET 0xC; world_small_calls.cpp; the script's SetWaterLevel). The water level `key`
    // moves to `target` over `steps` ticks (TweenSetTarget on +0x70).
    void SetWaterLevel(int key, float target, int steps);
    // 005939E0 (no caller in the exe; Rebirth's binaries refer to it). Every unit (class 0x50) of
    // the object map removed (RemoveObject by id; the walk steps on before each removal).
    void RemoveAllUnits();

    // world_zone.cpp
    // 005915E0 (RET 4). Replaces the zone exits (+0xEC) with copies of `source`'s records that
    // lead somewhere (target not ""); an exit without an entrance must lead to "none".
    void SetZoneExits(const ZoneExitArray* source);
    // 005910A0 (RET 0x10). Script AddUnitToServer: builds a unit record for prototype
    // `prototype` named `name` (006628D0), gives it the id `id` (the name's text key) and creates
    // it at `position` with the default rotation (0058EF20).
    void AddUnitToServer(unsigned id, const char* prototype, const char* name, const Vector3* position);
    // 00596030 (RET 8). Reads the objects of a .mob file (chunked, 00481660). Returns 1, or what
    // FatalError returns when the file cannot be opened or read.
    int LoadMapObjects(const char* file, int keepIds);
    // 00595AB0 (RET 4). Loads .mob file `file` once (the loaded names are kept at +0x124).
    int LoadMobFile(const char* file);
    // 00596C60. Removes every map object (+0x12DA0) from the server, releases the missiles and
    // the counted objects at +0x12E44 and clears the countdowns.
    void ClearMapObjects();
    // 00596730 (RET 4). Writes every map object to a save stream: id, prototype name, the
    // object's own Save (slot 10) and its two flag words; -1 ends the list.
    void SaveMapObjects(void* stream);

    // world_save.cpp
    // 00596E70 (RET 8). Restores the world from a mission save: zone names, settings, alarms and
    // clock, the zone itself (LoadMap with the saved AI-graph state), the managers' objects, the
    // map objects, timed objects, missiles, time wheel, script state, players, shared objects;
    // units that cannot stand where they were saved are moved to a free cell or removed. `map`
    // replaces the saved map name when not null. Returns 1, 0 when the AI graph cannot be read,
    // or what FatalError returns when the zone cannot be loaded.
    int LoadSave(WorldStream* stream, const char* map);
    // 00597BD0 (RET 4). Writes what LoadSave reads.
    void SaveWorld(WorldStream* stream);
    // 00596900 (RET 4). Reads the map objects SaveMapObjects wrote, registers them in the id maps
    // and wakes them.
    void LoadSavedMapObjects(WorldStream* stream);
    // 0058D280 (RET 4). Adds the time each player missed since its last update (plus `offset`) to
    // its "gtime" variable.
    void UpdatePlayerTimes(float offset);
    // 005947E0 (RET 0x10). Puts a new entry at the head of the shared-object list (+0x12E60):
    // `id`, the session player's +0x30 for that id in a multiplayer game (else -1), `value`, and
    // references to the objects of `objects` (a CPtrList). The second argument is unused.
    void AddSharedObject(int id, int unused, std::uint32_t value, void* objects);
    // 005956E0. Before a save: every living unit among the map objects (the walk stops at an
    // empty entry) refills its controller's slots (00525AF0 on +0x1E0) and runs the controller's
    // slot 19 with 0.
    void PrepareSave();
    // 00596140 (RET 0xC; world_game.cpp). Reads the zone's AI graph into the memory stream `out`:
    // the 0x31415926 chunk of the .mob file when there is one, else the whole <map>.grp file.
    // 1 when found.
    int ReadAIGraph(const char* map, const char* mob, void* out);
    // 00596350 (RET 4; world_game.cpp). The zone's scripts from the .mob file's 0xA000 chunk (its
    // 0xACCEECCB text packed and XORed, decoded by 004D8E10, or the plain 0xACCEECCA text) become
    // the only line of the first command queue.
    void LoadZoneScripts(const char* mob);
    // 0058DAF0 (RET 4; world_game.cpp). While a game runs (+0x138), every new connection the
    // lobby hands over (its slot 6) becomes a player (0066C180 / 00665990) of this game's kind,
    // fully dirty, starting at startPosition_; "Accepted player <id>" goes to the network log.
    void AcceptNewPlayers(void* lobby);

    // world_game.cpp
    // 0057AA80 (RET 4). Copy-constructs the zone name (+0x120) into `out`; returns out.
    mfc::CString* ZoneNameInto(mfc::CString* out);

    // world_map.cpp
    // 00595C70 (RET 0x10). Loads map `map` (when it changed; CAIMap 005B43C0) and the AI layer
    // state from `graph`, then the zone: the same .mob again starts it over (queues, diplomacy,
    // map objects, missiles and timed objects cleared), another one first empties the
    // world (ResetZone 0058B250); every player is marked dirty; the .mob objects are loaded (fresh
    // zones start, 005951A0). From a save the loaded objects are cleared again (the save holds
    // them). Returns 1, 0 when the map or the objects cannot be loaded.
    int LoadMap(const char* map, const char* mob, CStream* graph, char fromSave);
    // 0058B250. Empties the world of the current zone: shared objects, .mob files, countdowns,
    // exits, command queues, missiles, counted objects, the players' zone data, every object
    // (slot 5 then the maps), timed objects, the zone names and three managers.
    void ResetZone();
    // 0058B600. ResetZone, then the map name, diplomacy, both command queues and the AI map are
    // reset and the game state cleared: +0x15C, the day-time value, alarms, the kind, the start
    // position, the day factor (1), loaded_ and +0x13C.
    void ResetState();
    // world_construct.cpp
    // 00589ED0 (the static initialiser 00589BE0). The constructor: the base (ConstructBase), the
    // members' constructors in order (the zone exits, the update map, the strings and .mob list, the
    // messages registry, the AI map, both scripts, the diplomacy, the id maps, the zone header (a
    // queue stream), the three lists), the vtable 0073F928, then ResetState. Returns this.
    CWorldServer* Construct();
    // world_destructors.cpp
    // 0058AAD0 (the global's exit function 00589C00). The vtable 0073F928, Teardown, then the members
    // last to first: the lists, the zone header, the id maps, the strings, the diplomacy (its Reset),
    // both scripts (the second's destructor inlined), the AI map, the messages registry, the .mob
    // list, the zone name, the update map, the zone exits, then the base body (CWorld::Destruct).
    void Destruct();
    // 0058A430 (slot 0, RET 4). Destruct, then operator delete when bit 0 of `flags` is set.
    CWorldServer* DeletingDestruct(unsigned flags);

    // world_server_life.cpp
    // 0058AE10, vtable slot 2 (Create; called by WinMain). Teardown (slot 3) first, then the
    // managers (WorldCreateManagers), the messages registry (text folder + "SMessBase.reg"), the
    // side-specific manager (+0x44, a CServerSpecificManager given this world as context), the
    // CHoover (+0x140), the wind (1, 0, 0; strength 0.4) and the zone state cleared. Returns 1, or
    // FatalError's result when the managers or the registry fail.
    int Setup();
    // 0058B020, vtable slot 3 (Destroy; also WinMain at exit and the destructor 0058AAD0). Every
    // player disconnected and released, the CHoover deleted, both script queues reset, ResetState,
    // the messages registry closed, the zone exits freed, then the base's DestroyManagers.
    void Teardown();
    // 005957E0. Before every game state request: every player leaves, the shared objects, .mob
    // files, script queues, diplomacy, map objects, missiles and timed objects go, then the
    // base class's teardown (0055F890); the game result and kind are cleared.
    void TeardownZone();
    // 00595480 (RET 0xC; world_save.cpp; patched by Rebirth, so vanilla only; A-W14). A new zone
    // from the map `map` and the object file `mob`: the loading movie picked for `zone` and its
    // first frame shown; 0 when a file is neither where its name says nor in the maps folder
    // (null names are not looked for). The AI graph is read (ReadAIGraph; without it a fatal
    // error, whose result is returned), the .mob list and the alarms are emptied and the map
    // loaded between the AI map's BeginRefresh and EndRefresh (LoadMap; a failure is a fatal
    // error too). The zone name is kept when given, +0x160 cleared. Returns 1.
    int NewZone(const char* map, const char* mob, const char* zone);
    // 00595B50 (RET 8; world_map.cpp; the client's state block 00567AB0 through the glue 00578730).
    // Unless the zone's map and .mob are these already: NewZone(map, mob, null), then TeardownZone.
    void EnsureZone(const char* map, const char* mob);
    // 00595020 (RET 8; world_party.cpp; `this` is not read). `unit` used one of `item` (a scroll):
    // the item's count drops, and so does the count of the party's item it was made from (the
    // link at +0x1C) when the unit's player has a record for the unit.
    void UseItemUp(class CUnit* unit, class CItem* item);
    // The server's height slots (world_server.cpp; ST0, RET 8). 0058A390 (slot 28): the AI map's
    // top height at (x, y, 0). 0058A3D0 / 0058A3F0 (slots 29 / 30): slot 27 / slot 28 as they are.
    double ServerRawSurfaceHeight(float x, float y);
    double ServerGroundHeight(float x, float y);
    double ServerSurfaceHeight(float x, float y);
    // 005951A0. After a fresh zone is loaded: the map objects that are not part of the map
    // (slot 29) move from the map-object maps to +0x12D88 and are sent, with the zone header
    // (+0x12E00, reset first), through a temporary player.
    void SendFreshObjects();

    // world_update.cpp
    // 0058DFF0, vtable slot 6. One world update: drops disconnected players and lets the others'
    // units wake what they hear, accepts new players, runs the players and the world tick (unless
    // +0x6C), rebuilds the local players' "Heroes" group, synchronises every player (SyncPlayer;
    // 0 drops it) and counts the update (+0x68). Returns 1.
    int Update();

    // world_spells.cpp
    // 0058E850 (RET 0x18). Casts `spell` from `caster` (or from `from`): one missile at `target` /
    // `to`, or, for a spell with several targets (+0x38) or area flags, at the nearest living units
    // within its radius (+0x34) the caster may hit (its heroes group, friends or diplomacy), the
    // requested target first. Network clients cast nothing with a caster.
    void CastSpell(const void* spell, CEngineObject* caster, const Vector3* from, CEngineObject* target,
                   const Vector3* to, int extra);
    // 0058E670 (RET 0x20). One missile of `spell` from the caster (or `from`) to the target (or
    // `to`, on the ground there); kept on the missile list unless its first update ends it. A
    // hostile spell turns the target unit against the caster.
    void CreateMissile(const void* spell, CEngineObject* caster, Vector3 from, CEngineObject* target, const Vector3* to,
                       int extra);

    // world_loot.cpp
    // 005942D0 (RET 8, AL). `player` takes what `object` holds: a shared loot record
    // (flags_ 0x20000000, removed afterwards) or what 00461CB0 collects (in a multiplayer client
    // game quest items go to player 0); the gold is added to the player. False for no player, a
    // living owned unit, or nothing to take.
    bool Loot(CEngineObject* object, WorldPlayer* player);
    // 00593D60. The local player (id 0) loots every dead unit, which is then removed (both
    // through the global server 007AFF60). Nothing without a local player.
    void LootDeadUnits();

    // world_weather.cpp
    // 0058CCD0. The weather, every 64 ticks: the day-time value (+0x84); in single player rain
    // starting / stopping (+0xE4 / +0xE8, 007C2DFC delay), lightning strikes in storms (+0xDC 3),
    // bolts in the sky while it rains; the day factor (+0x88); every object's slot 57; the
    // players' "gtime".
    void UpdateWeather();

    // world_quests.cpp
    // 00591800. For every player and the ten mercenary slots ("merc1".."merc10"), sets the
    // briefing variables b.mercN.n1/n2/n3/n8/n9/n10/n11_N (and clears amerc_N) from whether the
    // mercenary is in the party, was hired (amerc_N), and how many others are alive.
    void UpdateMercBriefings();
    // 00592090 (RET 8). Briefing `name` ("a.b.<record>") is complete for player `playerId`: a
    // network client asks the server; otherwise the player gets the record's experience, gold
    // and items, loses its quest items, its variables are set, and the scripts get
    // GSSetVar / #OnBriefingComplete (run at once in kind-3 games).
    void CompleteBriefing(int playerId, const char* name);
    // 00591DE0 (RET 8). The mercenary side of a completed briefing `name`: for the ten slots,
    // b.mercN.n8_N clears that variable, n10_N removes the mercenary from the party and the
    // world, n2_N from the party, n1_N / n3_N hire it (amerc_N = 2, added to the party with its
    // unit's prototype or "Human Hero"); then UpdateMercBriefings.
    void ApplyMercBriefing(WorldPlayer* player, const char* name);
    // 005911C0 (RET 8). Gives `amount` experience: with a player, shared equally by its living
    // heroes (units and party records); without one, shared by the players that have a single
    // hero, in proportion to sqrt(+0x8E0 + +0x1E4) of each hero's controller. Zero (or NaN) does
    // nothing.
    void AwardExperience(WorldPlayer* player, float amount);
    // 00590C70 (RET 4). Brings the player's party records (+0x70) up to date with its living
    // units: the player's own items, then every record's quick, worn and belt items and spells
    // rebuilt from the unit (each item's +0x1C record pointing at its entry) and the statistics.
    void SyncPartyRecords(WorldPlayer* player);
    // 0058FD90 (RET 0xC). Before a zone change: every record of `records` (CArray of record
    // pointers) notes its unit's leader, gets a placement (if `placements`), is rebuilt from its
    // unit through `partyRecords`, and the unit is removed (a dead one just loses its owner).
    void StoreParty(void* records, void* partyRecords, void* placements);
    // 00590A40 (RET 8). The player's party leaves the zone: gtime, its items, then (single player
    // and kind 3, with +0x15C) its records without a unit are dropped and the rest stored
    // (StoreParty).
    void CollectParty(WorldPlayer* player, void* placements);

    // world_party.cpp
    // 005901E0 (RET 8). Moves quest items (class 0x14) from `itemArray` (a CArray of item
    // pointers) to `list` (CPtrList): in single player only those whose description names this
    // zone or whose id an object of the zone asks for (object slots 38/40); otherwise all.
    void TakeZoneQuestItems(void* itemArray, void* list);
    // 00590460 (RET 4). Gives the player fresh copies of the items it brings into the zone (all
    // of them on a client, the zone's quest items otherwise; nothing in a network game).
    void RestorePlayerItems(WorldPlayer* player);
    // 00590700 (RET 0xC). The party arrives in the zone (single player and kind 3, with +0x15C):
    // its units are created at the entrance's centre (the target zone's exit), facing away from
    // the map centre or along the exit's heading, the player state gets the start; `runScripts`
    // runs the script command queue five times.
    void Redeploy(WorldPlayer* player, void* placements, char runScripts);
    // 00594CE0 (slot 24, RET 4). A new item like `item`: made by the item factory (slot 18) from
    // its type (+0x10) and record (+0x18), filled by the item's slot 15 (with slot 22), with its
    // count (+0x28); one below its maximum (+0x2C) goes on counted_ once.
    CEngineObject* CopyItem(CEngineObject* item);
    // 00594F30 (RET 4). Puts `item` (an item whose state changed) on counted_, once; null is
    // ignored.
    void AddCounted(CEngineObject* item);
    // 00594AB0 (RET 4). The item manager's (007AFFB4) new item named `name` (006AC970).
    CEngineObject* CreateItemNamed(const char* name);
    // 00594BC0 (RET 4). The item manager's new spell named `name` (006AD1E0, with slot 22).
    CEngineObject* CreateSpellNamed(const char* name);

    // world_units.cpp
    // 0058EF20 (RET 0x20). Creates a unit for each record of `records` (a CArray of 0x784-byte unit
    // records; an existing object with the record's id is removed first), at its entry in
    // `placements` (CArray of 0x28-byte {id, position, rotation, two values}) or in a line from
    // `position` along `rotation`, moved to a free cell, under `player`'s control, with its items,
    // spells and AI; heroes get their figure parts. Followers are given their leaders; with a
    // player the units are added to the "Heroes" script group.
    void CreateUnits(void* player, void* records, const Vector3* position, Quaternion rotation, void* placements);
    // 0058ECE0 (RET 8). A quick item (potion) is used by `unit`: the effect object "st.tr.130"
    // (model "unhuma", type 9, flags 0x3010) is created at the unit's position on the ground and
    // flagged new (00529D20: flags_ |= 0x180000). `item` is not used.
    void UseQuickItem(void* item, CEngineObject* unit);

    // world_sync.cpp
    // 0058B750 (RET 4). Brings `player`'s view up to date (once per world update): the zone when
    // it changed (SendZone), weather, time, wind and countdowns when dirty, then the map objects
    // and timed objects it should see compared with what it was sent. Returns what the player
    // state's flush returns (0 drops the player), 1 for a player that is leaving.
    int SyncPlayer(WorldPlayer* player);
    // 0058C890 (RET 4). Sends `player` the zone to load (tick, map and .mob names, exits, the
    // zone's units) and, for a new game, where it starts. 0 when the player state refused it.
    int SendZone(WorldPlayer* player);

    // +0x084 WorldDayTime's value (0058CCD0; float bits): the script's IsNight reads it, the units'
    // movement factors scale by it.
    std::uint32_t nightLevel_;
    std::uint32_t weatherFactor_;         // +0x088 float bits: 1 dry, 0.9 in rain or snow (the units' fourth movement factor)
    WorldAlarm alarms_[5];                // +0x08C
    // +0x0DC the zone's weather kind (its record's setting2C): 3 storms with lightning strikes,
    // 1 light rain, other non-zero values heavier weather; 0 none. Single-player only.
    int climate_;
    int cave_;                            // +0x0E0 the zone is a cave (its record's setting30; sent with the zone)
    int weather_;                         // +0x0E4 0 clear, 1 rain, 2 snow (the script's SetWeather)
    float weatherStrength_;               // +0x0E8 sent with the weather
    void* exitsVtable_;                   // +0x0EC CArray<ZoneExit> (ZoneExitArray) from here
    ZoneExit* exits_;                     // +0x0F0
    int exitCount_;                       // +0x0F4
    int exitCapacity_;                    // +0x0F8
    int exitGrowBy_;                      // +0x0FC
    std::uint8_t loaded_;                 // +0x100 0 while LoadSave runs
    std::uint8_t unused_101[0x104 - 0x101];
    ObjectIdMap updateMap_;               // +0x104
    // +0x11C set: SendZone also sends the player its start (at its first unit). Cleared by the
    // reset; nothing in the game sets it.
    std::uint8_t sendsStartPosition_;
    // +0x11D / +0x11E (LoadMap, world_map.cpp: LoadedFresh / KeepsState): CreateObject (005927C0)
    // makes objects answering map object slot 29 (IsSynchronised), sounds, lights and particle
    // sources only with loadedFresh_ set, the other map objects only with keepsState_ set; a refused
    // object is released.
    std::uint8_t loadedFresh_;            // +0x11D the zone is not loaded from a save
    std::uint8_t keepsState_;             // +0x11E
    std::uint8_t unused_11F;
    char* zoneName_;                      // +0x120 CString: the zone's map database name
    mfc::CStringArray mobFiles_;           // +0x124 .mob files loaded in this zone (LoadMobFile)
    int gameResult_;                      // +0x138 0 while the game runs
    std::uint32_t lastRainBoltTick_;      // +0x13C the tick of the last bolt in the rain (one per 20 ticks); saved
    CHoover* hoover_;                     // +0x140 a CHoover (object.h), counted; made by Setup, deleted by Teardown
    // +0x144 a CFileRegistry (registry.h; closed by 0058B0E7) with the server's messages: the
    // login handler (0066F890) reads "SrvErrMsg" / "WrongClientVersion" from it.
    std::uint8_t messages_[0x15C - 0x144];
    CFileRegistry* Messages() { return reinterpret_cast<CFileRegistry*>(messages_); }
    int zoneLoaded_;                      // +0x15C set by LoadMap, cleared by the teardown and a .mob file that fails to load
    int scriptsActive_;                   // +0x160 non-zero: Tick runs the zone's script (set by Redeploy)
    Vector3 startPosition_;               // +0x164 where joining players start (0058DAF0)
    std::uint8_t aiMap_[0x290 - 0x170];   // +0x170 CAIMap (ai_map.h; the global 007B00D0); extent unknown
    CAIMap* AIMap() { return reinterpret_cast<CAIMap*>(aiMap_); }
    float gridHeightScale_;               // +0x290
    CSectorMap* Sectors() { return reinterpret_cast<CSectorMap*>(sectorMap_); }
    std::uint8_t sectorMap_[0x2468 - 0x294]; // +0x294 CSectorMap (sector_map.h; size unknown)
    // +0x2468 the AI layer state SaveWorld saves (005B1380 into a memory stream); its extent and
    // whether it belongs to the sector map are unknown.
    std::uint8_t aiLayer_[0x2804 - 0x2468];
    // +0x2804 a world tick: an idle unit standing in a kind-3 occluder replans while its motion order
    // started before it (the movement tick 00547890). Part of the sector map or not: unknown.
    int occluderReplanTime_;
    std::uint8_t unused_2808[0x280C - 0x2808];
    std::uint8_t** gridRows_;             // +0x280C rows of 9-byte cells, height (u16) at +4
    int gridWidth_;                       // +0x2810
    int gridHeight_;                      // +0x2814
    std::uint8_t unused_2818[0x2838 - 0x2818];
    std::uint8_t timeWheel_[0x12874 - 0x2838]; // +0x2838 the AI events (AIEventTable, unit_ai.h: time wheel, grid)
    AIEventTable* AIEvents() { return reinterpret_cast<AIEventTable*>(timeWheel_); }
    std::uint8_t commandsA_[0x12A6C - 0x12874]; // +0x12874 script command queue (004BF000)
    // commandsA_ is the zone's CScript (script.h, 0x1F8 bytes): its globals_ hold the "Heroes" group.
    CScript* MainScript() { return reinterpret_cast<CScript*>(commandsA_); }
    std::uint8_t commandsB_[0x12C64 - 0x12A6C]; // +0x12A6C script command queue (004BF000)
    CScript* SecondScript() { return reinterpret_cast<CScript*>(commandsB_); }
    CDiplomacy diplomacy_;                // +0x12C64 (object_diplomacy.h)
    // +0x12D64 the session kind: 1 single player, 3 no range test, 4 network; 1 or 3: SaveWorld
    // updates the players' "gtime" first. Joining players take it (WorldPlayer::sessionKind).
    int sessionKind_;
    char* mobName_;                       // +0x12D68 CString: the zone's .mob file
    char* mapName_;                       // +0x12D6C CString: the zone's map
    ObjectIdMap objects_;                 // +0x12D70 every world object by id
    // +0x12D88 the map objects SendFreshObjects sent (not part of the map itself, slot 29).
    ObjectIdMap sentObjects_;
    ObjectIdMap mapObjects_;              // +0x12DA0 objects saved with the zone (SaveMapObjects)
    ObjectIdMap otherMapObjects_;         // +0x12DB8 the map objects that are not units
    ObjectIdMap namedObjects_;            // +0x12DD0 objects with a second name (+0x1A8)
    ObjectIdMap timedObjects_;            // +0x12DE8
    // +0x12E00 the zone header SendFreshObjects sends to a temporary player (constructed by
    // 0041DD70 in the constructor 00589ED0).
    CMemoryStream zoneHeader_;
    ProjectileList projectiles_;          // +0x12E28
    mfc::CTypedPtrList<CEngineObject> counted_; // +0x12E44 counted references (released on clear)
    SharedObjectList sharedObjects_;      // +0x12E60 (world_save.cpp; AddSharedObject, IsObjectShared)
};
static_assert(offsetof(CWorldServer, countdowns_) == 0x70);
static_assert(offsetof(CWorldServer, nightLevel_) == 0x84);
static_assert(offsetof(CWorldServer, loaded_) == 0x100);
static_assert(offsetof(CWorldServer, zoneName_) == 0x120);
static_assert(offsetof(CWorldServer, hoover_) == 0x140);
static_assert(offsetof(CWorldServer, loadedFresh_) == 0x11D);
static_assert(offsetof(CWorldServer, keepsState_) == 0x11E);
static_assert(offsetof(CWorldServer, lastRainBoltTick_) == 0x13C);
static_assert(offsetof(CWorldServer, aiMap_) == 0x170);
static_assert(offsetof(CWorldServer, diplomacy_) == 0x12C64);
static_assert(offsetof(CWorldServer, sessionKind_) == 0x12D64);
static_assert(offsetof(CWorldServer, mapName_) == 0x12D6C);
static_assert(offsetof(CWorldServer, sentObjects_) == 0x12D88);
static_assert(offsetof(CWorldServer, zoneHeader_) == 0x12E00);
static_assert(offsetof(CWorldServer, otherMapObjects_) == 0x12DB8);
static_assert(offsetof(CWorldServer, namedObjects_) == 0x12DD0);
static_assert(offsetof(CWorldServer, alarms_) == 0x8C);
static_assert(offsetof(CWorldServer, climate_) == 0xDC);
static_assert(offsetof(CWorldServer, exits_) == 0xF0);
static_assert(offsetof(CWorldServer, gameResult_) == 0x138);
static_assert(offsetof(CWorldServer, scriptsActive_) == 0x160);
static_assert(offsetof(CWorldServer, sectorMap_) == 0x294);
static_assert(offsetof(CWorldServer, aiLayer_) == 0x2468);
static_assert(offsetof(CWorldServer, occluderReplanTime_) == 0x2804);
static_assert(offsetof(CWorldServer, timeWheel_) == 0x2838);
static_assert(offsetof(CWorldServer, commandsA_) == 0x12874);
static_assert(offsetof(CWorldServer, commandsB_) == 0x12A6C);
static_assert(offsetof(CWorldServer, projectiles_) == 0x12E28);
static_assert(offsetof(CWorldServer, updateMap_) == 0x104);
static_assert(offsetof(CWorldServer, gridHeightScale_) == 0x290);
static_assert(offsetof(CWorldServer, gridRows_) == 0x280C);
static_assert(offsetof(CWorldServer, objects_) == 0x12D70);
static_assert(offsetof(CWorldServer, timedObjects_) == 0x12DE8);
static_assert(offsetof(CWorldServer, sharedObjects_) == 0x12E60);
static_assert(offsetof(CWorldServer, exitsVtable_) == 0xEC);
static_assert(offsetof(CWorldServer, mobFiles_) == 0x124);
static_assert(offsetof(CWorldServer, zoneLoaded_) == 0x15C);
static_assert(offsetof(CWorldServer, mapObjects_) == 0x12DA0);
static_assert(offsetof(CWorldServer, counted_) == 0x12E44);

// One walkable platform (bridge, deck) the client tests positions against. 0x4C bytes, the
// element of the platform set's MFC CList (node: next, prev, then the record).
struct Platform {
    int id;                               // +0x00 the object it belongs to (RemoveObject drops its faces)
    float x, y;                           // +0x04 centre of the bounding circle
    float z;                              // +0x0C
    float radius;                         // +0x10
    Vector3 origin;                       // +0x14
    Vector3 axisU;                        // +0x20 local u axis
    Vector3 axisV;                        // +0x2C local v axis
    Vector3 normal;                       // +0x38
    float sizeU, sizeV;                   // +0x44 extent along u and v
};
static_assert(sizeof(Platform) == 0x4C);

using PlatformList = mfc::CList<Platform>;
using PlatformNode = PlatformList::Node;
static_assert(sizeof(PlatformNode) == 0x54);   // 00573000 carves nodes of 0x54 bytes

// The client world's platform set (CWorldClient + 0x1D4, 0x38 bytes): an engine object (its
// vtable at +0; Setup calls slot 2, Teardown slot 3) and the CList of the platform faces at +0x1C
// (AddFace adds at the head; the list's own NewNode 00573000, RemoveAt 0056FDD0, RemoveAll
// 0056FDA0).
struct PlatformSet {
    // 00560F00. max(height, z of every platform surface whose area contains the point); the
    // point is projected onto each platform plane along z.
    float SurfaceHeight(const Vector3* point, float height);
    // 00561050. Whether (x, y) lies on some platform (with a 0.1 margin).
    bool Contains(float x, float y);
    // 00561150 (RET 0x10; the particles' ground plane, CParticleSource::TickGroundPlane, through
    // 007AF78C). The highest platform surface above 0 at (x, y) (with a 0.5 margin): *height its
    // z and *normal its normal; 0 and (0, 0, 1) when there is none.
    void SurfacePlane(float x, float y, Vector3* normal, float* height);
    // 00560650 (RET 0x14; world_client_maps.cpp). Adds the face with corner `a`, edges to `b` and
    // `c` and `normal` when normal.z >= 0.8: origin a, unit axes (b - a) / |b - a| and (c - a) /
    // |c - a|, the two lengths, the normal; *out = the new record (its id, centre and radius are the
    // caller's), null when the face is too steep. Returns 1 when added.
    int AddFace(const Vector3* a, const Vector3* b, const Vector3* c, const Vector3* normal, Platform** out);
    // 00560820 (RET 0x10; called by CWorldClient::AddObject). The platform item `item` (+4
    // offset, +0x10 radius, +0x14 / +0x20 the box's corners, +0x2C pivot) of object `id` placed at
    // `position` with the rotation quaternion `rotation` (w, x, y, z): the box's corners and the six
    // face normals rotated, and every face that points up added with the id, the centre
    // R (pivot + offset) + position and the radius.
    void AddBox(int id, const FootprintPart* item, const Vector3* position, const Quaternion* rotation);

    // 00561800 (slot 2, the engine object's Create; the client's Setup). The face list emptied (its
    // blocks freed); returns 1. The set is the engine's CBridgeManager (vtable 0073F4FC, class 0x34).
    int Clear();
    // 00561A60. The destructor's body: the face list emptied (its vtable 0073F548 back), then the
    // engine object's body.
    void DestructBody();
    // 00561AE0 (slot 0, RET 4): DestructBody, then operator delete when flags bit 0 is set.
    PlatformSet* DeletingDestruct(unsigned flags);

    // The set as the engine object it starts with (its vtable is not declared here).
    CEngineObject* Engine() { return reinterpret_cast<CEngineObject*>(this); }

    std::uint8_t engineObject_[0x1C];     // +0x00 CEngineObject
    PlatformList list;                    // +0x1C the faces, the newest first
};
static_assert(offsetof(PlatformSet, list) == 0x1C);
static_assert(sizeof(PlatformSet) == 0x38);

// The unit set the server's SendZone fills (world_sync.cpp; constructor 005985E0, destructor
// 005986B0, 005593D0 adds a unit) and the client reads back as its unit looks (unit_looks.h): the
// figured records grouped by prototype (each group an STLport hash_map keyed by three packed
// 10-bit values), and the dressed records by prototype. All three maps hash a key to itself
// (0055C5C0 and the iterators' 0055B9D0 / 0055B500). Only the writer is rewritten
// (world_unit_set.cpp).
using FiguredUnitMap = stlp::hash_map<unsigned, FiguredUnitRecord, stlp::IntHash>;
struct UnitSet {
    // 00559E60 (RET 4). Writes the set to `stream` (its slot 2 writes bytes): the number of figured
    // records; per record its head (00558FA0), the morph coefficients, its name count and each
    // name with its terminator; the number of dressed records and per record a 16-bit prototype,
    // two counted dword arrays, a byte and a dword.
    void Write(CStream* stream);

    stlp::hash_map<unsigned, FiguredUnitMap, stlp::IntHash> figured;   // +0x00
    stlp::hash_map<unsigned, DressedUnitRecord, stlp::IntHash> dressed; // +0x14
};
static_assert(offsetof(UnitSet, dressed) == 0x14);
static_assert(sizeof(UnitSet) == 0x28);

// 0055A190 (ECX stream, EDX first list, RET 4; called by the client's zone load 00567AB0). Reads
// the zone's two unit look lists from `stream` (slot 1 reads bytes): a count, then per figured
// record a 16-bit prototype, two counted dword arrays, a byte, a dword, the three morph
// coefficients and a counted list of names; then a count and per dressed record the prototype,
// the two arrays, the byte and a dword. Both vectors are resized to the counts.
void __fastcall ReadUnitLooks(CStream* stream, stlp::vector<FiguredUnitRecord>* figured,
                              stlp::vector<DressedUnitRecord>* dressed);

// The client's sector grids (CWorldClient +0x120 for the objects, +0x150 the second one;
// world_client_maps.cpp): an STLport list of counted pointers per 16x16 cell.
using SectorList = stlp::list<CEngineObject*>;
using SectorListNode = SectorList::Node;
// 00572B30 / 00572C50 (RET 4; S13, world_client_maps.cpp): the cells' vector deleting
// destructor. Bit 1 of `flags`: `cells` is a new[] block (its count in the dword before it), every
// list destroyed from the last; else the one list. Bit 0: the memory freed. Returns the block.
SectorList* __fastcall SectorListsDelete(SectorList* cells, void*, unsigned flags);
// A cell's list destroyed (world_client_maps.cpp; STLport's list<counted pointer>::~list, 0056DEF0
// / 00572E90, inlined by the client's destructor for its heroes): each object released (slot 4)
// and its node returned to the pool, the sentinel last.
void DestroySectorList(SectorList* list);
// 0056DEF0 (world_client_maps.cpp, S3c): the same at its address, which only the exit function
// 0056A4C0 calls (on the deferred unit list 007AF5B4).
void __fastcall SectorListDestruct(SectorList* list);
struct SectorGrid {
    SectorList* cells_;                   // +0x00 the lists
    SectorList** rows_;                   // +0x04 row y: its lists
    int unknown08_, unknown0C_;           // +0x08 (00572450 resizes when they change)
    std::uint32_t width_;                 // +0x10 cells along x
    std::uint32_t height_;                // +0x14 cells along y
    // 00572820 / 005728D0 (the two list instantiations; world_client_maps.cpp). The storage for
    // unknown08_ x unknown0C_ cells: a new[] block of empty lists (the count before them), then the
    // row pointers.
    void Allocate();
    // 00572450 / 005724F0 (RET 8). When the storage has another size: the lists destroyed and the
    // storage freed (SectorListsDelete, flags 3), the size taken, then Allocate.
    void Resize(int width, int height);
    // 00570860 (RET 8; CWorldClient slot 41 on +0x120, also 0067BEF0 / 0067CEF0). Moves `object`
    // to `position` (stored at object +0x1C): when the cell (FISTP of x and y, >> 4) changes, out
    // of the old cell's list and to the end of the new one's (cells outside the grid are skipped).
    void Move(CEngineObject* object, const float* position);
    // 00570BB0 (RET 8; slot 42 on +0x150). The same for the second grid's list instantiation.
    void Move2(CEngineObject* object, const float* position);
};
static_assert(offsetof(SectorGrid, width_) == 0x10);

// 00571D50 (thiscall on the returned functor, RET 0x14). for_each over list nodes [first, last):
// sets the byte at node +0xD when hypot(node +0x10 - x, node +0x14 - y) <= node +0x94 * scale.
// Writes {x, y, scale} to out and returns it.
float* __fastcall MarkWithinRadius(float* out, void*, std::uint8_t* first, std::uint8_t* last, float x, float y,
                                   float scale);
// 00571800 / 005718B0 (thiscall on the returned functor, RET 0x18). for_each over list nodes
// [first, last) of 0x18-byte entries (+4 object id, +8 RECT): every entry whose rectangle holds
// (x, y) and whose object (found in `manager`) has a +0x94 that answers slot 22 (00571800) or
// slot 24 (005718B0) for the point is pushed to the vector `hits`. *out = hits; returns out.
void** __fastcall CollectWindowsAt(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, long x,
                                   long y, CBaseManager* manager);
void** __fastcall CollectWindowsAt2(void** out, void*, std::uint8_t* first, std::uint8_t* last, void* hits, long x,
                                    long y, CBaseManager* manager);
// 005726B0 (RET 4; the predicate of CWorldClient::ClearMaps' remove_if, ECX unused). An object
// with a positive id is released and true; false otherwise.
bool __fastcall ReleaseIfIdentified(void* functor, void*, CEngineObject* object);
// 0056FC60 (RET 8). Lookup in a hash_map<unsigned, T*>: *value = the entry's value and 1, or 0.
using PendingMap = stlp::hash_map<std::uint32_t, void*, stlp::IntHash>;
int __fastcall PendingMapLookup(const PendingMap* map, void*, const std::uint32_t* key, void** value);
// 0056E400 / 0056E440 (RET 4). Record `index` of a vector of 0x50- / 0xB4-byte records, or null.
// The unit / party records (0x784 bytes) handed around as MFC CArray<record*> (vtable 0073FAA4):
// CWorldClient::CreatePartyUnits, CWorldServer's unit creation, the hero screens. Its SetSize
// (005719B0) and SetAtGrow stay the instantiation's own (RecordArraySetSizeOriginal /
// RecordArraySetAtGrowOriginal, originals_library.h).
using RecordPtrArray = mfc::CArray<std::uint8_t*>;
using RecordVector = stlp::vector<std::uint8_t>;
static_assert(sizeof(RecordVector) == 12);
const std::uint8_t* __fastcall RecordAt50(const RecordVector* records, void*, std::uint32_t index);
const std::uint8_t* __fastcall RecordAtB4(const RecordVector* records, void*, std::uint32_t index);

// A list element whose layout is not needed here (only its size, for the node's).
template <std::size_t N>
struct OpaqueRecord {
    std::uint8_t bytes[N];
};


// The worlds' counted helpers (CWorld + 0x5C, vtable 0073F240, 0xC bytes; the client's +0x1C8,
// 0073F628): player sources (player_source.h; Query AL 0, the client's Segment calls Pump every
// tick). The server's hands its pending link over once (slot 6, 00478880). world_sessions.cpp.
class CWorldHandle : public CPlayerSource {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F240);

    // 0: 0055F260 (DeletingDestruct)
    int OnShutdown() override;          // 2: 0055F1E0 (the link released; built again in place)
    CWorldHandle* Clone() override;     // 3: 0055F1A0 (the same link, counted)
    void TakeNew(void* joined) override;   // 6: 00478880 (the pending link appended, then dropped here; player_sources.cpp)
    void TakeNewImpl(void* joined);   // body of the slot above (registered by member pointer)

    // 0055F280. The link released, the root vtable left.
    void Destruct();
    // 0055F260 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    CWorldHandle* DeletingDestruct(unsigned flags);

    CRefObject* pendingLink_;           // +0x08 counted; handed over by slot 6
};
static_assert(offsetof(CWorldHandle, pendingLink_) == 0x08);

// The client world's player source (CWorldClient + 0x1C8, vtable 0073F628, 8 bytes; Setup makes
// it): it has no state of its own. world_sessions.cpp.
class CClientHandle : public CPlayerSource {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073F628);

    // 0: 00562750 (the counted value's deleting destructor); 4: 005626F0 (AL 0).
    int OnShutdown() override;          // 2: 005626D0 (built again in place: the root vtable, then its own)
    CClientHandle* Clone() override;    // 3: 005626B0 (a new handle, count 0; null without memory)
    // 6: 00562700 (RET 4). The client takes no connections: every link of the list is released
    // and the list emptied (its blocks freed).
    void TakeNew(void* joined) override;
    void TakeNewImpl(void* joined);   // body of the slot above (registered by member pointer)
};
static_assert(sizeof(CClientHandle) == 8);

// A window rectangle of the frame (the client's list +0xF0 and the hit vector +0xF4; 0x18 bytes).
struct ScreenEntry {
    std::uint32_t unused_00;
    unsigned objectId;                    // +0x04 the object, in the map object manager
    std::int32_t rect[4];                 // +0x08 RECT on the screen
};
static_assert(sizeof(ScreenEntry) == 0x18);
using ScreenEntryNode = stlp::list<ScreenEntry>::Node;

// The STLport vector<ScreenEntry> the pickers fill (CWorldClient + 0xF4). Its _M_insert_overflow
// (00573350) is the instantiation's own (world_client_maps.cpp); push_back as the collectors inline
// it.
struct ScreenHits {
    ScreenEntry* start;
    ScreenEntry* finish;
    ScreenEntry* eos;
    void InsertOverflow(ScreenEntry* position, const ScreenEntry* value, int count);
    void PushBack(const ScreenEntry* entry) {
        if (finish != eos) {
            if (finish) *finish = *entry;
            ++finish;
        } else {
            InsertOverflow(finish, entry, 1);
        }
    }
};
static_assert(sizeof(ScreenHits) == 12);

// A marker entry MarkWithinRadius walks (an STLport list's element: node + 8).
struct RadiusMarker {
    std::uint8_t unused_00[5];
    std::uint8_t marked;                  // +0x05 set when within the radius
    std::uint8_t unused_06[2];
    float x, y;                           // +0x08
    std::uint8_t unused_10[0x8C - 0x10];
    float radius;                         // +0x8C scaled by the functor's scale
};
static_assert(offsetof(RadiusMarker, radius) == 0x8C);

// The client's local session (CWorldClient + 0x20C, made by 0066C180), as far as the maps read it:
// the local CPlayer (player_messages.h; its Tick 00665DE0 and Disconnect 00665A10 run on it).
struct WorldSession : CEngineObject {
    std::uint8_t link[4];                 // CPlayer::link_
    // +0x20 the local view: the player's connection (CPlayer::connection_), a CPlayerClient.
    class CPlayerConnection* localView;
    class CPlayerClient* LocalClient() const { return reinterpret_cast<class CPlayerClient*>(localView); }
    std::uint8_t playerFields[0x100 - 0x24]; // CPlayer's id, object maps, party, items and party id
    int gameKind;                         // +0x100 1 / 3: kinds of multiplayer session
};
static_assert(offsetof(WorldSession, localView) == 0x20);
static_assert(offsetof(WorldSession, gameKind) == 0x100);

// The client's rain / snow layer (CWorldClient + 0x6F4, 0xC8 bytes; constructor 004E3C20, drawn by
// 004E5680): an engine object (Teardown calls its slot 3) that keeps the world at +0x20.
class CRainSnow;            // effects.h

// The client's text resources (CWorldClient + 0x1D0, 0x2C bytes, vtable 0073F6F0; constructor
// 004DED20 after the engine object fields and the table at +0x1C, 00562A80).
struct ClientTexts : CEngineObject {
    std::uint8_t table[0x10];             // +0x1C
};
static_assert(sizeof(ClientTexts) == 0x2C);

// The client's managers Setup builds with their constructors partly inlined (base 00402B20).
// +0x1A0 (0xCC bytes, vtable 0073F5B8): a CList at +0xB0 (vtable 0073F61C, block size 10).
struct ClientListManager : CBaseManager {
    // 00562EC0. The destructor's body: every listed object released, the list emptied, then the
    // manager's own body.
    void DestructBody();
    // 00562EA0 (slot 0, RET 4): DestructBody, then operator delete when flags bit 0 is set.
    ClientListManager* DeletingDestruct(unsigned flags);

    mfc::CList<CEngineObject*> list;      // +0xB0 counted
};
static_assert(sizeof(ClientListManager) == 0xCC);
// +0x1A4 (0xD0 bytes, vtable 0073F554): a counted object and a CList (constructor 005715D0, 10).
struct ClientArrayManager : CBaseManager {
    // 00562DF0. The destructor's body: Clear (004E2850: the object and every listed one released),
    // the list's and the pointer's own destructors, then the manager's body.
    void DestructBody();
    // 00562DD0 (slot 0, RET 4).
    ClientArrayManager* DeletingDestruct(unsigned flags);

    CEngineObject* current;               // +0xB0 counted
    mfc::CList<CEngineObject*> list;      // +0xB4 counted (vtable 0073F73C)
};
static_assert(sizeof(ClientArrayManager) == 0xD0);
// +0x19C: the particle manager, CParticleManager (effects.h).

// The render device (the global pointer at 007AF068): three stacks of 4x4 float matrices.
struct MatrixDevice {
    std::uint8_t pipelineHeader[0x14];  // the pipeline's vtable, count, transform and library (CTransformPipeline)
    float* pickStack;      // +0x14 used by the cursor picking
    int pickDepth;         // +0x18
    std::uint8_t stack1[0x24 - 0x1C];      // stack 1
    float* objectStack;    // +0x24 object -> world, pushed while testing an object
    int objectDepth;       // +0x28
    std::uint8_t stacks3to8[0x5C - 0x2C];  // stacks 3..8
    float* viewStack;      // +0x5C the view matrices
    int viewDepth;         // +0x60

    // 004F16A0 (world_client_view.cpp). Where the bounding volume lies against the view frustum;
    // 2 = outside.
    int Classify(const void* bounds);
};
static_assert(offsetof(MatrixDevice, pickStack) == 0x14);
static_assert(offsetof(MatrixDevice, objectStack) == 0x24);
static_assert(offsetof(MatrixDevice, viewStack) == 0x5C);
inline MatrixDevice* TheMatrixDevice() { return *reinterpret_cast<MatrixDevice**>(std::uintptr_t(0x007AF068)); }

// The drawing side (vtable 0073F41C).
class CWorldClient : public CWorld {
public:
    void BuildUnitSkins(const void* othersVector, const void* dressedVector);
    // 005641F0, vtable slot 6 (world_client.cpp). The client's tick ("Segment"): autosave event
    // and sound volumes on frame 10, the terrain, sound and session updates, the objects and units,
    // effect lists, the lights' grey levels, the map state every 32 ticks. Returns 1; 0 when the
    // session's view has lost its connection (the session is disconnected, FatalError; Rebirth's
    // DisableExitOnCrashReport keeps the session).
    int Segment();
    // 00569E40 (world_client_draw.cpp). Draws the world view: view matrix, sky, terrain passes
    // around DrawPass, effects and particles, flying hit points. Returns 1. Rebirth's
    // LandscapeDrawRadius sets the terrain's draw distance (100 in the game).
    int Render();
    // 0056AFE0 (world_client_draw.cpp). The object passes: collects the visible objects of the
    // sectors around the camera (ObjectsDrawRadius widens the 100.0 margin), sorts and draws them,
    // the light effects, shadows and selection markers, then empties the frame's lists. Returns 0
    // without terrain, else 1.
    int DrawPass();
    // 00563F70, vtable slot 27. Ground height with the position clamped into the map.
    float ClampedGroundHeight(float x, float y);
    // A-X5 (ground_print.cpp). 0056CE90 (RET 0x18): a blood print at (x, y) when the setting is on
    // and prints.db's blood entry for the ground there has an alpha and a life (`tile`: the texture
    // quarter and, in the high half, the kind; `fresh` 1 for a hit, 0 for a death), coloured by
    // this frame's sunlight.
    void AddBloodPrint(float x, float y, float halfSize, float fullSize, std::uint32_t tile, int fresh);
    // 0056D150 (RET 0x24): a footprint. A blood print reached (its half width plus the footprint's
    // mean half size; not a fresh one) gives its kind; with a kind the blood entry, else the
    // footprint entry, gives alpha and life. Returns the blood print's kind, else -1 (also when
    // footprints are off).
    int AddFootprint(float x, float y, float halfWidth, float halfLength, float angle, std::uint32_t tile, float scale,
                     int visible, int bloodKind);
    // 0056D4C0 (RET 0x18): a scorch mark of `alpha` (made when it is positive and the scorch entry
    // has a life).
    void AddScorchMark(float x, float y, float halfWidth, float halfLength, float angle, float alpha);
    // A-X5 (world_client_decals.cpp). 0056C750 (RET 0x24): lays a decal on the ground into the three
    // arrays (shadows 0051CE60, ground prints 006CEDE0). The static builder (007AF488) starts on the
    // quad `corners` projected along `direction` from `point`; the terrain under it adds its
    // triangles unless `skipOnPlatform` is set and the point lies on a platform; with `objects`,
    // every platform face's object that the decal can touch adds the triangles of each part of its
    // figure (transformed by the object's and the part's matrices, on the scene pipeline's stack
    // 14). An object with several faces adds its parts once per face.
    void ProjectDecal(const Vector3* direction, const Vector3* point, float depth, VertexPacket* const* corners,
                      lib::AlignedArray<VertexPacket>* points, lib::AlignedArray<DecalTexCoord>* texCoords,
                      lib::AlignedArray<std::uint16_t>* indices, std::uint8_t skipOnPlatform, std::uint8_t objects);
    // 006CBC00 (RET 4; terrain_ambience.cpp). The ambient sounds' time of day into `out`: "day"
    // from 6 to 18 o'clock, "morning" before 6 and "evening" after 18 within 2..22, else "night".
    // Returns out.
    mfc::CString* AmbienceTimeOfDay(mfc::CString* out);
    // 005640A0. Nearest living unit of the local player's group that `unit`'s side can see
    // (relation masks +0x244/+0x248 of the candidate); null when none or `unit` is null.
    CEngineObject* NearestVisibleUnit(CEngineObject* unit);
    // 005699F0. The local view (session +0x20; its +0x1C is the local WorldPlayer), or null.
    std::uint8_t* LocalView();
    // 00566EC0. Whether the local view is in mode 2 (+0x20).
    bool IsLocalViewMode2();
    // world_small_calls.cpp (A-W14).
    // 005655D0 / 005655F0 (RET 4; the scene loader, 0051B2F0 / 0051B470). A new texture / material
    // of class `type` from the world's managers (+0x18C NewTextureOfType, +0x188 NewMaterial);
    // null without the manager.
    CTexture* NewTexture(int type);
    CMaterial* NewMaterial(int type);
    // 00565670 (RET 4; the game start, 00456440). Takes `camera` as the view camera: the old one
    // loses a reference (it is not released at zero), the new one gains one; either may be null.
    void SetCamera(CCamera* camera);
    // 00566C10 (RET 4). The dword of message 0x12 (CPlayerClient::OnWorldValue) stored at +0x7C0.
    void SetWorldValue(int value);
    // 005683B0 (RET 4). Message 0x73: the water levels (+0x70) read from `stream` and applied to
    // the terrain (TweenLoad). Returns 1.
    int ReadWaterLevels(CStream* stream);
    // 00566EF0. FALSE without a local view or while the local player's controller is busy.
    bool IsLocalPlayerIdle();
    // 00567440. Index of the exit whose area contains `point` and that leads somewhere.
    BOOL FindExitAt(const float* point, int* index);
    // 005674C0. Centre of exit `index`'s rectangle.
    float* ExitCentre(float* out, int index);   // returns out
    // 00567500 (the client world load 00567AB0). One ZoneExit particle source (0x2038) per exit
    // that leads somewhere, in +0xA0 at the exit's index (others null; the old markers released):
    // at the rectangle's centre, half extents and the angle's cos / sin at +0x118..+0x124, ticked
    // 20 times; then UpdateExitMarkers.
    void CreateExitMarkers();
    // 005677F0 (RET 0xC; the client glue 00578A20). Whether the party can leave through `exit`:
    // every living unit of `records` (unit id at +0x1C) stands in an exit (FindExitAt) and that
    // exit is `exit`. Those that do are listed in `inExit` (emptied first). 0 when no unit lives.
    BOOL PartyInExit(const RecordPtrArray* records, int exit, RecordPtrArray* inExit);
    // 00566C20. Makes the bird (+0x1B0, a CEffectMoshka) when there is none, resets it (slot 18)
    // and has it follow the unit of the local party's first record.
    void SummonBird();
    // 00565380 (RET 8; the server's quest variable message through 00580840). A quest variable
    // "q.<quest>.<name>" changed to `value`: every filed object whose name (+0x1A8) is <name> (the
    // text after the second dot) gets the visibility level `value` (0050F170); a value of 2 (or
    // NaN) with a <name> without a dot summons the bird (as SummonBird).
    void OnQuestVariable(const char* name, float value);
    // 005654F0 (RET 8; `this` not read). The party variable of an object's name (CMapObject +0x1A8):
    // "q.<local player's name>.<name>", constructed in `out`; returns `out`.
    mfc::CString* ObjectVariableName(mfc::CString* out, const char* name);
    // 005652E0 (the client's party variables arrived). Every filed object with a name gets the
    // visibility level of its variable (ObjectVariableName) in the local session's variables.
    void ApplyObjectVariables();
    // 0056D9F0. Flags describing the current environment for `unit`: unit state (+0x268), island
    // (gipat/ingos/suslanger), night (0x100) or day (0x80), and two world settings.
    std::uint32_t EnvironmentFlags(CEngineObject* unit);

    // world_client_view.cpp
    // 0056A990 (RET 0x18). Walks one object list (a map sector's, the deferred unit list at
    // 007AF5B4 or +0xB4), tests each object with a model against the view frustum and files it in
    // the frame's draw lists (world_client_view.cpp). `view` is the camera matrix (M * v),
    // `animTime` the game time handed to units' slot 25, `now` the timeGetTime() of the frame.
    // With `animateUnits` 0 units are only queued on the deferred list; otherwise they are animated
    // (slot 25) and filed like other objects. `withScripted` lets heroes with a text-key id
    // through. Returns how many objects were queued for the model-touch list (007AF508).
    int CollectVisible(void* list, const Matrix4* view, float animTime, std::uint32_t now, char animateUnits,
                       char withScripted);
    // 0056BF30 (RET 0xC). The ground point under screen pixel (x, y): inverts the current view
    // matrix and iterates on the terrain height (Terrain::HeightAt). Returns out (x, y, z).
    Vector3* PickGround(Vector3* out, int x, int y);
    // 0056C350 (RET 0xC). Same with the client's surface height (slot 29: platforms included).
    Vector3* PickSurface(Vector3* out, int x, int y);

    // world_client_maps.cpp
    // 005647E0 (RET 4). Files an object in the client's maps: an object whose id is already in the
    // list +0xB8 is only referenced and released. Otherwise: objects answering slot 29 are added to
    // the MFC list +0xBC and the id map +0xD8 (id -> list node); classes 0x54 / 0x5D add their
    // platform items (flag 0x400) to +0x1D4; units get slot 43 of their +0x240 in a multiplayer
    // session of kind 1 (+0x6EC >= 1, unit +0x24C clear); then the list +0xB8 (with a
    // reference), the sector grid +0x120 (removed first, then appended) and, for heroes with a
    // text-key id, the list +0xB4. Returns 1.
    int AddObject(CEngineObject* object);
    // 005651A0 (RET 4; a platform object moved: 0052B490, the client's 00668840). When an object
    // with `object`'s id is filed (+0xB8): its platform faces are removed and, for classes 0x54 /
    // 0x5D, its platform items added again where it now stands (as AddObject). EAX is not read.
    void RefreshPlatforms(CEngineObject* object);
    // 0056D7E0 (RET 4). Files `object` in the synchronised list +0xBC and the id map +0xD8 (as
    // AddObject does) unless its id is there already. AL: 1 when added (the upper bytes of EAX
    // are not a result; the callers do not read it).
    bool QueueSynchronised(CEngineObject* object);
    // 0056D7A0 (the options' display mode change). Every filed object whose kind has no shadows
    // (0051E200 false) drops its shadow (0051E280).
    void DropShadows();
    // 00566100 (RET 4; the battle screen and the interface's selection). Takes `object` out of the
    // selection list +0x7BC, its highlight off (flags 0x110 cleared, side half slots 25 / 26 with
    // 0) when it was on; with null every listed object's highlight goes off and the list empties.
    void Deselect(CEngineObject* object);
    // 00566030 (RET 4; A-W13). Adds `object` (not null) to the selection list: its part bounds
    // refreshed (CObject3D::UpdatePartBounds), the highlight on (flag 0x10; side half slot 26 with 1
    // unless flag 0x100 is set, then slot 25 with 1) when it was off, and its id appended unless
    // it is listed already.
    void Select(CEngineObject* object);
    // 00569D60 (Render calls it last; A-W13). Draws every line list of lineLists_ (0049B780 with the
    // scene pipeline and the renderer). Returns 1.
    int DrawLineLists();
    // 005630B0 (00567A60). One more reference on every item (slot 18's manager) and, when the item
    // factory exists (tested again, not the spell manager), on every spell (slot 22's).
    void ReferenceItemsAndSpells();
    // 00563190 (Teardown). The reverse: every item (slot 18's manager) and then every spell (slot
    // 22's) released (slot 4), each manager then emptied (its slot 15, DestroyAll).
    void ReleaseItemsAndSpells();
    // 00567A60 (RET 4; a zone state's block 3, player_client.cpp; A-W13). The items and spells
    // replaced: all released, then (a block with data) the spell and item managers loaded (slot 9,
    // then their objects, 004047E0) and referenced again. Returns 1.
    int LoadItemsAndSpells(CStream* block);
    // 00567950 (RET 4; block 5). The block read from its start: the spells and then the items it
    // lists (LoadManagerObjects). 0 when the block fails.
    int LoadNewItemsAndSpells(CStream* block);
    // 00564BE0 (RET 4). Takes the object `id` out of the maps: the MFC list +0x1F0 entries for it,
    // its +0xBC node and +0xD8 entry, then (when it is in +0xB8) the +0xB4 entries of a hero with
    // a text-key id (and 00538EA0 on its +0x240 in a session of kind 3), its grid cell and +0xB8;
    // the object is released.
    void RemoveObject(int id);
    // 00564E70. Empties the maps: both sector grids' cells, the MFC lists +0x1F0 and +0xBC, the id
    // map +0xD8, the lists +0x7BC and +0xB4, the +0xB8 entries with a positive id (released), the
    // session's player data, +0x34's slot 5 and the managers +0x3C/+0x40/+0x44's records.
    void ClearMaps();
    // 00565090 (A-W12). The zone's leftovers dropped: the counted array +0xA0 emptied (its own
    // SetSize releases them), the second grid's cells cleared (counted elements released), every
    // timed object (slot 14's manager) released, the bird (+0x1B0) reset (slot 18).
    void ResetZoneObjects();
    // 00567AB0 (RET 4; world_client_maps.cpp, A-W15; patched by Rebirth, so hooked in the vanilla
    // launch only: the player's client reaches it through its address). The zone's state block:
    // the flow acknowledges the server, sounds stop, then from `block` the tick, the map and .mob
    // names, the exits, the cave flag and the unit looks; the local server makes sure it has the
    // zone (00578730); the ground prints, ResetZoneObjects, ClearMaps, the items and spells and
    // the idle particle sources go; the terrain loads the map (a name without a drive from the
    // maps folder) with the allod of the local player's zone, the sector grids take its size
    // (cells of 16), the sky, lights table, exit markers, lighting and music follow the cave
    // flag, the local server's pending messages are appended to the view's, the unit skins are
    // built and the view resumes. Loading movie steps 8-11, then closed. Returns 1, or the
    // result of the fatal error when the map does not load.
    int LoadZone(CStream* block);
    // 00566CE0 / 00566D30 / 00566D80 (RET 4; world_client_session.cpp). The local session takes a
    // new link: the single-player loopback pair for `player` (CWorld's teardown 0055F890 first), a
    // replay of the recording `path`, or a driver for the game client's first connection (false
    // without an active client or connection; the argument is unused). Each empties the maps
    // (ClearMaps, ResetZoneObjects) and reconnects the session (CPlayer::Connect null, then the link).
    void SetPlayer(std::uint8_t* player);
    void PlayRecording(const char* path);
    bool JoinGameClient(const char* unused);
    // 00566E20 / 00566E70 (world_client_session.cpp). The player sources (+0x5C, +0x1C8) and the
    // network session (+0x60) pumped, then the local player's link (when it is active) asked
    // HasIncoming (slot 6, AL) / IncomingCount (slot 7); false / 0 without one.
    bool PumpHasIncoming();
    int PumpIncomingCount();
    // 00565790 / 005659D0 (RET 4). The object at screen point `point` (x, y): the screen entries of
    // the list +0xF0 that contain it and whose object's +0x94 answers slot 22 (00565790) or 24
    // (005659D0) go to the vector +0xF4, which is sorted front to back (DepthLess with the view
    // matrix); the first entry whose object still exists wins (+0x100 is left on it). Null if none.
    CEngineObject* PickAt(const long* point);
    CEngineObject* PickAt2(const long* point);
    // 00565E40 (RET 4; the battle screen's clicks and hover). The unit at screen point `point`:
    // from PickAt2's object on, the first unit among the sorted hits (the cursor +0x100 moves past
    // the others and those whose object is gone); failing that the same from PickAt's. Null if
    // none.
    CEngineObject* UnitAt(const long* point);
    // 00565D00 (RET 4). The same from PickAt2's object only (no second pick).
    CEngineObject* UnitAt2(const long* point);
    // 005656A0 (RET 8; the battle screen's selection box). Every object of the frame's window list
    // (+0xF0) that still exists and whose side half has a drawn part meeting the screen rectangle
    // `rect` (slot 23) is appended to `out`.
    void UnitsInBox(const long* rect, stlp::list<CEngineObject*>* out);
    // 0056D740 (RET 8; a party unit's time step, unit_vision.cpp). Marks the footprints (+0x1BC)
    // within their scale times `range` of the point (x, y): MarkWithinRadius over the list.
    void MarkFootprintsInSight(const float* point, float range);
    // 00564190 (RET 8; the server's message 0x72). The wind: stored (CWorld::SetWind), given to the
    // terrain's waves (a copy of the direction) and to the rain and snow (+0x6F4).
    void ApplyWind(const Vector3* direction, float strength);
    // The client's height slots 29 / 30 (005618E0 / 00561930; ST0, RET 8): slot 27 / slot 28 at
    // (x, y), stored as a float and raised to the platforms over the point (PlatformSet::
    // SurfaceHeight).
    double ClientGroundHeight(float x, float y);
    double ClientSurfaceHeight(float x, float y);
    // 0056CE40 (RET 0xC; the server's number messages, a living object's hit). A number over the
    // unit with id `id` (the flying hit points, +0x1B4); nothing for another object or without them.
    void ShowNumber(int id, int value, std::uint32_t kind);
    // 0056DE80 (the client's game state 3). Every unit among the objects (+0xB8) has its
    // controller load the face texture (00538DB0).
    void LoadUnitFaces();
    // 00578A60 (RET 4; the glue 00578A40). The target zone of exit `index` (the original copies
    // the exit out of the array and returns the copy's text, which shares the element's buffer).
    const char* ExitTarget(int index);
    // 00567400 (RET 4; the glue 005789E0). Removes the units of a record array (each record's unit
    // id, +0x1C) from the world.
    void RemoveUnitsOf(const RecordPtrArray* records);
    // 00566330 (RET 4; the glue 00578D30). The local client sends its message 0x10 with `value`.
    int SendFlag10(int value);
    // 005662B0 (RET 4; the server's camera placement message). The camera looks at the point (x, y)
    // at its own focus height, with its view applied and kept above the objects. Returns 1.
    int CentreOn(const Vector3* point);

    // The debug display's switches (the console's debug commands; bits 0-3 of +0x88): the frame
    // rate 00569A10 / 00569A20, the render counters 00569A40 / 00569A50, the managers' lines
    // 00569A90 / 00569AA0 and the world's 00569B10 / 00569B20. Switching one off also takes its
    // lines out of the debug info panel (0079B930).
    void ShowFps();
    void HideFps();
    void ShowRenderInfo();
    void HideRenderInfo();
    void ShowManagers();
    void HideManagers();
    void ShowWorldData();
    void HideWorldData();
    // 00566F70 (RET 8). Creates the party's heroes from their records (+0x14 prototype, +0x18
    // second prototype, +0x1C id, +8 model, +0x10 skin, +0x20 stats, +0x724 position, +0x730 /
    // +0x740 / +0x750 weapons, quick items and armour as pointer arrays with their counts after):
    // the figure from the part names (the hair "hr.%.2d" by the prototype's number), items, player
    // (+0); each unit is filed with AddObject. Armour of type 0 is put on only with `allArmour`.
    // Stops at the first unit that cannot be created or given a figure.
    void CreatePartyUnits(const RecordPtrArray* party, char allArmour);
    // 0056DAF0 (RET 0xC; the camera's 0040E4C0). Appends to the plain pointer list `out` every
    // object of the sector grid's cells around `point` whose (x, y) (+0x1C plus +0x128) lies within
    // radius + hypot(+0x144, +0x148) of the point. The cell range (+0x138..+0x144, truncated and
    // clamped to the terrain) and the walk's cursor (+0x148 / +0x14C) are left in the client.
    // The original's z difference is the point's z minus itself (0, kept).
    void ObjectsInRange(const float* point, float radius, void* out);
    // 00563290 (slot 3; also 00456440 and 00561B00). Tears the client world down: the camera's
    // target, the objects at +0x1B4 / +0x1AC / +0x1B0, 00563190, +0xA0, the lists +0x1B8, +0x1BC,
    // +0x1C0, +0x6F4 and the platforms (slot 3), +0x1F0, the sector grid's cells, the lists +0x7BC,
    // +0xB4 and +0xB8 (released), the session (+0x20C), the counted +0x1C8, 0055F8E0, then the
    // owned objects +0x1A0 .. +0x1D0 (counted ones dropped first) deleted and cleared.
    // (Not declared as the Destroy override: a virtual member's address is a vtable thunk, and
    // hooking the slot's own code with it recurses.)
    void Teardown();
    // 00561E50 (slot 2; also 00456440). Sets the client world up: the managers (0055EE40), the
    // texture path, the lights table (LightsGipat.ini, 00563930), the owned objects +0x1CC, +0x1D0,
    // the terrain (+0x180, its constructor inlined), +0x44, +0x18C, +0x188, +0x190, +0x19C, the
    // session (+0x20C), the counted +0x1C8, the text resources, +0x1A0, +0x1A4, +0x1A8, +0x6F4 and the
    // platforms, flag 0x200, the cursor (+0x7D0), a reference on every player, room for 400 screen
    // entries (+0xF4), and both sector grids at 4 x 4. Returns 1.
    int Setup();
    // 00563930 (RET 4; world_client_setup.cpp). Reads the light colours from config\<file> (an
    // .ini, LightsGipat.ini): each key of [sunlight], [ambient] and [sky] names its hour in
    // characters 4..5 ("hour07") and holds "r, g, b" (0..255, scaled by 1/255, alpha 1); the hours
    // between two keys in file order are filled by linear steps from the earlier key (the first
    // from hour 0). Hours are not range-checked. Returns 1.
    int LoadLights(const char* file);
    // 00566360 (world_client_lights.cpp; Render, the zone load and message 0x71 call it). The
    // frame's light from the time of day (00567680 first): fog start, sun direction (the hour
    // turns it once around +z, its elevation peaks at noon), sunlight, ambient and sky colours
    // interpolated between the hour's table entries; the renderer's sun, ambient and fog colour,
    // the sky's lighting; once per new hour the terrain's sun (006C8ED0). A cave takes hour 0's
    // colours and a fixed sun. Returns 1 when a new hour began (or the renderer's +4 bit 0 is
    // clear: nothing done), else 0; no caller reads it.
    int UpdateLighting();
    // 00567680 (world_client_lights.cpp). With one marker per exit (+0xA0, ZoneExit particle
    // sources made by 00567500): a marker whose target zone's party variable "z.<zone>" is 1 (or
    // NaN) gets extent (1, 0, 0) (its particles turn red), the others (0, 0, 0).
    void UpdateExitMarkers();
    // 00563710 (RET 4; world_client_setup.cpp; once at startup, 00456440, with the renderer).
    // The texture manager gets `device` (006913A0); the platforms and sky (00562F70); the digit
    // sprites (+0x1B4) with the "numbers" texture (kind 6; flag 0x40000); a new figure loader for
    // the figures resource (the CString 0079BC94) in +0x34 (counted, the old one released); then
    // the unit database counts the sound files of +0x1CC.
    void SetUpGraphics(void* device);
    // 00562F70 (world_client_setup.cpp; SetUpGraphics). Once (+0x1AC null): a new CSky; the figure
    // manager makes a CSkyObject (class 0x44, id -666) named "nask0" that loads "nask0sky.fig"
    // from the figure resources and becomes the sky's object (flag 0x10), with a material of type
    // 0x12. Without the file the object is referenced and released again.
    void LoadSky();

    // 005612F0 (world_construct.cpp; the static initialiser 00560620). The constructor: the base
    // (ConstructBase), the members' constructors in order (the zone exits, the counted array, the
    // lists and maps, both sector grids at 1 x 1, the ground print lists, the platform set, the rain
    // and snow layer (cleared), the selection list, the window's names), the flags 0x3DC0, the vtable
    // 0073F41C and the fields it zeroes. Returns this.
    CWorldClient* Construct();
    // 00561B00 (S13a, world_destructors.cpp; also the global client world's exit function). The
    // destructor's body: the client vtable, Teardown, then the members last to first (the window
    // names, the lists, the rain and snow layer, the platforms, the handle, the ground prints, both
    // sector grids set to 1 x 1 and freed, the maps, the counted array, the exits) and the base's
    // body (CWorld::Destruct).
    void Destruct();
    // 00561A00 (slot 0 of 0073F41C): Destruct, then operator delete when bit 0 of `flags` is set.
    // Returns this.
    CWorldClient* DeletingDestruct(unsigned flags);

    // The local session (+0x20C) as the maps read it.
    WorldSession* Session() const { return reinterpret_cast<WorldSession*>(session_); }

    int unused84_;                       // +0x084 -100000 after Setup
    std::uint32_t debugFlags_;            // +0x088 the debug display's switches (bits 0-3: ShowFps ...)
    void* exitsVtable_;                   // +0x08C CArray<ZoneExit> (ZoneExitArray) from here
    ZoneExit* exits_;                     // +0x090
    int exitCount_;                       // +0x094
    std::uint8_t exitArrayTail[0xA0 - 0x98]; // the exit array's capacity and grow-by (read through the CArray)
    // +0x0A0 counted objects (MFC CArray; its own SetSize 00570000 releases the dropped ones).
    mfc::CArray<CEngineObject*> countedA0_;
    SectorList heroes_;                   // +0x0B4 heroes with a text-key id (counted)
    SectorList objects_;                  // +0x0B8 every filed object (one reference each)
    // +0x0BC the objects answering slot 29 (IsSynchronised), with their nodes by id in +0x0D8 and
    // the highest id at +0x0EC (Segment drops those whose update returns 0).
    mfc::CList<CEngineObject*> dynamicObjects_;
    PendingMap dynamicNodes_;             // +0x0D8 id -> its node in dynamicObjects_
    int dynamicMaxId_;                    // +0x0EC
    stlp::list<ScreenEntry> screenEntries_;   // +0x0F0 the window rectangles of the frame
    ScreenHits hits_;                     // +0x0F4 the entries under the point (PickAt)
    ScreenEntry* hitCursor_;              // +0x100 PickAt's walk, left on the entry it returns
    std::uint8_t unused_104[0x108 - 0x104];
    stlp::hash_map<unsigned, void*, stlp::IntHash> lineLists_;   // +0x108 line lists by key (DrawLineLists)
    int lastLineListKey_;                 // +0x11C the last line list key handed out (00569B60); 0 from the constructor
    SectorGrid objectGrid_;               // +0x120 the objects by sector (slot 41 moves them)
    // +0x138 ObjectsInRange's cell range and its walk's cursor.
    std::uint32_t rangeFirstX_, rangeFirstY_, rangeLastX_, rangeLastY_, rangeX_, rangeY_;
    SectorGrid secondGrid_;               // +0x150 (slot 42 moves objects in it)
    std::uint8_t unused_168[0x180 - 0x168];
    Terrain* terrain_;                    // +0x180 counted
    // +0x184 CString: the zone's allod in lower case ("gipath" after Setup; the zone load takes it from
    // the map database's record of the local player's zone, "gipat" without one). It names the
    // terrain's texture pages, the lights table and the music theme.
    char* zoneName_;
    CBaseManager* manager188_;            // +0x188 counted (constructor 0068E3F0)
    CTextureManager* textureManager_;     // +0x18C (slot 34) counted
    CBaseManager* manager190_;            // +0x190 counted (constructor 0068D850)
    CEngineObject* manager194_;           // +0x194 (slot 37 returns it; not identified)
    CEngineObject* manager198_;           // +0x198 (slot 38 returns it; not identified)
    CParticleManager* particleManager_;        // +0x19C counted
    ClientListManager* listManager_;      // +0x1A0 owned
    ClientArrayManager* arrayManager_;    // +0x1A4 owned
    struct CMusicSystem* music_;          // +0x1A8 owned (sound.h; constructor 006B4350)
    struct CSky* skyDome_;                // +0x1AC the sky (owned; destructor body 005636A0; sky_object.h)
    CEngineObject* owned1B0_;             // +0x1B0 owned
    CFlyingHitPoints* numbers_;           // +0x1B4 owned
    // +0x1B8 / +0x1BC / +0x1C0 the ground prints (ground_print.h; element destructors 0056D080,
    // 00575560, 00575580). Segment ticks them (slot 0) and colours the blood prints by the light.
    stlp::list<BloodPrint> bloodPrints_;
    stlp::list<Footprint> footprints_;
    stlp::list<GroundPrint> scorchMarks_;
    int lightLevelsCountdown_;            // +0x1C4 time left before the blood prints are lit again (Segment); 0 after Setup
    RefPtr handle_;                       // +0x1C8 (vtable 0073F628)
    CSoundManager* sounds_;               // +0x1CC the sound manager (owned; the client controller's hit sounds)
    ClientTexts* texts_;                  // +0x1D0 the text resources (counted)
    PlatformSet platforms_;               // +0x1D4
    std::uint8_t* session_;               // +0x20C local session; its +0x20 is the local view
    CCamera* camera_;                     // +0x210 the view camera (DrawPass, Render)
    std::uint8_t unused_214[0x224 - 0x214];
    char* texturePath_;                   // +0x224 CString: <data path>textures\ (Setup)
    int constant228_;                     // +0x228 10 from the constructor; nothing in the exe reads it
    // +0x22C / +0x3AC / +0x52C the light colours by hour (0..23; r, g, b, 1) from the lights table's
    // [sunlight], [ambient] and [sky] sections (LoadLights).
    LightColour sunlight_[24];
    LightColour ambient_[24];
    LightColour sky_[24];
    // +0x6AC / +0x6BC / +0x6CC this frame's light (UpdateLighting): the ambient colour (the
    // table's, or 0.8 x the sunlight when the table's is black; its fourth float +0x6B8 is never
    // written), a copy of it (four floats, the sky's ambient) and the sunlight (its fourth float is
    // whatever the original's stack held).
    LightColour frameAmbient_;
    LightColour skyAmbient_;
    LightColour frameSun_;
    int lightingMark_;                    // +0x6DC 0 after Setup, 0x80 once UpdateLighting runs outside a cave; never read
    // +0x6E0 1: a cave zone (the map's flag: LightsCave.ini, the sky "Sky01", light fixed at hour 0;
    // EnvironmentFlags 0x400 instead of 0x200).
    int cave_;
    int partyDanger_;                     // +0x6E4 0 after Setup; the party state's danger (message 0x60: 2 fighting)
    int constant6E8_;                     // +0x6E8 0 from the constructor; nothing in the exe reads it
    int ticksRun_;                        // +0x6EC Segment's count; AddObject fades units in (slot 43) when >= 1
    float tickFraction_;                  // +0x6F0 added to the tick for the units' time (network client)
    // +0x6F4 the rain / snow layer. CRainSnow is abstract here like every engine object, so the
    // world keeps its bytes and RainSnow() is the object. Its `mode` (+0x7A8 of the world: 1 rain,
    // 2 snow) picks the ground prints' wet look and an environment flag.
    alignas(4) std::uint8_t rainSnow_[0xC8];
    CRainSnow* RainSnow() { return reinterpret_cast<CRainSnow*>(rainSnow_); }
    const CRainSnow* RainSnow() const { return reinterpret_cast<const CRainSnow*>(rainSnow_); }
    stlp::list<void*> list7BC_;           // +0x7BC (nodes of 0xC bytes; pool free 00498C40)
    int worldValue_;                      // +0x7C0 -1; message 0x12 sets it (SetWorldValue); the exe never reads it
    int constants7C4_[3];                 // +0x7C4 0 from the constructor; nothing in the exe reads them
    void* cursor_;                        // +0x7D0 HCURSOR wincurs\Df0000.cur
    // +0x7D4 .. +0x7E0 the window's names, set by the constructor and freed by the destructor only:
    // "EvilIslands", "EvilIslandsClass", "EvilIslands", "EvilIslands Paused".
    mfc::CString appName_;
    mfc::CString windowClass_;
    mfc::CString windowTitle_;
    mfc::CString pausedTitle_;
};
static_assert(offsetof(CWorldClient, unused84_) == 0x84);
static_assert(offsetof(CWorldClient, countedA0_) == 0xA0);
static_assert(offsetof(CWorldClient, heroes_) == 0xB4);
static_assert(offsetof(CWorldClient, objects_) == 0xB8);
static_assert(offsetof(CWorldClient, dynamicObjects_) == 0xBC);
static_assert(offsetof(CWorldClient, dynamicNodes_) == 0xD8);
static_assert(offsetof(CWorldClient, dynamicMaxId_) == 0xEC);
static_assert(offsetof(CWorldClient, screenEntries_) == 0xF0);
static_assert(offsetof(CWorldClient, hits_) == 0xF4);
static_assert(offsetof(CWorldClient, hitCursor_) == 0x100);
static_assert(offsetof(CWorldClient, rangeFirstX_) == 0x138);
static_assert(offsetof(CWorldClient, rangeY_) == 0x14C);
static_assert(offsetof(CWorldClient, secondGrid_) == 0x150);
static_assert(offsetof(CWorldClient, manager188_) == 0x188);
static_assert(offsetof(CWorldClient, particleManager_) == 0x19C);
static_assert(offsetof(CWorldClient, owned1B0_) == 0x1B0);
static_assert(offsetof(CWorldClient, bloodPrints_) == 0x1B8 && offsetof(CWorldClient, scorchMarks_) == 0x1C0);
static_assert(offsetof(CWorldClient, handle_) == 0x1C8);
static_assert(offsetof(CWorldClient, sounds_) == 0x1CC);
static_assert(offsetof(CWorldClient, texts_) == 0x1D0);
static_assert(offsetof(CWorldClient, texturePath_) == 0x224);
static_assert(offsetof(CWorldClient, lightingMark_) == 0x6DC);
static_assert(offsetof(CWorldClient, frameAmbient_) == 0x6AC && offsetof(CWorldClient, frameSun_) == 0x6CC);
static_assert(offsetof(CWorldClient, ticksRun_) == 0x6EC);
static_assert(offsetof(CWorldClient, rainSnow_) == 0x6F4);
static_assert(offsetof(CWorldClient, list7BC_) == 0x7BC);
static_assert(offsetof(CWorldClient, cursor_) == 0x7D0);
static_assert(offsetof(CWorldClient, exits_) == 0x90);
static_assert(offsetof(CWorldClient, objectGrid_) == 0x120);
static_assert(offsetof(CWorldClient, terrain_) == 0x180);
static_assert(offsetof(CWorldClient, platforms_) == 0x1D4);
static_assert(offsetof(CWorldClient, session_) == 0x20C);
static_assert(offsetof(CWorldClient, camera_) == 0x210);
static_assert(offsetof(CWorldClient, textureManager_) == 0x18C);
static_assert(offsetof(CWorldClient, numbers_) == 0x1B4);
static_assert(offsetof(CWorldClient, sunlight_) == 0x22C);
static_assert(offsetof(CWorldClient, sky_) == 0x52C);
static_assert(offsetof(CWorldClient, constant228_) == 0x228 && offsetof(CWorldClient, constant6E8_) == 0x6E8);
static_assert(offsetof(CWorldClient, lastLineListKey_) == 0x11C && offsetof(CWorldClient, constants7C4_) == 0x7C4);
static_assert(offsetof(CWorldClient, appName_) == 0x7D4 && sizeof(CWorldClient) == 0x7E4);

// MatrixRows (the first three rows of a 4x4 matrix; Transform 00560E10) is in src/lib/vecmath.h.

// Sorting predicates passed to STLport sort (operator() of empty or small functors).
// 00573A60. Orders objects (by id at +4 of each entry) by distance along a view plane; dead and
// living objects are separated first.
struct DepthLess {
    float* plane;                         // +0 plane at +0x20 (normal) and +0x2C (offset)
    void* manager;                        // +4 CBaseManager holding the objects
    bool Less(const int* a, const int* b);
};
// 00574300. Orders units by group rank, then sequence number, then +0x30.
struct UnitOrderLess {
    bool Less(CEngineObject* a, CEngineObject* b);
};
// 00589580. Orders entries by the unsigned value at +0x1C of the object they point to (+8).
struct EntryIdLess {
    BOOL Less(const std::uint8_t* a, const std::uint8_t* b);
};
// world_stamp_sort.cpp. STLport's sort of the changed variables' nodes with EntryIdLess (the flow's
// update 005800C0): 00589160 (RET 0xC) __introsort_loop, 00589240 (RET 4) __final_insertion_sort;
// first in ECX, last in EDX, then a dummy value-type pointer, the depth limit and the comparator.
void __fastcall StampIntroSort(void** first, void** last, int valueType, int depthLimit, const void* comp);
void __fastcall StampInsertionSort(void** first, void** last, const void* comp);

// Game interface glue (00578000-00580000): small free functions the scripts and menus call.
// 0057F440. Whether a multiplayer client session (global at 0079B5D8) is active.
BOOL IsMultiplayerClient();
// 00578B40. Game clock: whole ticks + fraction.
float GameTime();
// 00578BA0. The local player's data block at +0x70.
std::uint8_t* LocalPlayerData();
// 00578A90. The local player's name (copy constructed into `out`, returned).
mfc::CString* __stdcall LocalPlayerName(mfc::CString* out);
// 00578B50. Float variable `name` of the local player as the client sees it.
float __stdcall LocalPlayerVariable(const char* name);
// 00578BE0. Float variable `name` of the local player: the client's view in multiplayer, player 0
// of the server otherwise (0 without that player).
float __stdcall PlayerZeroVariable(const char* name);

// The game flow object (007AFE20). Only the functions rewritten here are declared.
struct GameFlow {
    // 0057B3B0 (RET 4). Saves the game into saves\<slot>\ (single player only, 007B0098 == 1):
    // empties the folder, copies the visited zones' files from saves\current, writes scenario.sav
    // (the party records, input and scripts state) and, outside the cutscene zones, mission.sav
    // (the world, SaveWorld). Without scenario.sav the game goes back to the main menu. Returns 1
    // when saved.
    int SaveGame(const char* slot);
    // 0057F560 (StartServer, before the sessions; patched by Rebirth, so vanilla only; A-W14).
    // Fills the zone table (007AFDE0) once, while it is empty: for every "*.mq" file of the maps
    // folder, its title in lower case is registered as a quest folder (RegisterQuests) and the
    // quest texts loaded so far are unloaded again; a zone the map database knows gets an entry
    // {the title, the record's map name, the allod}: 0 / 1 / 2 by the three map name lists
    // (00790270 zone2..zone9, 00790298 zone12 / zone13, 007902C0 zone17 / zone18), else 3.
    void BuildZoneTable();
    // 0057F190 (each network step). The local player's "z.MPGame1" variable, set to 1 or
    // removed: on a client, set unless the client session's +0x25EC is active; on the host, set
    // when more than one player is in (with +0x80) or unless the client's +0x25F4 is active.
    // Nothing without an active client session.
    void UpdateMultiplayerVariable();
    // 0057E6B0 (RET 4). The multiplayer start zone for the client session's game type (+0x261C):
    // "bz2mpg".."bz4mpg" for 1..3, else "bz1mpg"; constructed into `out`, returned.
    mfc::CString* MultiplayerZoneInto(mfc::CString* out);
    // 0057F280. Starts a multiplayer game: the party manager (007AA5A0) is reset, a single party
    // record gets the unit id 0x3B9AF5CE, the party outside a game (007C82B0) is sent to the start
    // zone with "z.MPGame1" = 1 and redeployed; the flow loads that zone (state 4) and the
    // client's game state request becomes kind 1.
    void BeginMultiplayerGame();
    // 0057FF40 (RET 4; world_briefing.cpp). The local player has seen `briefing`: for a name
    // ending in "_N" the zone's quests are loaded first (005809D0 with the name without "_N"),
    // then BriefingVariableInto's variable is set to 1.
    void MarkBriefing(const char* briefing);
    // 0057F0F0. With an active client session: the local player's party is copied into the party
    // outside a game (007C82B0), redeployed by the party manager, its map transfer sent
    // (00662AC0 with 007C82A0), then the flow's update (005800C0).
    void ReturnLocalParty();
    // 0057F150. With an active client session: the flow's update, then an answered game state
    // request (kind 3) becomes a new one of kind 0.
    void UpdateAndAcknowledge();
    // 0057FB60 (RET 4). A multiplayer client asks the server to complete `briefing` (session
    // command 1).
    void RequestBriefing(const char* briefing);
    // 00580040 (RET 4; world_briefing.cpp). Removes the local player's variable of `briefing`.
    void ForgetBriefing(const char* briefing);
    // 0057F390. Replaces the game client session (0079B5D8) with a new one (0041BC40), releasing
    // the old one.
    void RecreateClientSession();
    // 00581610 (RET 4). SaveGame through its address (a thunk the menus call).
    int SaveGameThunk(const char* slot);
    // 00581660. Releases both network sessions (server 0079B5DC, then client 0079B5D8), clears
    // 007C7DD8 and sets the state to 1.
    void CloseSessions();
    // 0057ECA0 (the menus and the interface on leaving a network game). As CloseSessions without
    // the state: 007C7DD8 cleared, the server and the client session released.
    void DropSessions();
    // 0057ED00 (the hero screens). DropSessions, then the resources go back to the local database
    // (UseResources(false)).
    void EndSession();
    // 00580840 (RET 4; world_briefing.cpp). A player variable changed (event +8: the record,
    // name at +0x0C, value at +0x18): "q." variables go to the quest log (007C7D50) and the
    // client world.
    void OnVariableChanged(const void* event);
    // 005810A0 (RET 4; world_briefing.cpp). Registers a quest folder once: the flow loads its
    // quests (005809D0), then with "<folder>\quest.reg" in the texts the quest table reads it,
    // the map database loads "<folder>\map.txt" and the folder joins the list at 007AFDC0.
    void RegisterQuests(const char* folder);
    // 0057EE10 (world_game.cpp). The multiplayer zone switch, in two calls: the first (+0x80
    // clear) resets the host's connections, takes the party (player 0's records when its state
    // has +0x70, else the outside party) for the zone at +0x7C, asks for game state 3 and
    // requests state 6 (host) or 5 (client, to its peer's zone); the second (+0x80 set) asks for
    // state 1 and the start zone again, and the host drops its client.
    void SwitchMultiplayerZone();
    // 0057E740 (RET 0x18; world_game.cpp). Hosts a multiplayer game: new server (game type) and
    // client sessions, the client connects to 127.0.0.1 with the password (20 bytes) as hello,
    // then the server's name ("Server" when empty), +0x25E0, password, listing (not `unlisted`)
    // and +0x25FC are set; between 0057F560 and 0057EB10 (both patched by Rebirth).
    void StartServer(const char* name, int value25E0, int gameType, const char* password, char unlisted, int value25FC);
    // 0057EA50 (A-W13). Joins as a client only: the server session dropped, a new client session,
    // then 0057EB10.
    void StartClient();
    // 0057E970 (RET 8; `this` is not used). The client session joins the server at `address` with
    // the password (20 bytes, zero-padded) as its hello.
    void JoinServer(const net::NetAddress* address, const char* password);
    // The flow's requests (world_game.cpp): the state (+0) the flow's update acts on next, the
    // zone or save name (+4), +0x78 and, for 5 and 6, a copy of a party (+8, 00661300).
    void RequestState3();                                            // 005816C0
    void RequestLoad(const char* name, char flag);                   // 005816D0 (RET 8): 2, +0x78 = flag != 0
    void RequestZoneChange(const char* zone, int entrance);          // 00581700 (RET 8): 4, +0x78 = entrance
    void RequestPartyZone5(const char* zone, const void* party);     // 00581730 (RET 8)
    void RequestPartyZone6(const char* zone, const void* party);     // 00581760 (RET 8)
    // 0057C460 (RET 8). Starts `zone`: the current zone is torn down; mode 0 takes the mode from
    // the zone record's kind (0 -> 1, 1 -> 4, 2 -> 3), "endofgame" is mode 8, mode 2 loads the main
    // menu zone (also when the zone is unknown, after FatalError and closing the network
    // sessions); otherwise the party enters the zone (0057A060) and the clock follows its "gtime".
    // The mode becomes the campaign mode (007C2CC4). Rebirth's AllowScriptsInMainMenu sets the
    // server's +0x160 in the menu.
    void StartZone(const char* zone, int mode);
    // 0057BD10. A new game: "exec autoexec.cfg" on the console, the party manager's new game
    // (004A88A0), saves\current emptied, then the party's zone started (mode 0). Rebirth's
    // AutoExec runs addon's AutoExec lines first.
    void StartNewGame();
    // 0057C700. Acts on the pending request (+0, then cleared): the zone being left is saved
    // (0057A780), the interface closes its windows, the zone is torn down and the resource
    // database follows the network state; then 1 main menu, 2 load the save named at +4 (two
    // ticks first; autoplay with +0x78), 3 new game (StartNewGame), 4 zone +4, 5 / 6 multiplayer
    // zone changes with the party copy at +8. Ends with the network pump (004122D0).
    void ProcessRequest();
    // 0057DEE0 (RET 4; `this` is not used). The resource database follows `network`:
    // "databaseLMP.res" in a network game, else "database.res". When the name differs from the
    // last one (007AFDEC, "database.res" at first) the databases are switched (0057CB50, inlined in
    // the original) and the difficulty (0079BEC0) is saved to 0079BEC4 and cleared (network) or
    // taken back from it.
    void UseResources(bool network);
    // 0057ABA0 (RET 4). Loads the game saved in saves\<slot>\ (not in multiplayer, 007B0098 2 or
    // 3): "exec autoexec.cfg", saves\current emptied, scenario.sav read back (the party; by
    // version the input, scripts and globals), the visited zones of the current map copied back
    // (the others marked not visited), then the zone: from mission.sav (kinds 0, 2) or started
    // afresh. Without scenario.sav the main menu starts. Returns 1 when loaded. Rebirth's AutoExec
    // runs addon's AutoExec lines first.
    int LoadGame(const char* slot);
    // 0057EB10. After the sessions start: the party step 0057DEE0(1), the variables of the party
    // outside a game (007C82B0 + 0x54) emptied, its members reset (00661FB0), and the client's host
    // record (session +0x2600) takes the hero's and party's names (+0x18, +0xC; defaults "Human
    // Hero", "Bug"), +0x28 = 1. Rebirth's PersistentGlobalVars keeps the "gvar." variables.
    void AfterServerStart();
    // 005800C0. A multiplayer client's update from the server: the quest texts unloaded, the
    // local player's variables replaced ("constr.current" kept), the zone's quests, the game type's
    // construction variable and briefings (at least 1), the seen briefings, the quest folders of
    // "q." variables, then the server's variables applied in stamp order (007AFED0 / 007AFED4),
    // and the multiplayer flag variable. Rebirth's PersistentGlobalVars keeps the "gvar." ones.
    void UpdateFromServer();

    // The party records of a request (PartyRecords, unit.h; RequestPartyZone5 / 6 copy one in).
    PartyRecords* RequestParty() { return reinterpret_cast<PartyRecords*>(requestParty_); }
    // The same bytes as the party object they are (party.h): the constructor and destructor run on it.
    class CParty* RequestPartyObject() { return reinterpret_cast<class CParty*>(requestParty_); }

    // 00578460 / 005784D0 (A-W13; the static initialiser of the flow 007AFE20 and its atexit
    // function call them). The constructor body: empty names, the party constructed (00661050), no
    // request; returns this. The destructor body: the names and the party (006611D0).
    GameFlow* Construct();
    void Destruct();
    // 0057ED60 (RET 4; the session messages 004186F0 / 00418CC0). saves\current emptied, the
    // multiplayer zone (+0x7C) named; a host without an active client session starts it (0057BDF0)
    // when there is a name.
    void SetNetworkZone(const char* zone);

    int state_;                           // +0x00 the pending request (1..6; ProcessRequest runs it)
    char* requestTarget_;                 // +0x04 CString: the request's zone or save name
    std::uint8_t requestParty_[0x70];     // +0x08 PartyRecords: RequestParty()
    int requestArg_;                      // +0x78 the entrance (4) or the load flag (2)
    char* zone7C_;                        // +0x7C CString: the multiplayer zone (SwitchMultiplayerZone)
    char flag80_;                         // +0x80 set by SwitchMultiplayerZone, cleared by the game's start
};
static_assert(offsetof(GameFlow, requestTarget_) == 0x04);
static_assert(offsetof(GameFlow, requestArg_) == 0x78);
static_assert(offsetof(GameFlow, flag80_) == 0x80);
inline GameFlow* TheGameFlow() { return reinterpret_cast<GameFlow*>(std::uintptr_t(0x007AFE20)); }

struct SavedPartyEntry;   // world_game.cpp: the party records (0x70 bytes) and the file name

// The saved multiplayer parties: the folder and an STLport vector of 0x74-byte entries.
struct SavedParties {
    // 00662FB0 (world_game.cpp). Rebuilds the list from the folder's "*.mp" files: each is read
    // into party records (00662DD0); a loaded one has its variables emptied and, with records +0x34
    // == 1, is appended. Rebirth's PersistentGlobalVars keeps the "gvar." variables.
    void Reload();
    // 006632B0 (party_file.cpp). With a folder name: the folder created and emptied (0057AAB0),
    // then every entry written into it (MpHeroRecord::WriteFile, 00662AC0).
    void SaveAll();
    // 0065F2D0 (party_file.cpp, A-H17; the static destructor 0065F2C0 at exit). The party outside a
    // game (+0x10) as an MpHeroRecord (file name, then the party), every entry, the vector's
    // storage back to the node allocator, the folder name.
    void Destruct();
    // 0065F580 (party_file.cpp; the static initialiser 0065F2A0). An empty folder name and vector,
    // the party outside a game as an MpHeroRecord (the party 00661050, an empty file name). Returns
    // this.
    SavedParties* Construct();

    char* folder_;               // +0x00 CString
    SavedPartyEntry* entries_;      // +0x04 vector<SavedPartyEntry>: start, finish, end of storage
    SavedPartyEntry* finish_;       // +0x08
    SavedPartyEntry* eos_;          // +0x0C
    // +0x10 the party outside a game (CParty, 0x70 bytes; 007C82B0) and a file name: an MpHeroRecord
    // (ui_screens.h).
    std::uint8_t outsideGame_[0x74];
};
static_assert(offsetof(SavedParties, outsideGame_) == 0x10);
static_assert(offsetof(SavedParties, entries_) == 0x04);
static_assert(offsetof(SavedParties, eos_) == 0x0C);

// 00581630 (RET 8; world_game.cpp). Sets 007C7D94 and has the local view ask to go to `zone`
// (0066B1B0); returns what that returns.
int __stdcall RequestZone(const char* zone, int entrance);

// 00580F10 (world_briefing.cpp). With the briefing texts loaded (007AF788): every quest text file
// loaded so far (007AFDD0) is unloaded and the list emptied.
void UnloadQuestTexts();

// 005809D0 (RET 4, AL; ECX, the flow, is not read; world_briefing.cpp, A-W15). Loads the quest
// texts of `zone`: false without the briefing texts (007AF788) or when the maps folder has no
// "<zone>.mq" (CFileFind). A running game server (0079B5DC) first lists the file's bytes under
// "<zone>.mq" for its clients (ShareFile); then a path not yet among the quest files (007AFDD0)
// joins them and the briefing texts add it as a source. Rebirth patches it: hooked in the vanilla
// launch only, and rewritten callers go through FlowLoadQuestsOriginal (originals_world.h).
bool __stdcall LoadZoneQuests(const char* zone);

// 0057F820 (ECX = list, EDX = name; world_game.cpp). Whether `name` is in `list` (an array of
// names ended by an empty one), compared case-insensitively.
bool __fastcall NameInList(const char* const* list, const char* name);

// 0057AAB0 (ECX = path ending in '\'; world_game.cpp). Creates the folder and deletes every file
// in it (CFileFind "*.*"; CreateDirectoryA / DeleteFileA through game.exe's imports).
void __fastcall ClearSaveFolder(const char* path);

// 0057F850 (RET 0xC; world_game.cpp). For the zone table entries (007AFDE0) with `id`, grouped by
// their second name in order of first appearance: one random entry per group (never the one
// whose first name is `exclude` when there is a choice; one IsaacRandom draw per group) has its
// first name appended to `out` (an STLport vector of strings).
void __stdcall PickZones(void* out, int id, const char* exclude);

// 00579E60 (RET 4; world_game.cpp). Reads mission.sav from a file stream (magic 0x102) and
// restores the world (LoadSave); old versions also carry the globals (< 0x52) and the input
// state (< 0x47). 0 when the file does not read.
int __stdcall LoadMission(void* file);

// world_savefile.cpp. 00579B80 (ECX = file stream, EDX = magic; RET 8): writes the header
// (004A5340), at version > 0x6B a checksum, then a flag byte and the LZ-compressed data (or, when
// decompressing it does not give the data back, 0 and the raw data). 00579830 (ECX = memory
// stream, EDX = magic; RET 0xC; AL): reads one back (versions up to 00790264 uncompressed);
// false on a bad header or checksum.
void __fastcall SaveFileWrite(void* file, unsigned magic, int version, void* data);
bool __fastcall SaveFileRead(void* out, unsigned magic, int minimum, int maximum, void* file);
// 00790264: the newest save version stored uncompressed (SaveFileRead reads those as they are).
inline int SaveLastUncompressedVersion() { return *reinterpret_cast<const int*>(0x00790264); }

// world_briefing.cpp. 0057FC90 (RET 8): the mercenary who speaks `briefing` (the second word of
// the first "#show" line of section "briefing <name>" that is not "Hero"), or "", constructed
// into `out`. 0057FDE0 (RET 8): the briefing's variable, "b.<speaker>.<briefing>" or, without a
// speaker, "b.elder.<briefing>".
mfc::CString* __stdcall BriefingSpeakerInto(mfc::CString* out, const char* briefing);
mfc::CString* __stdcall BriefingVariableInto(mfc::CString* out, const char* briefing);
// 005814C0 (RET 4). The multiplayer peer's map (the client's +0x25F4 or the server's +0x25A8,
// name at +0x0C), replaced by the first line of its "quest <name>" section when there is one;
// "" without a peer.
mfc::CString* __stdcall SessionMapNameInto(mfc::CString* out);
// 00595080. Chooses the loading movie (the name at 00791EE0) for zone `zone` (may be null) by its
// allod in the map database: progres2.bik for gipat, progres3.bik for ingos, progres4.bik for
// suslanger, else progres.bik (Rebirth's FixProgressNames: progres2.bik for gipat2, progres5.bik for
// jigran); the zones gz1g and bz7g (case-insensitive) take progres1.bik.
void __stdcall SelectLoadingMovie(const char* zone);

CWorldServer* WorldServer();   // 007AFF60
// 0079BE5C: the time speed setting (the options' value; a negative speed asks for it).
inline int TimeSpeedSetting() { return *reinterpret_cast<const int*>(0x0079BE5C); }
// The display options' distances: 0079BED4 the fog start by day, 0079BED0 at night, 0079BE54
// the far clip (the transform pipeline's depth range, 00500B70 / 00566360).
inline float FogStartDay() { return *reinterpret_cast<const float*>(0x0079BED4); }
inline float FogStartNight() { return *reinterpret_cast<const float*>(0x0079BED0); }
inline float FarClipDistance() { return *reinterpret_cast<const float*>(0x0079BE54); }
CWorldClient* WorldClient();   // 007AF5B8

// The client's per-frame draw lists (world_client_view.cpp files the visible objects in them,
// world_client_draw.cpp sorts, draws and empties them): STLport vectors of object pointers, the
// globals 007AF508 + 0x10 * list, in this order. The deferred unit list follows at 007AF5B4 (an
// STLport list of counted object pointers; nodes of 12 bytes).
enum class FrameList {
    TouchedModels,   // 007AF508 models to keep loaded (by last use)
    Shadow52,        // 007AF518
    ShadowUnits,     // 007AF528
    Shadow5D,        // 007AF538
    ShadowOther,     // 007AF548
    Shadow54,        // 007AF558
    Visible52,       // 007AF568
    VisibleUnits,    // 007AF578
    Visible5D,       // 007AF588
    VisibleOther,    // 007AF598
    Visible54,       // 007AF5A8
};
inline stlp::vector<CEngineObject*>& FrameObjects(FrameList list) {
    return *reinterpret_cast<stlp::vector<CEngineObject*>*>(std::uintptr_t(0x007AF508) + 0x10u * static_cast<std::uintptr_t>(list));
}
inline stlp::list<CEngineObject*>* FrameDeferredUnits() {
    return reinterpret_cast<stlp::list<CEngineObject*>*>(std::uintptr_t(0x007AF5B4));
}
// 007AF63C: the id the next object only the client makes gets (client figures such as a bow
// shot's arrow); each use counts it down.
inline int& NextClientObjectId() { return *reinterpret_cast<int*>(0x007AF63C); }
// 007AFFB4: the world server's item manager (the item factory, CWorld slot 18 / itemManager_).
inline CEngineObject* WorldItemManager() { return *reinterpret_cast<CEngineObject* const*>(0x007AFFB4); }

// 00589CA0 (thiscall on a shared-object entry, RET 4; world_save.cpp). Reads an entry of the
// shared-object list: id, player, value, then object ids resolved by the item manager (007AFFB4);
// each found object is appended with a reference.
void __fastcall LoadSharedObject(void* entry, void*, WorldStream* stream);

// 00578E30 (thiscall on the client glue 007AFEA4, unused; RET 0x18; world_weather.cpp). A
// lightning strike from `from` to `to`, as a script line on the queue at 007C29CC: one time in
// four only a thunder sound, otherwise a point light at the middle, a sound and two bolts.
void __fastcall ShowBolt(void* glue, void*, Vector3 from, Vector3 to);

// world_guard.cpp. 005790E0 (thiscall on the client glue 007AFEA4, unused; RET 4): with `plain`
// 0 the players' gold, party records (0x704 bytes from +0x20) and, on the server, their units'
// controller statistics (0x704 bytes from +0x1E0) are XORed with a new random key (007AFECC),
// with 1 they are XORed back; also the party records outside a game (007C82B0). Nothing when
// the state (0079026C) is already `plain`. 005791C0 (RET 4) does one player list.
void __fastcall GuardPlayerData(void* glue, void*, char plain);
void __fastcall GuardPlayers(CEngineObject* players, char units, std::uint32_t key);

// ---- Client draw lists and their sorting (world_client_view.cpp) --------------------------------
// 0056AFE0 (the client's draw pass, patched by Rebirth) fills per-frame vectors of object pointers
// with CWorldClient::CollectVisible and sorts some of them with STLport's sort, instantiated for
// two orderings. The entry points keep STLport's internal calling convention: first in ECX, last
// in EDX, then a dummy value-type pointer, the depth limit and the (empty) comparator on the stack.

// 005740D0 (RET 0xC). STLport __introsort_loop with UnitOrderLess (model rank, +0xF0, view depth).
void __fastcall IntroSortByModel(CEngineObject** first, CEngineObject** last, void* valueType, int depthLimit,
                                 int comp);
// 00574390 (RET 4). STLport __final_insertion_sort with UnitOrderLess.
void __fastcall FinalSortByModel(CEngineObject** first, CEngineObject** last, int comp);
// 00573D50 (RET 0xC). __introsort_loop ordering objects by their model's last use (+0x94 -> +0xEC),
// most recent first.
void __fastcall IntroSortByModelUse(CEngineObject** first, CEngineObject** last, void* valueType, int depthLimit,
                                    int comp);
// 00573F10 (RET 4). __final_insertion_sort with the same ordering.
void __fastcall FinalSortByModelUse(CEngineObject** first, CEngineObject** last, int comp);
// 00573EE0. The comparator of that ordering: b's model used before a's (0/1 in EAX).
struct ModelUseLess {
    BOOL Less(CEngineObject* a, CEngineObject* b);
};

// 005679A0 (ECX manager, EDX stream; world_client_maps.cpp, A-W13). Reads objects up to a zero
// byte: after each mark an id, a class and the object's +0x18; an object registered under the id
// is released first (its count raised to 1 when 0), then a new one is created (slot 23), loaded
// (slot 9) and referenced. 1 at the end mark, 0 when a mark is read from a failed stream.
int __fastcall LoadManagerObjects(CBaseManager* manager, CStream* stream);
// 0055E8A0 (ECX list, EDX stream; world_client_maps.cpp). Writes what LoadManagerObjects reads:
// per listed object a 1, its id, type and +0x18, then its own Save (slot 10); a 0 at the end.
void __fastcall SaveListedObjects(mfc::CList<CEngineObject*>* list, CStream* stream);

// 0057BC30 (world_game.cpp, A-W13; the save screens). 1000000 plus the sizes (low dwords) of the
// files in saves\current.
int CurrentSaveSize();

// 0057CB50 (ECX: the database's file name; world_game.cpp, A-W13). Every resource database destroyed
// and constructed again in place (the compiler-generated resets 0057CBE0, 0057CD60, 0057CE80,
// 0057D340) and loaded from "<resources (0079BC94)><name>": the units (0053A200, then their sound
// file counts, 0053A8E0), the skills (00520220), the spell tables (00684550), the items (006A67A0).
void __fastcall SwitchResourceDatabases(const char* name);
