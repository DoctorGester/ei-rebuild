#pragma once

// The server side of a player's connection (PlayerServer) as far as the message writers use it, and the
// party record lookup. Each writer appends one message to the outgoing stream: a type byte, then the payload.
// The single-player game runs the same server / client pair, so the per-tick player state, script variables
// and unit events all go through them. See docs/notes/objects.md.

#include "ack_kind.h"
#include "engine_object.h"
#include "lib/vecmath.h"
#include "net_delta.h"
#include "party_storage.h"
#include "script.h"
#include "water_tweens.h"

#include "lib/bit_flags.h"
#include "lib/containers.h"
#include "lib/hash_map.h"

#include <cstddef>
#include <cstdint>
#include <vector>

class Player;
class Item;
class Unit;
class MapObject;
class Effect;
struct PathOrder;
namespace netlink {
class NetLink;
}

// The messages a PlayerServer sends its client, by the type byte that starts each one (PlayerClient's
// DispatchMessages switches on them). The numbers are the wire format.
enum class ServerMessage : std::uint8_t {
    LoginAccepted = 0x00,       // the player's id; answers ClientMessage::Login
    Disconnect = 0x01,          // our server writes it when a player leaves
    Zone = 0x02,
    ItemsAndSpells = 0x03,      // no sender here
    CameraPlacement = 0x04,
    NewItemsAndSpells = 0x05,   // the states of the given items and spells (ItemsGiven)
    ItemsReceived = 0x06,
    GameState = 0x07,
    ZoneChange = 0x08,
    GameLost = 0x09,
    ItemRemoved = 0x0A,
    UnitSpeech = 0x0B,
    ObjectValues = 0x0C,        // no sender here
    Jingle = 0x0D,
    PathMarker = 0x0E,
    FreeCamera = 0x0F,
    Error = 0x11,               // a text; the client has no handler (it drops the link)
    WorldValue = 0x12,
    StringEvent = 0x13,
    ItemValue = 0x14,
    ExperienceAll = 0x15,
    WeaponRemoved = 0x16,
    QuestItemsChanged = 0x17,
    Experience = 0x18,
    UnitControl = 0x19,
    ObjectAdded = 0x40,
    ObjectChanged = 0x41,
    ObjectRemoved = 0x42,
    UnitDied = 0x43,
    TimedAdded = 0x50,
    TimedChanged = 0x51,
    TimedRemoved = 0x52,
    PartyState = 0x60,
    Weather = 0x70,
    Time = 0x71,
    Wind = 0x72,
    Countdowns = 0x73,
    Party = 0x80,
    SetVariable = 0x81,
    DeleteVariable = 0x82,
    PartyUpdate = 0x83,
};

// The messages a PlayerClient sends its server (PlayerServer::Receive switches on them). The numbers are
// the wire format; 0x37 has no handler.
enum class ClientMessage : std::uint8_t {
    Login = 0x00,
    Leave = 0x01,
    RefreshParty = 0x02,
    TakeExit = 0x03,
    GameStateRequest = 0x04,
    Joined = 0x05,
    LeaveToZone = 0x06,
    BriefingDone = 0x07,
    NewParty = 0x08,
    Resume = 0x09,
    Pause = 0x10,
    Walk = 0x30,
    Attack = 0x31,
    CastAtPoint = 0x32,
    CastAtObject = 0x33,
    WantedWeapon = 0x34,
    MoveStyle = 0x35,
    UseOrder = 0x36,
    UnitLookup = 0x38,
    Follow = 0x39,
    GoTo = 0x3A,
    FightAtWill = 0x3B,
    LastPosition = 0x40,
    PartyUpdate = 0x50,
};

// Starts a message in `out`: its type byte.
template <typename Message>
void WriteType(MemoryStream* out, Message type) {
    out->WriteValue(static_cast<std::uint8_t>(type));
}

// A unit placement: the unit's id, position, rotation and two unit values (WorldServer::StoreParty stores
// them, CreateUnits restores them).
struct UnitPlacement {
    int id;
    Vector3 position;
    Quaternion rotation;
    std::uint32_t moveMode;         // the unit's moveMode2
    std::uint32_t weapon;           // its active weapon (pendingWeapon on restore)
};

