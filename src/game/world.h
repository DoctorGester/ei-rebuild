#pragma once

// The world: the server side that runs the game rules (CWorldServer, the global at 007AFF60) and
// the client side that draws it and answers terrain queries (CWorldClient, the global at 007AF5B8).
// Both derive from a common base whose methods live at 0055E000-0055FFFF. Class names come from
// the game's error strings ("CWorldServer::LoadMap", "CWorldClient::ToggleWireframe"); everything
// else is provisional. Only the members used by the rewritten functions are declared.

#include <cstddef>
#include <cstdint>

#include "engine_object.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "maths.h"

using BOOL = int;

struct ScriptGroup;
class Terrain;

namespace world {
constexpr int kClassUnit = 0x50;   // CEngineObject::GetClassId() of units
}

// A player (one per human or AI side). Fields named only as far as used here.
struct WorldPlayer {
    std::uint8_t unknown_000[0x24];
    int id;                              // +0x024 player number (0 = the local player)
    std::uint8_t unknown_028[0xA0 - 0x28];
    std::uint8_t* mercs;                 // +0x0A0 0x784-byte records, unit id at +0x1C
    int mercCount;                       // +0x0A4
    std::uint8_t unknown_0A8[0xC4 - 0xA8];
    std::uint8_t variables[0x110 - 0xC4]; // +0x0C4 named float variables ("gtime", quest flags)
    int lastTick;                        // +0x110
    std::uint8_t unknown_114[0x130 - 0x114];
    std::uint32_t dirty;                 // +0x130 changes still to send to this player
};
static_assert(offsetof(WorldPlayer, mercs) == 0xA0);
static_assert(offsetof(WorldPlayer, variables) == 0xC4);
static_assert(offsetof(WorldPlayer, dirty) == 0x130);

// Walks the world's player list. The list object is reference counted (+4) while an iterator holds
// it; its MFC list head is at +0x38 and each node's data (+8) is a WorldPlayer.
struct PlayerIterator {
    CEngineObject* list = nullptr;
    void* next = nullptr;

    WorldPlayer* First(CEngineObject* players);   // 00404920
    WorldPlayer* Next();                          // 00404960
    // The original inlines this: drop the list's count without releasing it.
    ~PlayerIterator() {
        if (list) --list->refCount_;
    }
};
static_assert(sizeof(PlayerIterator) == 8);

// hash_map<unsigned, CEngineObject*> plus the highest key ever inserted (several instantiations
// share this shape: 005999E0 / 00599BD0 are its Set).
struct ObjectIdMap {
    stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash> map;   // +0x00
    int maxId;                                                      // +0x14

    // 005999E0 and 00599BD0 (identical code, different template instantiations): map[key] = value
    // and raise maxId.
    void Set(const unsigned& key, CEngineObject* const& value);
};
static_assert(sizeof(ObjectIdMap) == 0x18);

// A zone exit as the world server keeps it (0x3C bytes); the client keeps the same records.
struct ZoneExit {
    std::uint8_t unknown_00[0x14];
    float x1, y1, x2, y2;                 // +0x14 rectangle
    std::uint8_t unknown_24[4];
    char* target;                         // +0x28 CString: target zone ("" = none)
    int entrance;                         // +0x2C entrance number in the target zone
    std::uint8_t unknown_30[0x3C - 0x30];

    // 004E59F0. Whether (x, y) lies inside the exit area; not rewritten.
    bool Contains(float x, float y) const;
};
static_assert(sizeof(ZoneExit) == 0x3C);
static_assert(offsetof(ZoneExit, target) == 0x28);

// Members both world classes share (base class, 0055E000-0055FFFF).
class CWorld : public CEngineObject {
public:
    float timeBase_;                      // +0x1C
    float secondsPerTick_;                // +0x20
    std::uint8_t unknown_24[0x38 - 0x24];
    CEngineObject* players_;              // +0x38 player list (vtable slot 20 returns it)
    std::uint8_t unknown_3C[0x64 - 0x3C];
    int tick_;                            // +0x64 game time in ticks
};
static_assert(offsetof(CWorld, players_) == 0x38);
static_assert(offsetof(CWorld, tick_) == 0x64);

