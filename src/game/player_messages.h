#pragma once

// The server side of a player's connection (CPlayerServer) as far as the message writers use it, and the
// party record lookup. Each writer appends one message to the outgoing stream: a type byte, then the payload.
// The single-player game runs the same server / client pair, so the per-tick player state, script variables
// and unit events all go through them. See docs/notes/objects.md.

#include "engine_object.h"
#include "maths.h"
#include "net_delta.h"
#include "party_storage.h"
#include "script.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"
#include "lib/hash_map.h"

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

class CPlayer;
class CItem;
class CUnit;
class CMapObject;
class CEffect;
struct PathOrder;
namespace netlink {
class NetLink;
}

// A pointer array of the sync (SyncPlayer's statics, the player state's sent objects): data on a
// 32-byte boundary, size, capacity, grow-by.
using PlayerObjectArray = std::vector<CEngineObject*>;

// One row of the server's and the client's handler tables: a message type and its handler, a method of the
// connection that reads the message from its stream.
template <typename Connection>
struct HandlerEntry {
    std::uint8_t type;
    int (Connection::*handler)();
};

// A unit placement: the unit's id, position, rotation and two unit values (CWorldServer::StoreParty stores
// them, CreateUnits restores them).
struct UnitPlacement {
    int id;
    std::uint32_t position[3];      // float bits
    std::uint32_t rotation[4];
    std::uint32_t value20;
    std::uint32_t value24;
};
static_assert(sizeof(UnitPlacement) == 0x28);

// A world player's connection to its client (CPlayer::connection_): CPlayerServer or CPlayerClient.
class CPlayerConnection : public CEngineObject {
public:
    ~CPlayerConnection() override;

    virtual void ForgetSentObjects() = 0;   // server (sent objects, party units); client empty
    virtual void UnitChanged(CUnit*) {}     // client (the unit's controller); server empty
    virtual void ResetState() {}            // server and client versions
    virtual void Leave() {}                 // server (state 3, flushed); client empty
    virtual void ItemsGiven(const std::list<CItem*>*, int) {}   // server; client empty
    virtual void ItemsTaken() {}            // server (message 0x17); client empty
    virtual void ItemTaken(CEngineObject*) {}   // server (message 0x0A); client empty
    virtual void LeaderDied() {}            // the player's leader died: server; client empty
    virtual void TickDone() {}              // server; client empty
    virtual void ItemUsed(CEngineObject*) {}    // server (message 0x14); client empty
    virtual int Receive() = 0;              // server and client versions

    CPlayer* Owner() const { return player_; }

    CPlayer* player_;   // the owner (set by CPlayer::SetConnection)
    int state_;         // both classes' state (1: joined / logged in; see each class); the harness reads it

protected:
    CPlayerConnection();
};
static_assert(offsetof(CPlayerConnection, state_) == 0x20);   // the harness's ConnectionState

// A player's server side (made by the server's specific manager).
class CPlayerServer final : public CPlayerConnection {
public:
    CPlayerServer();
    ~CPlayerServer() override;

    int Create() override { return CreateServer(); }
    void Destroy() override { ClearServerState(); }
    const char* GetClassName() override;
    int GetClassId() override;
    void ForgetSentObjects() override { ReleaseSentObjects(); }
    void ResetState() override { ClearServerState(); }
    void Leave() override { LeaveAndFlush(); }
    void ItemsGiven(const std::list<CItem*>* items, int flag) override { SendItemsGiven(items, flag); }
    void ItemsTaken() override { Send17(); }
    void ItemTaken(CEngineObject* item) override { SendObject0A(item); }
    void LeaderDied() override { OnLeaderDied(); }
    void TickDone() override { ClearGroup50F8(); }
    void ItemUsed(CEngineObject* item) override { SendObject14(item); }
    int Receive() override { return ReceiveMessages(); }