class PlayerClient;
class PlayerServer;

// A world player's connection to its client (Player::connection): PlayerServer or PlayerClient.
class PlayerConnection : public EngineObject {
public:
    ~PlayerConnection() override;

    virtual void ForgetSentObjects() = 0;   // server (sent objects, party units); client empty
    virtual void UnitChanged(Unit*) {}     // client (the unit's controller); server empty
    virtual void ResetState() {}            // server and client versions
    virtual void Leave() {}                 // server (State::Leaving, flushed); client empty
    virtual void ItemsGiven(const std::vector<Item*>*, int) {}   // server; client empty
    virtual void ItemsTaken() {}            // server (message 0x17); client empty
    virtual void ItemTaken(EngineObject*) {}   // server (message 0x0A); client empty
    virtual void LeaderDied() {}            // the player's leader died: server; client empty
    virtual void TickDone() {}              // server; client empty
    virtual void ItemUsed(Item*) {}    // server (message 0x14); client empty
    virtual int Receive() = 0;              // server and client versions

    Player* player;    // the owner (set by Player::SetConnection)

protected:
    PlayerConnection();
};

// A player's server side (made by the server's specific manager).
class PlayerServer final : public PlayerConnection {
public:
    PlayerServer();
    ~PlayerServer() override;

    void Create() override { ResetState(); }
    void Destroy() override { ResetState(); }
    const char* ClassName() override;
    ClassId GetClassId() override;
    // The sent objects and timed objects released, both unit groups emptied.
    void ForgetSentObjects() override;
    // Back to a new connection's state: the messages, the sent objects, NotLoggedIn, the groups, the placements and
    // both coders; the next party refresh drawn (ISAAC).
    void ResetState() override;
    // A joined player collects its party when deployed (partyDeployed, then cleared) and, in a
    // multiplayer game, sends it; then Leaving with message 0x01 unless already, and Flush.
    void Leave() override;
    // For a joined player (State::Joined): message 0x05, a stream with the given items' spells and then the items,
    // and message 0x06: the party's money, the flag's low byte, the items' ids and -1.
    void ItemsGiven(const std::vector<Item*>* items, int flag) override;
    void ItemsTaken() override;                       // message 0x17
    void ItemTaken(EngineObject* item) override;     // message 0x0A: the item's id
    // The player's leader died. Outside a network game: messages 0x09 and 0x0B (the hero's id, 0xFF). In one,
    // a party of one hero is revived: it loses a share of its money (into a shared object with the given
    // items) and of its experience, gets a new record id, and is collected, sent and redeployed; its new
    // brain knows the units the old one knew and the old hero.
    void LeaderDied() override;
    void TickDone() override;                         // possessedGroup emptied
    void ItemUsed(Item* item) override;               // message 0x14: the item's id and charge
    // Every packet waiting on the link is decompressed into `in` and its messages run through the handlers;
    // before the player has logged in only types 0 and 8, the rest of such a packet is dropped. A read past the
    // end, a type without a handler or a refusing handler ends in the fatal error (its result returned). 0 when
    // leaving (State::Leaving, also after a handler) or without a link, else 1.
    int Receive() override;