// The game rules side (vtable 0073F928).
class CWorldServer : public CWorld {
public:
    // 00593AA0, vtable slot 40. World object with this id, or null.
    CEngineObject* FindObject(unsigned id);
    // 00594180. Player number `id`, or null.
    WorldPlayer* GetPlayer(int id);
    // 00594220, vtable slot 27. Ground height from the server's coarse height grid; 0 outside.
    float GridHeight(float x, float y);
    // 00591740. Living mercenaries of player `id`, not counting the leader (0 without the player).
    int CountLivingMercs(int id);
    // 005949D0. Whether object `objectId` is on the shared-object list for player `playerId` (or
    // for everyone). In multiplayer the player is looked up on the game server.
    bool IsObjectShared(int objectId, int playerId);
    // 00594A30. Fills `group` with the objects on that list for player `playerId`.
    void CollectSharedObjects(ScriptGroup* group, int playerId);
    // 0058B6D0. ORs `mask` into every player's dirty flags.
    void MarkPlayersDirty(std::uint32_t mask);
    // 0058CC70 / 0058CC80 / 0058EE00 / 005942C0. Setters (weather and similar map settings).
    void SetUnknownE0(int value);
    void SetUnknownDC(int value);
    void SetUnknownE4E8(int e4, float e8);
    void SetUnknown6C(std::uint8_t value);
    // 0058E4C0. Target zone and entrance of exit `index`; FALSE when out of range.
    BOOL GetZoneExit(int index, mfc::CString* target, int* entrance);
    // 0058E520. Registers an object that lives for a limited time (spells, effects): stamps the
    // current tick, adds a reference, marks it new and puts it in timedObjects_.
    void AddTimedObject(CEngineObject* object);
    // 0058E5D0. Flags object `id` deleted, releases it and removes it from timedObjects_.
    void RemoveTimedObject(unsigned id);
    // 0058D5D0. Puts a unit in the update map (+0x104) and sets its update slot.
    void AddToUpdateMap(CEngineObject* unit);

    std::uint8_t unknown_068[0x6C - 0x68];
    std::uint8_t unknown6C_;              // +0x6C
    std::uint8_t unknown_06D[0xDC - 0x6D];
    int unknownDC_;                       // +0x0DC
    int unknownE0_;                       // +0x0E0
    int unknownE4_;                       // +0x0E4
    float unknownE8_;                     // +0x0E8
    std::uint8_t unknown_0EC[0xF0 - 0xEC];
    ZoneExit* exits_;                     // +0x0F0
    int exitCount_;                       // +0x0F4
    std::uint8_t unknown_0F8[0x104 - 0xF8];
    ObjectIdMap updateMap_;               // +0x104
    std::uint8_t unknown_11C[0x290 - 0x11C];
    float gridHeightScale_;               // +0x290
    std::uint8_t unknown_294[0x280C - 0x294];
    std::uint8_t** gridRows_;             // +0x280C rows of 9-byte cells, height (u16) at +4
    int gridWidth_;                       // +0x2810
    int gridHeight_;                      // +0x2814
    std::uint8_t unknown_2818[0x12D70 - 0x2818];
    ObjectIdMap objects_;                 // +0x12D70 every world object by id
    std::uint8_t unknown_12D88[0x12DE8 - 0x12D88];
    ObjectIdMap timedObjects_;            // +0x12DE8
    std::uint8_t unknown_12E00[0x12E64 - 0x12E00];
    void* sharedObjects_;                 // +0x12E64 head of an MFC list of {object id, player}
};
static_assert(offsetof(CWorldServer, exits_) == 0xF0);
static_assert(offsetof(CWorldServer, updateMap_) == 0x104);
static_assert(offsetof(CWorldServer, gridHeightScale_) == 0x290);
static_assert(offsetof(CWorldServer, gridRows_) == 0x280C);
static_assert(offsetof(CWorldServer, objects_) == 0x12D70);
static_assert(offsetof(CWorldServer, timedObjects_) == 0x12DE8);
static_assert(offsetof(CWorldServer, sharedObjects_) == 0x12E64);