    void SetVariable(const char* name, std::uint32_t value);        // message 0x81
    void SetVariableMax(const char* name, float value);             // raises a variable, 0x81
    void DeleteVariable(const char* name);                          // message 0x82
    void SendGameTime();                                            // message 0x07
    int SendState02(CMemoryStream* zone);                           // message 0x02, `zone`
    int SendState70(std::uint8_t a, std::uint16_t b);               // message 0x70
    int SendState71(std::uint32_t a, std::uint32_t b);              // message 0x71
    int SendState72(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w);   // 0x72
    void SendState73(const WaterTweens* source);                    // message 0x73
    int SendPosition04(const Vector3* at, std::uint32_t a, std::uint32_t b);   // message 0x04
    std::uint32_t SendPosition0F(const Vector3* at, const Vector3* other, std::uint8_t flag);   // 0x0F
    void Send17();
    void SendObject0A(const CEngineObject* object);                 // id
    void SendPair16(std::uint32_t a, std::uint32_t b);
    void SendObject14(const CEngineObject* object);                 // the item's id and charge
    void SendText0D(const char* text);
    void SendValue15(std::uint32_t value);
    void SendPair18(std::uint32_t a, std::uint32_t b);
    void SendObject43(const CEngineObject* object);                 // id, nothing without an object
    void SendObject0B(const CEngineObject* object, std::uint32_t value);   // id, value
    // Message 0x12, a byte and `value` (the 2nd argument; the first is not read). The original never writes
    // the byte: whatever its stack holds goes out. Ours sends 0; the client's handler skips the byte.
    void SendValue12(std::uint32_t unused, std::uint32_t value);
    void SendText13(std::uint32_t unused, const char* text);        // the text is the 2nd argument
    void WriteTo(CStream* target);                                  // target->CopyFrom(out_)
    void SetDisconnect();                                           // state 4
    void SetFlag70(std::uint8_t value);

    // Compresses the outgoing messages and sends them on the player's link; the messages are emptied. The
    // send's result (a bool), 0 without a link.
    int Flush();
    // Each new map object (message 0x40: header, scale, names, the server companion's names and strings, a
    // unit's name, its full state) / timed object (0x50: id, class, full state) is sent and kept, counted, in
    // the player's map. 1.
    int SendObjectsAdded(const PlayerObjectArray* objects);
    int SendTimedAdded(const PlayerObjectArray* objects);
    // The objects changed in this world update (their stamp equals the world's update count) send their delta
    // (0x41 / 0x51: id, then the delta stream). 1.
    int SendObjectsChanged(const PlayerObjectArray* objects);
    int SendTimedChanged(const PlayerObjectArray* objects);
    // Objects gone from view: dropped from the player's map and released (0x42 / 0x52: id). 1.
    int SendObjectsRemoved(const PlayerObjectArray* objects);
    int SendTimedRemoved(const PlayerObjectArray* objects);
    // Every item of the player's item list the party's item manager knows is sent (SendObject14).
    void SendItems();
    // For a joined player (state 1): message 0x05, a stream with the given items' spells and then the items,
    // and message 0x06: the party's money, the flag's low byte, the items' ids and -1.
    void SendItemsGiven(const std::list<CItem*>* items, int flag);
    // Message 0x60: the world's pause flag, possessedId_, the party units (partyUnits_) by id and whether the
    // client should refresh the party (refreshPending_: 1 when a unit of the player has a level at most 2, 2
    // when a party unit fights or is the enemy of a hostile neighbour), then the objects shared with a
    // multiplayer client's single hero. Returns 1.
    int SendPartyState();
    // A map object leaves the world: when the client knows it, it is dropped from the player's map and the
    // sent list (each drop releases it) and message 0x42 goes out; it leaves both groups in any case.
    // Returns 1.
    int ForgetObject(CEngineObject* object);
    BOOL IsPartyUnit(CEngineObject* object);
    // Message 0x80: the party saved (with its variables) as a sized block; the next refresh in 900..1800
    // updates (ISAAC).
    void SendParty();
    // Every packet waiting on the link is decompressed into the received stream and its messages run through
    // the handlers; before the player has joined (state 0) only types 0 and 8, the rest of such a packet is
    // dropped. A read past the end, a type without a handler or a refusing handler ends in the fatal error
    // (its result returned). 0 when leaving (state 3, also after a handler) or without a link, else 1.
    int ReceiveMessages();
    // The party units (partyUnits_) recomputed: with possessedId_ at -1, group50F8_ plus the player's units,
    // the units they see and what they know (their brains' lists), and every brain then knows all of it;
    // otherwise partyUnits_ becomes a copy of group50F8_ and possessedId_ -1.
    void UpdatePartyUnits();