    void SetVariable(std::string_view name, float value);                // message 0x81
    void SetVariableMax(std::string_view name, float value);             // raises a variable, 0x81
    void DeleteVariable(const char* name);                          // message 0x82
    void SendGameTime();                                            // message 0x07
    void SendZone(MemoryStream* zone);                           // message 0x02, `zone`
    void SendWeather(std::uint8_t weather, std::uint16_t strength);   // message 0x70
    void SendTime(float hours, float speed);                          // message 0x71
    void SendWind(float x, float y, float z, float strength);       // message 0x72
    void SendCountdowns(const WaterTweens* source);                    // message 0x73
    void SendCameraPlacement(const Vector3& at, std::uint32_t startValue, std::uint32_t sessionKind);   // message 0x04
    void SendFreeCamera(const Vector3& at, const Vector3& other, std::uint8_t flag);   // 0x0F
    void SendWeaponRemoved(std::uint32_t unitId, std::uint32_t index);
    void SendJingle(const char* text);
    void SendExperienceAll(std::uint32_t value);
    void SendExperience(std::uint32_t unitId, std::uint32_t value);
    void SendUnitDied(const EngineObject* object);                 // id, nothing without an object
    void SendUnitSpeech(const EngineObject* object, AckKind kind);   // id, the ack kind (message 0x0B)
    // Message 0x12, a byte and `value` . The original never writes
    // the byte: whatever its stack holds goes out. Ours sends 0; the client's handler skips the byte.
    void SendWorldValue(std::uint32_t value);
    void SendStringEvent(std::string_view text);
    void WriteTo(ByteStream* target);                                  // target->CopyFrom(out)
    void SetDisconnect();                                           // State::Disconnected
    void SetPartyDeployed(std::uint8_t value);

    // Compresses the outgoing messages and sends them on the player's link; the messages are emptied. The
    // send's result (a bool), 0 without a link.
    int Flush();
    // Each new map object (message 0x40: header, scale, names, the server companion's names and strings, a
    // unit's name, its full state) / timed object (0x50: id, class, full state) is sent and kept, counted, in
    // the player's map.
    void SendObjectsAdded(const std::vector<MapObject*>* objects);
    void SendTimedAdded(const std::vector<Effect*>* effects);
    // The objects changed in this world update (their stamp equals the world's update count) send their delta
    // (0x41 / 0x51: id, then the delta stream).
    void SendObjectsChanged(const std::vector<MapObject*>* objects);
    void SendTimedChanged(const std::vector<Effect*>* effects);
    // Objects gone from view: dropped from the player's map and released (0x42 / 0x52: id).
    void SendObjectsRemoved(const std::vector<MapObject*>* objects);
    void SendTimedRemoved(const std::vector<Effect*>* effects);
    // Every item of the player's item list the party's item manager knows is sent (ItemUsed).
    void SendItems();
    // Message 0x60: the world's pause flag, possessedId, the party units (partyUnits) by id and the party's
    // danger (danger: EnemyNear when a unit of the player has an exposure at most 2, InDanger when a party unit
    // fights or is the enemy of a hostile neighbour), then the objects shared with a
    // multiplayer client's single hero.
    void SendPartyState();
    // A map object leaves the world: when the client knows it, it is dropped from the player's map and the
    // sent list (each drop releases it) and message 0x42 goes out; it leaves both groups in any case.
    void ForgetObject(EngineObject* object);
    bool IsPartyUnit(EngineObject* object);
    // Message 0x80: the party saved (with its variables) as a sized block; the next refresh in 900..1800
    // updates (ISAAC).
    void SendParty();
    // The party units (partyUnits) recomputed: with possessedId at -1, possessedGroup plus the player's units,
    // the units they see and what they know (their brains' lists), and every brain then knows all of it;
    // otherwise partyUnits becomes a copy of possessedGroup and possessedId -1.
    void UpdatePartyUnits();

    // Message 0x36: unit ids (missing or blocked ones left out), a flag, a kind, a value and the target
    // object's id. Of several units, the nearest to the order's aim point first, the first whose controller
    // accepts the use order (CheckOrder, or CheckOrderAt where its path ends) does it, else the nearest; its
    // brain's Player mode runs CommandUse. Returns true.
    bool OnUseOrder();