// One walkable platform (bridge, deck) the client tests positions against. 0x4C bytes, stored in
// an MFC-style singly linked list (next pointer first, then the record).
struct Platform {
    std::uint8_t unknown_00[4];
    float x, y;                           // +0x04 centre of the bounding circle
    std::uint8_t unknown_0C[4];
    float radius;                         // +0x10
    float origin[3];                      // +0x14
    float axisU[3];                       // +0x20 local u axis
    float axisV[3];                       // +0x2C local v axis
    float normal[3];                      // +0x38
    float sizeU, sizeV;                   // +0x44 extent along u and v
};
static_assert(sizeof(Platform) == 0x4C);

struct PlatformNode {
    PlatformNode* next;
    std::uint8_t unknown_04[4];
    Platform platform;                    // +0x08
};

// The client world's platform set (CWorldClient + 0x1D4).
struct PlatformSet {
    // 00560F00. max(height, z of every platform surface whose area contains the point); the
    // point is projected onto each platform plane along z.
    float SurfaceHeight(const Vector3* point, float height);
    // 00561050. Whether (x, y) lies on some platform (with a 0.1 margin).
    bool Contains(float x, float y);

    std::uint8_t unknown_00[0x20];
    PlatformNode* head;                   // +0x20
};

// The drawing side (vtable 0073F41C).
class CWorldClient : public CWorld {
public:
    // 00563F70, vtable slot 27. Ground height with the position clamped into the map.
    float ClampedGroundHeight(float x, float y);
    // 005640A0. Nearest living unit of the local player's group that `unit`'s side can see
    // (relation masks +0x244/+0x248 of the candidate); null when none or `unit` is null.
    CEngineObject* NearestVisibleUnit(CEngineObject* unit);
    // 00564AF0. Item `index` of the concatenation of two pointer arrays, at +0x188 of each.
    void* ListItem(int index);
    // 005699F0. The local view (session +0x20; its +0x1C is the local WorldPlayer), or null.
    std::uint8_t* LocalView();
    // 00566EC0. Whether the local view is in mode 2 (+0x20).
    bool IsLocalViewMode2();
    // 00566EF0. FALSE without a local view or while the local player's controller is busy.
    bool IsLocalPlayerIdle();
    // 00567440. Index of the exit whose area contains `point` and that leads somewhere.
    BOOL FindExitAt(const float* point, int* index);
    // 005674C0. Centre of exit `index`'s rectangle.
    float* ExitCentre(float* out, int index);   // returns out
    // 0056D9F0. Flags describing the current environment for `unit`: unit state (+0x268), island
    // (gipat/ingos/suslanger), night (0x100) or day (0x80), and two world settings.
    std::uint32_t EnvironmentFlags(CEngineObject* unit);

    std::uint8_t unknown_068[0x90 - 0x68];
    ZoneExit* exits_;                     // +0x090
    int exitCount_;                       // +0x094
    std::uint8_t unknown_098[0xD0 - 0x98];
    std::uint8_t** listA_;                // +0x0D0
    int listACount_;                      // +0x0D4
    std::uint8_t unknown_0D8[0xE0 - 0xD8];
    std::uint8_t** listB_;                // +0x0E0
    std::uint8_t unknown_0E4[0x180 - 0xE4];
    Terrain* terrain_;                    // +0x180
    char* zoneName_;                      // +0x184 CString
    std::uint8_t unknown_188[0x1D4 - 0x188];
    PlatformSet platforms_;               // +0x1D4
    std::uint8_t unknown_1F8[0x20C - 0x1F8];
    std::uint8_t* session_;               // +0x20C local session; its +0x20 is the local view
    std::uint8_t unknown_210[0x6E0 - 0x210];
    int unknown6E0_;                      // +0x6E0
    std::uint8_t unknown_6E4[0x7A8 - 0x6E4];
    int unknown7A8_;                      // +0x7A8
};
static_assert(offsetof(CWorldClient, exits_) == 0x90);
static_assert(offsetof(CWorldClient, terrain_) == 0x180);
static_assert(offsetof(CWorldClient, platforms_) == 0x1D4);
static_assert(offsetof(CWorldClient, session_) == 0x20C);
static_assert(offsetof(CWorldClient, unknown7A8_) == 0x7A8);

// 00560E10. out = rows * v for the three rows of a 4x4 matrix (row stride 16 bytes). Returns out.
struct MatrixRows {
    float m[12];
    Vector3* Transform(Vector3* out, const Vector3* v) const;
};

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

CWorldServer* WorldServer();   // 007AFF60
CWorldClient* WorldClient();   // 007AF5B8