    // The player's leader died. Outside a network game: messages 0x09 and 0x0B (the hero's id, 0xFF). In one,
    // a party of one hero is revived: it loses a share of its money (into a shared object with the given
    // items) and of its experience, gets a new record id, and is collected, sent and redeployed; its new
    // brain knows the units the old one knew and the old hero.
    void OnLeaderDied();
    // Message 0x36: unit ids (missing or blocked ones left out), a flag, a kind, a value and the target
    // object's id. Of several units, the nearest to the order's aim point first, the first whose controller
    // accepts the use order (CheckOrder, or CheckOrderAt where its path ends) does it, else the nearest; its
    // brain's Player mode runs CommandUse. Returns 1.
    int OnUseOrder();

    // The rest of the client's messages. Each returns 1, or 0 to refuse (the dispatcher's fatal error).
    // The player leaves for zone `zone` at `entrance` (joined players only): in campaign modes 1 and 3 a dead
    // or missing leader first dies (outside a network game nothing more); the party is collected, handed to
    // the party manager (single player) or sent (multiplayer); then message 0x08 and a flush.
    void LeaveToZone(const char* zone, int entrance);
    int OnRefreshParty();      // Message 0x02: ForgetKnownObjects, SendParty
    int OnTakeExit();          // Message 0x03: exit index -> GetZoneExit -> LeaveToZone (0 when none)
    int OnJoined();            // Message 0x05: state 2 -> 1, the session kind, Redeploy with the placements, dirty_ bit 4
    int OnLeaveToZone();       // Message 0x06: zone text, entrance -> LeaveToZone
    int OnBriefingDone();      // Message 0x07: briefing name -> CompleteBriefing(player id)
    // Message 0x08: a party (LoadParty with variables); one of a single record gets a fresh party id
    // (PartyNextId into partyId_), its record the next free object id (from 1000000013), and is stored in
    // the party manager's map. 0 for any other record count.
    int OnNewParty();
    int OnResume();            // Message 0x09: state 4 -> 1; Redeploy (no placements) unless deployed
    // Messages 0x30 / 0x3A: unit ids (missing or blocked ones left out), a run flag and a point; each unit's
    // Player mode walks / goes (CommandWalk / CommandGoTo) to the point plus its formation offset.
    int OnWalk();
    int OnGoTo();
    // Message 0x31: unit ids, two flags, an object id; each unit's Player mode attacks it.
    int OnAttack();
    int OnPartyUpdate();       // Message 0x50: a sized block loaded into the party (no variables)
    // "Able" units below: present, not blocked (flag 0x800000) and not dead.
    int OnLeave();             // Message 0x01: the connection's Leave
    // Message 0x04 (joined players, state 1, else 0): the game state into sessionKind100_, the party collected
    // (CollectParty with the placements) and sent, message 0x07 (the state and the world tick), dirty bit
    // 0x10 cleared, state 2.
    int OnGameStateRequest();
    // Message 0x10: a pause byte. A multiplayer client that is also the attached game server, asking to
    // pause: only the session leader (its id the party's first record's unit) may, and its name is announced
    // (text 0xC); then the world's SetPaused(pause != 0).
    int OnPause();
    // Message 0x37: no handler (the table has this function, which refuses: returns 0).
    int OnMessage37();
    // Messages 0x33 / 0x32: {spell, unit id, target id / point, flag byte}: an able unit with a Player mode
    // casts at the object / point (CommandCast / CommandCastAt).
    int OnCastAtObject();
    int OnCastAtPoint();
    // Messages 0x34 / 0x35: {unit id, byte}: an able unit's wanted weapon slot / a living unit's movement
    // style (no blocked test).
    int OnWantedWeapon();
    int OnMoveStyle();
    int OnUnitLookup();        // Message 0x38: two dwords, the first looked up as a unit (unused)
    // Message 0x39: {unit id, leader id}: a unit's Player mode follows the leader (may be null).
    int OnFollow();
    // Message 0x3B: a unit id, then a byte: CUnit::SetUnknown438(signed byte) when the unit exists.
    int OnUnitFlag438();
    int OnLastPosition();      // Message 0x40: 8 bytes into lastPosition_ x, y
    // Message 0x00: the login (PlayerLoginMessage); 0 unless the state is 0 (after the read). A client version
    // other than 0x64 gets message 0x11 ("WrongClientVersion") and state 3 (0); a name or password that is
    // not printable text: 0. The party by game mode (1 the party manager's EndRedeploy; 2 / 3 the one stored
    // under partyId_, 0 when there is none; 4 cleared), then message 0x00 with the player's id, SendParty,
    // state 1.
    int OnLogin();
    // Message 0x83: the party saved without its variables as a sized block.
    void SendPartyUpdate();
    // From the player state tick: a joined, deployed player's party is sent again (logged, the records
    // synced, SendParty).
    void ResendParty();
    // From the briefing screen. Message 0x19: a byte and a dword.
    void SendPair19(std::uint8_t a, std::uint32_t b);