    // The rest of the client's messages. Each returns true, or false to refuse (the dispatcher's fatal error).
    // The player leaves for zone `zone` at `entrance` (joined players only): in campaign modes 1 and 3 a dead
    // or missing leader first dies (outside a network game nothing more); the party is collected, handed to
    // the party manager (single player) or sent (multiplayer); then message 0x08 and a flush.
    void LeaveToZone(std::string_view zone, int entrance);
    bool OnRefreshParty();      // Message 0x02: ForgetKnownObjects, SendParty
    bool OnTakeExit();          // Message 0x03: exit index -> FindZoneExit -> LeaveToZone (false when none)
    // Message 0x05: GameStateSent -> Joined, the session kind, Redeploy with the placements, PlayerDirty::GameState.
    bool OnJoined();
    bool OnLeaveToZone();       // Message 0x06: zone text, entrance -> LeaveToZone
    bool OnBriefingDone();      // Message 0x07: briefing name -> CompleteBriefing(player id)
    // Message 0x08: a party (LoadParty with variables); one of a single record gets a fresh party id
    // (PartyNextId into partyId), its record the next free object id (from 1000000013), and is stored in
    // the party manager's map. 0 for any other record count.
    bool OnNewParty();
    bool OnResume();            // Message 0x09: Disconnected -> Joined; Redeploy (no placements) unless deployed
    // Messages 0x30 / 0x3A: unit ids (missing or blocked ones left out), a run flag and a point; each unit's
    // Player mode walks / goes (CommandWalk / CommandGoTo) to the point plus its formation offset.
    bool OnWalk();
    bool OnGoTo();
    // Message 0x31: unit ids, two flags, an object id; each unit's Player mode attacks it.
    bool OnAttack();
    bool OnPartyUpdate();       // Message 0x50: a sized block loaded into the party (no variables)
    // "Able" units below: present, not blocked (flag 0x800000) and not dead.
    bool OnLeave();             // Message 0x01: the connection's Leave
    // Message 0x04 (joined players, else false): the game state into sessionKind, the party collected
    // (CollectParty with the placements) and sent, message 0x07 (the state and the world tick), dirty bit
    // 0x10 cleared, GameStateSent.
    bool OnGameStateRequest();
    // Message 0x10: a pause byte. A multiplayer client that is also the attached game server, asking to
    // pause: only the session leader (its id the party's first record's unit) may, and its name is announced
    // (text 0xC); then the world's SetPaused(pause != 0).
    bool OnPause();
    // Messages 0x33 / 0x32: {spell, unit id, target id / point, flag byte}: an able unit with a Player mode
    // casts at the object / point (CommandCast / CommandCastAt).
    bool OnCastAtObject();
    bool OnCastAtPoint();
    // Messages 0x34 / 0x35: {unit id, byte}: an able unit's wanted weapon slot / a living unit's movement
    // style (no blocked test).
    bool OnWantedWeapon();
    bool OnMoveStyle();
    bool OnUnitLookup();        // Message 0x38: two dwords, the first looked up as a unit (unused)
    // Message 0x39: {unit id, leader id}: a unit's Player mode follows the leader (may be null).
    bool OnFollow();
    // Message 0x3B: a unit id, then a byte: Unit::SetFightAtWill(signed byte) when the unit exists.
    bool OnFightAtWill();
    bool OnLastPosition();      // Message 0x40: 8 bytes into lastPosition x, y
    // Message 0x00: the login (PlayerLoginMessage); false unless the player is NotLoggedIn (after the read). A client version
    // other than 0x64 gets message 0x11 ("WrongClientVersion") and Leaving (false); a name or password that is
    // not printable text: false. The party by game mode (1 the party manager's EndRedeploy; 2 / 3 the one stored
    // under partyId, 0 when there is none; 4 cleared), then message 0x00 with the player's id, SendParty,
    // Joined.
    bool OnLogin();
    // Message 0x83: the party saved without its variables as a sized block.
    void SendPartyUpdate();
    // From the player state tick: a joined, deployed player's party is sent again (logged, the records
    // synced, SendParty).
    void ResendParty();
    // From the briefing screen. Message 0x19: a byte and a dword.
    void SendUnitControl(std::uint8_t event, std::uint32_t subject);

    // Message 0x0E about a path of `unit` (nothing for null): kind, flag, the point's two dwords, the unit's
    // id and `limit`; except for kind 3 the path written through a delta field.
    void SendPathMessage(EngineObject* unit, PathOrder* path, const Vector3& point, int kind, int flag,
                         float limit);

    // -1, or the unit a Possession spell took (its id): the next party refresh then sends possessedGroup (the
    // target and what it sees) and sets it back to -1. Sent with message 0x60.
    std::uint32_t possessedId;

