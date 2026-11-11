#pragma once

// A world player (Player): its units, the objects its client knows, the given items and the connection to its
// client (player_messages.h: PlayerConnection, PlayerServer). Members in player.cpp. See docs/notes/objects.md
// ("Players").

#include "engine/core/engine_object.h"
#include "game/players/party.h"

#include "lib/bit_flags.h"
#include "lib/hash_map.h"

#include <cstdint>
#include <vector>

class Item;
class Unit;
class MapObject;
class Effect;
class PlayerConnection;
class PlayerServer;
namespace netlink {
class NetLink;
}

// The bits of Player::dirty: the state the server still has to send the player (WorldServer::SyncPlayer),
// each cleared by its message. kPlayerDirtyAll after a reset or a load.
enum class PlayerDirty : std::uint32_t {
    Zone = 0x1,                 // message 0x02
    Weather = 0x2,              // 0x70
    Time = 0x4,                 // 0x71: the time and its speed
    Wind = 0x8,                 // 0x72
    GameState = 0x10,           // 0x07: the session kind and the tick (set when the player joined)
    Countdowns = 0x20,          // 0x73: the water tweens
};
template <> inline constexpr bool lib::kBitFlags<PlayerDirty> = true;
inline constexpr std::uint32_t kPlayerDirtyAll = 0xFFFFFFFFu;

// A world player (Player). The harness's digest reads its fields (tests/verify/harness_digest.cpp), hence the
// layout asserts.
class Player final : public EngineObject {
public:
    // The world objects the player knows, by id, each counted once: the map objects and the timed objects
    // (effects).
    // Walked in STLport's order: the units by the party's loops (combat_motion, object_diplomacy), the known objects by
    // the briefing's people list, the known timed objects by ReleaseEach (each release can destroy the effect).
    using MapObjectMap = lib::HashMap<int, MapObject*>;
    using TimedObjectMap = lib::HashMap<int, Effect*>;
    using UnitMap = lib::HashMap<int, Unit*>;

    Player();
    ~Player() override;

    void Create() override {
        CreatePlayer();
    }
    void Destroy() override { DestroyPlayer(); }
    const char* ClassName() override;
    ClassId GetClassId() override;

    void CreatePlayer();
    void DestroyPlayer();
    // Empties the given items, both known-object maps, the units (each taken away; the walk stops at the
    // first null) and the recharge list, then the connection's ForgetSentObjects.
    void Clear();
    // The old connection loses its owner and a reference; the new one gets this player as owner and a
    // reference.
    void SetConnection(PlayerConnection* connection);
    void SetPlayerId(int id);
    // With a link the connection leaves; Clear; the old link disconnected and released; `link` taken
    // (counted); then the connection's ResetState.
    void Connect(netlink::NetLink* link);
    // As Connect without the new link and the ResetState call.
    void Disconnect();
    // Both known-object maps cleared, the connection's ForgetSentObjects, dirty kPlayerDirtyAll.
    void ForgetKnownObjects();

    // Whether `unit` is the party's leader: there are records and its id is the first record's key. 0 for
    // null.
    bool IsLeader(const EngineObject* unit);
    // The connection's ItemsGiven, then every item of `items` counted and appended to the given items.
    void GiveItems(const std::vector<Item*>& items, int flag);
    // The connection's ItemsTaken, then every given item taken off the head and released.
    void TakeAllItems();
    // 1 when a given loot item (ItemKind::Quest) has `kind` (ItemLoot::kind), or an item a unit of the player
    // carries has it (any class; the walk stops at a null unit).
    bool HasLoot(int kind);
    // The first given loot item of `kind` / item with id `id`: the connection's ItemTaken, out of the list,
    // released; 1, or 0 when there is none.
    bool TakeLoot(int kind);
    bool TakeItem(int id);
    // A unit not yet in units is counted, raises the max id and is put in; then the connection's
    // UnitChanged. Nothing for null or a known unit.
    void AddUnit(Unit* unit);
    // A unit in units is erased, released and given to the connection's UnitChanged.
    void RemoveUnit(Unit* unit);
    // The item's id joins the recharge list unless there; the connection's ItemUsed.
    void ItemUsed(Item* item);
    // The recharge list emptied, then the party assigned from `source` (Party::Assign's result).
    Party* AssignParty(Party* source);
    // Every unit at a taming stage leaves the player; the walk stops at a null entry.
    void ReleaseTamedUnits();

    // Ticks the recharging items (RunTick; found through the party's item manager), dropping those gone or
    // whose tick returned 0, then the connection's TickDone.
    void Tick();

    Party* GetParty() { return &party; }
    // The connection as the server's view of it (a PlayerServer on the server world, null for none).
    PlayerServer* Server() const;

    netlink::NetLink* link = nullptr;           // the connection to the player's client, null: none
    PlayerConnection* connection = nullptr;    // PlayerServer / PlayerClient (counted)
    int playerId = -1;                          // player number (0 = the local player)
    MapObjectMap knownObjects;                  // map objects sent to the client
    int maxObjectId = 0;                        // highest id put in knownObjects
    UnitMap units;                              // the player's units by id (counted)
    int maxUnitId = 0;                          // highest id put in units
    TimedObjectMap knownTimed;                  // timed objects (effects) sent to the client
    int maxTimedId = 0;
    Party party;
    std::vector<Item*> items;          // items given to the player (counted)
    int partyId = 0;                            // the party's key in the party manager; sent with the client's login
    int sessionKind = 0;                     // 3: the client runs a setup on the controllers of new units
    std::uint32_t lastScreen = 0;               // the session kind before the last game-state message (5: the camp)
    std::uint8_t godMode = 0;                   // the script's GodMode(player, on); the combat code tests it
    int logTraffic = 0;                         // logs the size of every flush
    int lastTick = -1;                          // -1 after Create; the world tick last saved or restored for the player
    std::vector<std::uint32_t> recharging;     // ids of items recharging (saved with the world from version 0x55)
    std::uint32_t dirty = 0;                    // PlayerDirty bits
};