    void ClearServerState();
    void ReleaseSentObjects();
    void ClearGroup50F8();
    // A joined player (state 1) collects its party when deployed (flag70_, then cleared) and, in a
    // multiplayer game, sends it; then state 3 with message 0x01 unless already 3, and Flush.
    void LeaveAndFlush();
    // Message 0x0E about a path of `unit` (nothing for null): kind, flag, the point's two dwords, the unit's
    // id and `limitBits`; except for kind 3 the path written through a delta field.
    void SendPathMessage(CEngineObject* unit, PathOrder* path, const float* point, int kind, int flag,
                         std::uint32_t limitBits);

    // -1, or the unit a Possession spell took (its id): the next party refresh then sends group50F8_ (the
    // target and what it sees) and sets it back to -1. Sent with message 0x60.
    std::uint32_t possessedId_;
    // ClearServerState, then the handler table: 256 entries, zeroed, and the 25 handlers by message type.
    BOOL CreateServer();

    using Handler = int (CPlayerServer::*)();
    std::vector<Handler> handlers_;   // by message type (null: refused; 256 of them)
    Vector3 lastPosition_;          // sent with 0x04 / 0x0F
    int spare_;                     // -1 after Create; nothing reads it
    BitStream out_;                 // messages to the client
    std::uint8_t flag70_;           // OnLeaderDied collects the party when set, redeploys it when clear
    std::uint8_t unused_71[3];
    mfc::CArray<UnitPlacement> placements_;
    CoderState compressor_;         // for the messages sent (Flush)
    CoderState decompressor_;       // for the messages received
    PlayerObjectArray sentObjects_; // map objects the client has (counted)
    PlayerObjectArray sentTimed_;   // timed objects the client has (counted)
    ScriptGroup group50F8_;
    ScriptGroup partyUnits_;        // units always sent (message 0x60)
    int refreshPending_;            // 0, 1 or 2 (SendPartyState)
    int refreshTimer_;              // updates until the party is sent again
};

// The client's login message to the world server (not net::LoginMessage; 0x88 bytes, read whole by CPlayerServer::OnLogin).
struct PlayerLoginMessage {
    int version;                    // +0x00 0x64
    char name[0x40];                // +0x04
    char password[0x40];            // +0x44
    std::uint32_t partyId;          // +0x84 the client player's partyId_ (cleared before the read)
};
static_assert(sizeof(PlayerLoginMessage) == 0x88);

// 1 when the first `maxLength` bytes of `text` are printable characters, each a valid UTF-8 sequence of a printable
// code point; a terminator before that gives whether at least `minLength` characters came first; 0 at the first
// unprintable one. (The original took the game's isprint of each signed char.)
BOOL IsPrintableText(const char* text, int maxLength, int minLength);

// |a - b| squared, unrounded (the use order's nearest-unit test).
double UseOrderDistanceSquared(const Vector3* a, const Vector3* b);
// The walk orders' formation: one unit gets (0, 0, 0); several get the unit vector (x, y, 0) from the units'
// 2D centroid to each (unnormalised when it is zero). `offsets` is sized to the units.
void FormationOffsets(const std::vector<CUnit*>* units, std::vector<Vector3>* offsets);

// A world player (CPlayer). The harness's digest reads its fields (tests/verify/harness_digest.cpp), hence the
// layout asserts.
class CPlayer final : public CEngineObject {
public:
    // The world objects the player knows, by id, each counted once: the map objects and the timed objects
    // (effects).
    // Walked in STLport's order (the objects are released, the units handled in it).
    using MapObjectMap = lib::HashMap<int, CMapObject*>;
    using TimedObjectMap = lib::HashMap<int, CEffect*>;
    using UnitMap = lib::HashMap<int, CUnit*>;

    CPlayer();
    ~CPlayer() override;

    int Create() override { return CreatePlayer(); }
    void Destroy() override { DestroyPlayer(); }
    const char* GetClassName() override;
    int GetClassId() override;