    // Where the client is in joining and leaving (the harness reads it).
    enum class State : int {
        NotLoggedIn = 0,        // only a login or a new party (ClientMessage::NewParty) is read
        Joined = 1,
        GameStateSent = 2,      // the game state request is answered; the client's Joined message is next
        Leaving = 3,
        Disconnected = 4,       // SetDisconnect; ClientMessage::Resume brings it back
    };
    State state;

    Vector3 lastPosition;           // sent with 0x04 / 0x0F
    MemoryStream in;                   // the client's messages being run (each received packet decompressed)
    MemoryStream out;                  // messages to the client
    std::uint8_t partyDeployed;     // the party stands in the world: LeaderDied collects it when set, redeploys it when clear
    std::vector<UnitPlacement> placements;
    CoderState compressor;          // for the messages sent (Flush)
    CoderState decompressor;        // for the messages received
    std::vector<MapObject*> sentObjects;    // map objects the client has (counted)
    std::vector<Effect*> sentTimed;    // timed objects the client has (counted)
    ScriptGroup possessedGroup;
    ScriptGroup partyUnits;         // units always sent (message 0x60)
    // How much danger the party is in (SendPartyState; the script's IsPlayerInDanger / IsPlayerInSafety).
    enum class PartyDanger : int { Safe = 0, EnemyNear = 1, InDanger = 2 };
    PartyDanger danger;
    int refreshTimer;               // updates until the party is sent again
};

// The client's login message to the world server (not net::LoginMessage; 0x88 bytes, read whole by PlayerServer::OnLogin).
struct PlayerLoginMessage {
    int version;                    // 0x64
    char name[0x40];
    char password[0x40];
    std::uint32_t partyId;          // the client player's partyId (cleared before the read)
};
static_assert(sizeof(PlayerLoginMessage) == 0x88);   // sent as its bytes

// The login's name and password filter: 1 when `text`, within the login field's 0x3F bytes, is printable characters
// (each a valid UTF-8 sequence of a printable code point) and not empty; 0 at the first unprintable one. (The
// original took the game's isprint of each signed char.)
bool IsPrintableText(const char* text);

// The walk orders' formation: one unit gets (0, 0, 0); several get the unit vector (x, y, 0) from the units'
// 2D centroid to each (unnormalised when it is zero). `offsets` is sized to the units.
void FormationOffsets(const std::vector<Unit*>* units, std::vector<Vector3>* offsets);

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
    void GiveItems(const std::vector<Item*>* items, int flag);
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
    PlayerServer* Server() const { return dynamic_cast<PlayerServer*>(connection); }

    netlink::NetLink* link;                     // the connection to the player's client, null: none
    PlayerConnection* connection;              // PlayerServer / PlayerClient (counted)
    int playerId;                               // player number (0 = the local player)
    MapObjectMap knownObjects;                  // map objects sent to the client
    int maxObjectId;                            // highest id put in knownObjects
    UnitMap units;                              // the player's units by id (counted)
    int maxUnitId;                              // highest id put in units
    TimedObjectMap knownTimed;                  // timed objects (effects) sent to the client
    int maxTimedId;
    Party party;
    std::vector<Item*> items;          // items given to the player (counted)
    int partyId;                                // the party's key in the party manager; sent with the client's login
    int sessionKind;                         // 3: the client runs a setup on the controllers of new units
    std::uint32_t lastScreen;                   // the session kind before the last game-state message (5: the camp)
    std::uint8_t godMode;                       // the script's GodMode(player, on); the combat code tests it
    int logTraffic;                             // logs the size of every flush
    int lastTick;                               // -1 after Create; the world tick last saved or restored for the player
    std::vector<std::uint32_t> recharging;     // ids of items recharging (saved with the world from version 0x55)
    std::uint32_t dirty;                        // PlayerDirty bits
};

// The message dispatchers' fatal error: `format` with the previous and the current message type (their low
// bytes); the FatalError result.
int PlayerMessageError(const char* format, std::uint32_t previous, std::uint32_t type);