    BOOL CreatePlayer();
    void DestroyPlayer();
    // Empties the given items, both known-object maps, the units (each taken away; the walk stops at the
    // first null) and the recharge list, then the connection's ForgetSentObjects.
    void Clear();
    // The old connection loses its owner and a reference; the new one gets this player as owner and a
    // reference.
    void SetConnection(CPlayerConnection* connection);
    void SetPlayerId(int id);
    // With a link the connection leaves; Clear; the old link disconnected and released; `link` taken
    // (counted); then the connection's ResetState.
    void Connect(netlink::NetLink* link);
    // As Connect without the new link and the ResetState call.
    void Disconnect();
    // Both known-object maps cleared, the connection's ForgetSentObjects, dirty_ -1.
    void ForgetKnownObjects();

    // Whether `unit` is the party's leader: there are records and its id is the first record's key. 0 for
    // null.
    BOOL IsLeader(const CEngineObject* unit);
    // The connection's ItemsGiven, then every item of `items` counted and appended to the given items.
    void GiveItems(const std::list<CItem*>* items, int flag);
    // The connection's ItemsTaken, then every given item taken off the head and released.
    void TakeAllItems();
    // 1 when a given loot item (0x3009) has `kind` (CItemLoot::kind_), or an item a unit of the player
    // carries has it (any class; the walk stops at a null unit).
    BOOL HasLoot(int kind);
    // The first given loot item of `kind` / item with id `id`: the connection's ItemTaken, out of the list,
    // released; 1, or 0 when there is none.
    BOOL TakeLoot(int kind);
    BOOL TakeItem(int id);
    // A unit not yet in units_ is counted, raises the max id and is put in; then the connection's
    // UnitChanged. Nothing for null or a known unit.
    void AddUnit(CUnit* unit);
    // A unit in units_ is erased, released and given to the connection's UnitChanged.
    void RemoveUnit(CUnit* unit);
    // The item's id joins the recharge list unless there; the connection's ItemUsed.
    void ItemUsed(CEngineObject* item);
    // The recharge list emptied, then the party assigned from `source` (CParty::Assign's result).
    CParty* AssignParty(CParty* source);
    // Every unit at a taming stage leaves the player; the walk stops at a null entry.
    void ReleaseTamedUnits();

    // Ticks the recharging items (RunTick; found through the party's item manager), dropping those gone or
    // whose tick returned 0, then the connection's TickDone.
    void Tick();

    CParty* Party() { return party_.Get(); }
    // The connection as the server's view of it (a CPlayerServer on the server world, null for none).
    CPlayerServer* Server() const { return static_cast<CPlayerServer*>(connection_); }

    netlink::NetLink* link_;                    // the connection to the player's client, null: none
    CPlayerConnection* connection_;             // CPlayerServer / CPlayerClient (counted)
    int playerId_;                              // player number (0 = the local player)
    MapObjectMap knownObjects_;                 // map objects sent to the client
    int maxObjectId_;                           // highest id put in knownObjects_
    UnitMap units_;                             // the player's units by id (counted)
    int maxUnitId_;                             // highest id put in units_
    TimedObjectMap knownTimed_;                 // timed objects (effects) sent to the client
    int maxTimedId_;
    PartyStorage party_;                        // an embedded CParty
    std::list<CItem*> items_;           // items given to the player (counted)
    int partyId_;                               // the party's key in the party manager; sent with the client's login
    int sessionKind100_;                        // 3: the client runs a setup on the controllers of new units
    std::uint32_t lastScreen_;                  // the session kind before the last game-state message (5: the camp)
    std::uint8_t godMode_;                      // the script's GodMode(player, on); the combat code tests it
    int logTraffic_;                            // logs the size of every flush
    int lastTick_;                              // -1 after Create; the world tick last saved or restored for the player
    std::list<std::uint32_t> recharging_;      // ids of items recharging (saved with the world from version 0x55)
    std::uint32_t dirty_;                       // state to send (bits the server's state messages clear); -1 after the reset
};
static_assert(offsetof(CPlayer, link_) == 0x1C);
static_assert(offsetof(CPlayer, knownObjects_) == 0x28);
static_assert(offsetof(CPlayer, connection_) == 0x20);

// The message dispatchers' fatal error: `format` with the previous and the current message type (their low
// bytes); the FatalError result.
int PlayerMessageError(const char* format, std::uint32_t previous, std::uint32_t type);
