#pragma once

// The server side of a player's connection (CPlayerServer) as far
// as the message writers use it, and the party record lookup. Each writer appends one message to
// the outgoing stream at +0x48 (a CMemoryStream; its slot 2 writes bytes): a type byte, then the
// payload. The single-player game runs the same server / client pair, so the per-tick player state
// script variables and unit events all go through them. Names are provisional; see
// docs/notes/objects.md.

#include "engine_object.h"
#include "maths.h"
#include "net_delta.h"
#include "party_storage.h"
#include "script.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CPlayer;
class CItem;    // item.h
class CParty;   // party.h
class CPlayerClient;   // player_client.h
class CUnit;    // unit.h
class CMapObject;   // map_object.h
class CEffect;      // world_effect.h
struct WaterTweens;   // water_tweens.h
struct PathOrder;     // ai_map.h
namespace netlink {
class NetLink;  // net_link.h
}

// A pointer array of the sync (SyncPlayer's statics, the player state's sent objects): data on a
// 32-byte boundary, size, capacity, grow-by.
using PlayerObjectArray = lib::AlignedArray<CEngineObject*>;

// One row of the server's and the client's handler tables (player_messages.cpp, player_client.cpp):
// a message type and the address of its thiscall handler.
struct HandlerEntry {
    std::uint8_t type;
    std::uintptr_t address;
};
static_assert(sizeof(HandlerEntry) == 8 && offsetof(HandlerEntry, address) == 4);

// A unit placement (0x28 bytes): the unit's id, position, rotation and two
// values (CWorldServer::StoreParty stores the unit's +0x268 / +0x26C, CreateUnits restores them).
struct UnitPlacement {
    int id;
    std::uint32_t position[3];      // +0x04 (float bits)
    std::uint32_t rotation[4];      // +0x10
    std::uint32_t value20;          // +0x20
    std::uint32_t value24;          // +0x24
};
static_assert(sizeof(UnitPlacement) == 0x28);

// A world player's connection to its client (CPlayer::connection_): CPlayerServer
// or CPlayerClient, both derived from it. It names the slots after CEngineObject's that the player calls; the
// base's bodies are empty, 13 and 23 pure.
class CPlayerConnection : public CEngineObject {
public:
    // The deleting destructor (player_client.cpp): no player, then CEngineObject's.
    ~CPlayerConnection() override;

    virtual void ForgetSentObjects() = 0;   // Virtual 13: server (sent objects, party units); client empty
    virtual void UnitChanged(CUnit*) {}     // Virtual 14: client (the unit's controller); server empty
    virtual void ResetState() {}            // Virtual 15: server and client versions
    virtual void Leave() {}                 // Virtual 16: server (state 3, flushed); client empty
    // Virtual 17: server (CPlayerServer::SendItemsGiven); client empty.
    virtual void ItemsGiven(const mfc::CTypedPtrList<CItem>*, int) {}
    virtual void ItemsTaken() {}            // Virtual 18: server (message 0x17); client empty
    virtual void ItemTaken(CEngineObject*) {}   // Virtual 19: server (message 0x0A); client empty
    // Virtual 20: server (CPlayerServer::OnLeaderDied; CUnitServer::RunTick calls it when
    // the dying unit is the player's leader); client empty.
    virtual void LeaderDied() {}
    virtual void TickDone() {}              // Virtual 21: server (empties +0x50F8); client empty
    virtual void ItemUsed(CEngineObject*) {}    // Virtual 22: server (message 0x14); client empty
    virtual int Receive() = 0;              // Virtual 23: server and client versions

    CPlayer* Owner() const { return player_; }

    CPlayer* player_;   // +0x1C the owner (set by CPlayer::SetConnection)
    int state_;         // +0x20 both classes' state (1: joined / logged in; see each class); the harness reads it

protected:
    // The subclasses' constructors start with it: CEngineObject's fields, no player.
    CPlayerConnection();
};
static_assert(offsetof(CPlayerConnection, state_) == 0x20);   // the harness's ConnectionState

// A player's server side (0x5128 bytes, made by the server's specific manager).
class CPlayerServer final : public CPlayerConnection {
public:
    // The engine object's fields, an empty handler table, the outgoing stream,
    // the placements, both coders, the sent arrays and groups, then ClearServerState.
    CPlayerServer();
    // The deleting destructor: ClearServerState (an ISAAC draw, as the original), the groups destroyed, the
    // sent arrays' blocks freed, both coders' output ended, the placements' storage, the handler table's block and
    // the stream's storage freed, then CPlayerConnection's.
    ~CPlayerServer() override;

    int Create() override { return CreateServer(); }
    void Destroy() override { ClearServerState(); }
    const char* GetClassName() override;                                   // "CPlayerServer"
    int GetClassId() override;                                             // 0x3B
    void ForgetSentObjects() override { ReleaseSentObjects(); }
    // Virtual 14 (empty; CPlayerConnection's).
    void ResetState() override { ClearServerState(); }
    void Leave() override { LeaveAndFlush(); }
    void ItemsGiven(const mfc::CTypedPtrList<CItem>* items, int flag) override { SendItemsGiven(items, flag); }
    void ItemsTaken() override { Send17(); }
    void ItemTaken(CEngineObject* item) override { SendObject0A(item); }
    void LeaderDied() override { OnLeaderDied(); }
    void TickDone() override { ClearGroup50F8(); }
    void ItemUsed(CEngineObject* item) override { SendObject14(item); }
    int Receive() override { return ReceiveMessages(); }

    // Variables and state.
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
    // Simple messages.
    void Send17();
    void SendObject0A(const CEngineObject* object);                 // id
    void SendPair16(std::uint32_t a, std::uint32_t b);
    void SendObjects0C(const mfc::CTypedPtrList<CEngineObject>* list);   // ids and +0x28 of a list
    void SendObject14(const CEngineObject* object);                 // id, +0x28
    void SendText0D(const char* text);
    void SendText08(const char* text, std::uint32_t value);
    void SendValue15(std::uint32_t value);
    void SendPair18(std::uint32_t a, std::uint32_t b);
    void SendObject43(const CEngineObject* object);                 // id, nothing without an object
    void SendObject0B(const CEngineObject* object, std::uint32_t value);   // id, value
    // Message 0x12, a byte and `value` (the 2nd argument; the first is not
    // read). The original never writes the byte: whatever its stack holds goes out. Ours sends 0;
    // the client's handler skips the byte.
    void SendValue12(std::uint32_t unused, std::uint32_t value);
    void SendText13(std::uint32_t unused, const char* text);        // the text is the 2nd argument
    void SendText(std::uint8_t type, const char* text);
    void WriteTo(CStream* target);                                  // target->CopyFrom(out_) (slot 7)
    void SetDisconnect();                                           // state 4
    void SetFlag70(std::uint8_t value);

    // The per-tick sync (player_sync.cpp; called by CWorldServer::SyncPlayer, world_sync.cpp).
    // Compresses the outgoing messages (the coder at +0x88) and sends them on the player's
    // link (slot 8); the messages are emptied. The send's result (a bool), 0 without a link.
    int Flush();
    // Each new map object (message 0x40: header, scale, names, the
    // server companion's names and strings, a unit's name and +0x23C, its full state) / timed
    // object (0x50: id, class, full state) is sent and kept, counted, in the player's map. 1.
    int SendObjectsAdded(const PlayerObjectArray* objects);
    int SendTimedAdded(const PlayerObjectArray* objects);
    // The objects changed in this world update (their stamp equals the
    // world's update count) send their delta (0x41 / 0x51: id, then the delta stream). 1.
    int SendObjectsChanged(const PlayerObjectArray* objects);
    int SendTimedChanged(const PlayerObjectArray* objects);
    // Objects gone from view: dropped from the player's map and
    // released (0x42 / 0x52: id). 1.
    int SendObjectsRemoved(const PlayerObjectArray* objects);
    int SendTimedRemoved(const PlayerObjectArray* objects);
    // Every item of the player's item list (+0x114) the party's item manager knows is
    // sent (SendObject14, slot 22).
    void SendItems();
    // For a joined player (state 1): message 0x05, a stream with the
    // given items' spells and then the items,
    // and message 0x06: the party's money, the flag's low byte, the items' ids and -1.
    void SendItemsGiven(const mfc::CTypedPtrList<CItem>* items, int flag);
    // Message 0x60: the world's +0x6C flag, +0x24, the party units (partyUnits_) by id and
    // whether the client should refresh the party (refreshPending_: 1 when a unit of the player has
    // a level at most 2, 2 when a party unit fights or is the enemy of a hostile neighbour), then the
    // objects shared with a multiplayer client's single hero. Returns 1.
    int SendPartyState();
    // A map object leaves the world: when the client knows it, it is dropped from
    // the player's map and the sent list (each drop releases it) and message 0x42 goes out; it
    // leaves both groups in any case. Returns 1.
    int ForgetObject(CEngineObject* object);
    // Whether `object` is one of the party units (partyUnits_).
    BOOL IsPartyUnit(CEngineObject* object);
    // Message 0x80: the party saved (with its variables) as a sized block; the next
    // refresh in 900..1800 updates (ISAAC).
    void SendParty();
    // Every packet waiting on the link is decompressed (+0x28B0) into the
    // received stream and its messages run through the handlers; before the player has
    // joined (state 0) only types 0 and 8, the rest of such a packet is dropped. A read past the
    // end, a type without a handler or a refusing handler ends in the fatal error (its
    // result returned). 0 when leaving (state 3, also after a handler) or without a link, else 1.
    int ReceiveMessages();
    // The party units (partyUnits_) recomputed: with +0x24 at -1, group50F8_ plus the
    // player's units, the units they see and what they know (the brains' +0x12C lists), and every
    // brain then knows all of it; otherwise partyUnits_ becomes a copy of group50F8_ and +0x24 -1.
    void UpdatePartyUnits();

    // player_server_handlers.cpp.
    // The player's leader died. Outside a network game (IsMultiplayerClient):
    // message 0x09, then 0x0B (the hero's id, 0xFF) when the first record's unit exists. In one,
    // a party of one hero is revived: it loses a share of the money, which goes with the
    // player's given items into a shared object (AddSharedObject, the old id, then TakeAllItems);
    // the record gets a new id (the old one, plus 1000000 when below 1001000000, then the first
    // id from there that no world object has), the slots restored and a share of its experience above its NPC record's floor taken off.
    // The party collected (when deployed, flag70_) and sent (SendParty), lastPosition_.z
    // -1313, redeployed (Redeploy with scripts) and the new hero's brain gets the units (class
    // 0x50) the old one knew (+0x12C) and the old hero, each once and counted.
    void OnLeaderDied();
    // Message 0x36, from the client's ViewOrderTarget / dead-unit order
    // handlers). Unit ids (a count, then the ids; units missing or blocked, flag 0x800000, are
    // left out), a flag, a kind, a value and the target object's id. With two units or more the
    // one nearest to the order's aim point (UseOrderDistanceSquared) is asked first: the first
    // whose controller accepts the use order (CheckOrder) or would accept it from where its
    // path ends (CheckOrderAt; its position without a path) does it, the rest are dropped one by
    // one; when none does, the nearest is chosen. With one unit that unit. The chosen unit's
    // brain's Player mode then runs CommandUse(object, kind, value, flag != 0). Returns 1.
    int OnUseOrder();

    // player_server_handlers.cpp: the rest of the client's messages. Each returns 1, or 0
    // to refuse (the dispatcher's fatal error).
    // The player leaves for zone `zone`, entrance `entrance` (joined players,
    // state 1, only): in campaign modes 1 and 3 a dead or missing leader runs slot 20 (the
    // leader's death) and, outside a network game, nothing more; the party is collected when
    // deployed (flag70_); state 3; the party's zone and entrance set; single player hands the
    // party to the party manager, multiplayer sends it (a lone hero's record gets id
    // 999999950); then message 0x08 (the zone with its zero, the entrance) and a flush.
    void LeaveToZone(const char* zone, int entrance);
    int OnRefreshParty();      // Message 0x02: ForgetKnownObjects, SendParty
    int OnTakeExit();          // Message 0x03: exit index -> GetZoneExit -> LeaveToZone (0 when none)
    int OnJoined();            // Message 0x05: state 2 -> 1, the session kind, Redeploy with the placements, +0x130 bit 4
    int OnLeaveToZone();       // Message 0x06: zone text, entrance -> LeaveToZone
    int OnBriefingDone();      // Message 0x07: briefing name -> CompleteBriefing(player id)
    // Message 0x08: a party (LoadParty with variables); one of a single record gets a fresh party
    // id (PartyNextId into the player's +0xFC), its record the next free object id (from 1000000013),
    // and is stored in the party manager's map. 0 for any other record count.
    int OnNewParty();
    int OnResume();            // Message 0x09: state 4 -> 1; Redeploy (no placements) unless deployed
    // Messages 0x30 / 0x3A: unit ids (missing or blocked ones left out), a run flag and
    // a point; each unit's Player mode walks / goes (CommandWalk / CommandGoTo) to the point plus
    // its formation offset (FormationOffsets).
    int OnWalk();
    int OnGoTo();
    // Message 0x31: unit ids, two flags, an object id; each unit's Player mode attacks it.
    int OnAttack();
    int OnPartyUpdate();       // Message 0x50: a sized block loaded into the party (no variables)
    // player_server_orders.cpp: the last client messages. Units are the player's world's
    // "able" means present, not blocked (flag 0x800000) and not dead (slot 55).
    int OnLeave();             // Message 0x01: the connection's Leave
    // Message 0x04 (joined players, state 1, else 0): the game state into the player's +0x100,
    // the party collected (CollectParty with the placements) and sent, message 0x07 (the state and
    // the world tick), dirty bit 0x10 cleared, state 2.
    int OnGameStateRequest();
    // Message 0x10: a pause byte. A multiplayer client that is also the attached game server,
    // asking to pause: only the session leader (its id the party's first record's unit) may, and
    // its name is announced (text 0xC); then the world's SetPaused(pause != 0).
    int OnPause();
    // Messages 0x33 / 0x32: {spell, unit id, target id / point, flag byte}: an able unit
    // with a Player mode casts at the object (world slot 40) / point (CommandCast / CommandCastAt).
    int OnCastAtObject();
    int OnCastAtPoint();
    // Messages 0x34 / 0x35: {unit id, byte}: an able unit's wanted weapon slot / a living
    // unit's movement style (no blocked test).
    int OnWantedWeapon();
    int OnMoveStyle();
    int OnUnitLookup();        // Message 0x38: two dwords, the first looked up as a unit (unused)
    // Message 0x39: {unit id, leader id}: a unit's Player mode follows the leader (may be null).
    int OnFollow();
    // Message 0x3B: a unit id, then a byte: CUnit::SetUnknown438(signed byte) when the unit exists.
    int OnUnitFlag438();
    int OnLastPosition();      // Message 0x40: 8 bytes into lastPosition_ x, y
    // Message 0x00: the login (PlayerLoginMessage). 0 unless the state is 0 (after the read).
    // A client version other than 0x64: message 0x11 with the server's "SrvErrMsg" /
    // "WrongClientVersion" text (formatted with 1, 0), state 3, 0. 0 when the name or password
    // is not printable text (IsPrintableText(.., 0x3F, 1)). By game mode: 1 the party manager's
    // EndRedeploy and OpenEdgeExits on the party; 2 / 3 the party stored under +0xFC loaded
    // (PartyMapLoad; 0 when it is not stored); 4 Clear(1, 0). Then message 0x00 with the player's
    // id, SendParty, state 1, 1.
    int OnLogin();
    // Message 0x83: the party saved without its variables as a sized block.
    void SendPartyUpdate();
    // Called from the player state tick. A joined, deployed player's party is sent
    // again: "Resending party of player <id>" to the log, the records synced, SendParty,
    // then the follow-up call.
    void ResendParty();
    // From the briefing screen. Message 0x19: a byte and a dword.
    void SendPair19(std::uint8_t a, std::uint32_t b);

    // The connection virtuals' bodies (player_messages.cpp unless noted).
    // Virtuals 3 and 15, and CreateServer's first part: the outgoing
    // messages emptied, both sent arrays released and freed, state 0, the last position 0 and
    // +0x44 -1, both groups emptied, +0x24 -1, no refresh pending, flag70_ 0, the placements
    // emptied, both coders reset, the next party refresh in 900..1800 updates (ISAAC). (Not
    // CEngineObject::ClearState, slot 5.)
    void ClearServerState();
    // ForgetSentObjects (virtual 13). Both sent arrays released and freed, both groups emptied.
    void ReleaseSentObjects();
    // Group50F8_ emptied.
    void ClearGroup50F8();
    // A joined player (state 1) collects its
    // party when deployed (flag70_, then cleared) and, in a multiplayer game, sends it
    // (SendParty); then state 3 with message 0x01 unless already 3, and Flush.
    void LeaveAndFlush();
    // Message 0x0E about a path of `unit` (nothing for null): kind, flag,
    // the point's two dwords, the unit's id and `limitBits`; except for kind 3 the path
    // written through a delta field on the stack, its channel 0 output appended.
    void SendPathMessage(CEngineObject* unit, PathOrder* path, const float* point, int kind, int flag,
                         std::uint32_t limitBits);

    // +0x24 -1, or the unit a Possession spell took (its id): the next party refresh then
    // sends group50F8_ (the target and what it sees) and sets it back to -1. Sent with message 0x60.
    std::uint32_t possessedId_;
    // The outgoing messages emptied, both sent-object
    // arrays released and freed, state 0, the last position 0 and +0x44 -1, both unit groups
    // emptied, +0x24 -1, no refresh pending, +0x70 0, the placements emptied, both
    // coders reset, the next party refresh in 900..1800 updates (ISAAC), then the handler table:
    // 256 entries, zeroed, and the 25 handlers by message type (their game addresses). Returns 1.
    BOOL CreateServer();

    // +0x28 the message handlers by type (0: refused; 256 of them).
    using Handler = int(__fastcall*)(CPlayerServer*, void*);
    lib::AlignedArray<Handler> handlers_;
    Vector3 lastPosition_;          // +0x38 sent with 0x04 / 0x0F
    int spare_;                     // +0x44 -1 after Create; nothing reads it
    BitStream out_;                 // +0x48 messages to the client (CMemoryStream)
    std::uint8_t flag70_;           // +0x70 OnLeaderDied collects the party when set, redeploys it when clear
    std::uint8_t unused_71[3];
    // +0x74 the unit placements (an MFC CArray).
    mfc::CArray<UnitPlacement> placements_;
    CoderState compressor_;         // +0x88 for the messages sent (Flush)
    CoderState decompressor_;       // +0x28B0 for the messages received
    PlayerObjectArray sentObjects_; // +0x50D8 map objects the client has (counted)
    PlayerObjectArray sentTimed_;   // +0x50E8 timed objects the client has (counted)
    ScriptGroup group50F8_;         // +0x50F8
    ScriptGroup partyUnits_;        // +0x510C units always sent (message 0x60; the timed objects' slot 20)
    int refreshPending_;            // +0x5120 0, 1 or 2 (SendPartyState)
    int refreshTimer_;              // +0x5124 updates until the party is sent again
};

// The client's login message to the world server (not net::LoginMessage; 0x88 bytes, read whole by CPlayerServer::OnLogin).
struct PlayerLoginMessage {
    int version;                    // +0x00 0x64
    char name[0x40];                // +0x04
    char password[0x40];            // +0x44
    std::uint32_t partyId;          // +0x84 the client player's partyId_ (cleared before the read)
};
static_assert(sizeof(PlayerLoginMessage) == 0x88);

// 1 when the first `maxLength`
// characters of `text` are printable (the game's isprint, of the signed char); a terminator
// before that gives whether at least `minLength` characters came first; 0 at the first
// unprintable one. With EI_UTF8 by character: a valid UTF-8 sequence of a printable code point each
// (lib/text_case.h), `minLength` characters.
BOOL __fastcall IsPrintableText(const char* text, int maxLength, int minLength);

// |a - b| squared, unrounded in ST0 (the use order's nearest-unit test).
double __fastcall UseOrderDistanceSquared(const Vector3* a, const Vector3* b);
// The walk orders' formation: one unit gets (0, 0, 0); several get
// the unit vector (x, y, 0) from the units' 2D centroid to each (unnormalised when it is zero).
// `offsets` (CArray<Vector3>) is sized to the units; nothing without units.
void __fastcall FormationOffsets(const mfc::CArray<CUnit*>* units, mfc::CArray<Vector3>* offsets);


// A world player ("CPlayer", class id 0xF; 0x134 bytes).
// player.cpp: the constructor, destructor, Create / Destroy and the item and unit lists. The harness's digest reads
// its fields (tests/verify/harness_digest.cpp), hence the layout asserts.
class CPlayer final : public CEngineObject {
public:
    // The world objects the player knows, by id, each counted once (player_sync.cpp): the map
    // objects and the timed objects (effects).
    using MapObjectMap = stlp::hash_map<int, CMapObject*, stlp::IntHash>;
    using TimedObjectMap = stlp::hash_map<int, CEffect*, stlp::IntHash>;
    using UnitMap = stlp::hash_map<int, CUnit*, stlp::IntHash>;

    // The maps (NextPrime(100) buckets each; their functor bytes are left as they are), the party
    // (constructed, then created), the lists (block size 10), then what DestroyPlayer leaves.
    CPlayer();
    // The deleting destructor. DestroyPlayer, the party destroyed (slot 3), the lists emptied, the party
    // destructed, the maps freed, the link released; CEngineObject's last.
    ~CPlayer() override;

    int Create() override { return CreatePlayer(); }
    void Destroy() override { DestroyPlayer(); }
    const char* GetClassName() override;               // "CPlayer"
    int GetClassId() override;                         // 0xF

    // DestroyPlayer; returns 1.
    BOOL CreatePlayer();
    // Destroy (virtual 3) and the destructor's copy. The recharge list emptied, Clear,
    // the party destroyed and created again (slots 3, 2); +0x130 0, id -1, +0x108 0; the link
    // disconnected (slot 11) and released, the connection detached and released (slot 4);
    // +0x110 -1, +0x100 and +0x104 0.
    void DestroyPlayer();
    // The given items released and the list emptied, both known-object maps cleared
    // every unit taken away (CUnit::SetPlayer(null), until the first is null), the
    // unit map emptied (max id 0), the recharge list emptied, then the connection's slot 13.
    void Clear();
    // The old connection loses its owner and a reference (slot 4); the new one
    // gets this player as owner and a reference.
    void SetConnection(CPlayerConnection* connection);
    void SetPlayerId(int id);
    // With a link the connection leaves (slot 16); Clear; the link disconnected
    // (slot 11) and released; `link` taken (counted); then the connection's slot 15.
    void Connect(netlink::NetLink* link);
    // As Connect without the new link and the slot 15 call.
    void Disconnect();
    // Both known-object maps cleared, the connection's slot 13, +0x130 -1.
    void ForgetKnownObjects();

    // Whether `unit` is the party's leader: there are records and its id is the
    // first record's key (+0x1C). 0 for null.
    BOOL IsLeader(const CEngineObject* unit);
    // The connection's slot 17 with both arguments, then every item of `items`
    // counted and appended to the given items.
    void GiveItems(const mfc::CTypedPtrList<CItem>* items, int flag);
    // The connection's slot 18, then every given item taken off the head and released.
    void TakeAllItems();
    // 1 when a given loot item (0x3009) has `kind` (CItemLoot::kind_), or an item
    // a unit of the player carries has it at +0x4C (any class; the walk stops at a null unit).
    BOOL HasLoot(int kind);
    // The first given loot item of `kind` / item with id `id`: the
    // connection's slot 19, out of the list, released; 1, or 0 when there is none.
    BOOL TakeLoot(int kind);
    BOOL TakeItem(int id);
    // A unit not yet in units_ is counted, raises the max id and is put in;
    // then the connection's slot 14. Nothing for null or a known unit.
    void AddUnit(CUnit* unit);
    // A unit in units_ is erased, released (slot 4) and given to the
    // connection's slot 14.
    void RemoveUnit(CUnit* unit);
    // The item's id joins the recharge list unless there; the connection's
    // slot 22.
    void ItemUsed(CEngineObject* item);
    // The recharge list emptied, then the party assigned from `source`
    // (CParty::Assign, whose result is returned).
    CParty* AssignParty(CParty* source);
    // Every unit at a taming stage (its controller's slot 39) leaves the player
    // (CUnit::SetPlayer(null)); the walk stops at a null entry.
    void ReleaseTamedUnits();

    // Ticks the objects in the list at +0x114 (slot 6; looked up by id through the
    // party's slot 18 manager) and drops the ones that are gone or whose tick returned 0, then runs
    // slot 21 of the connection.
    void Tick();

    // The embedded party (party_storage.h).
    CParty* Party() { return party_.Get(); }
    // The connection as the server's view of it (a CPlayerServer on the server world, null for
    // none).
    CPlayerServer* Server() const { return static_cast<CPlayerServer*>(connection_); }

    netlink::NetLink* link_;                    // +0x1C the connection to the player's client, null: none
    CPlayerConnection* connection_;             // +0x20 CPlayerServer / CPlayerClient (counted)
    int playerId_;                              // +0x24 player number (0 = the local player)
    MapObjectMap knownObjects_;                      // +0x28 map objects sent to the client
    int maxObjectId_;                           // +0x3C highest id put in knownObjects_
    UnitMap units_;                             // +0x40 the player's units by id (counted)
    int maxUnitId_;                             // +0x54 highest id put in units_
    TimedObjectMap knownTimed_;                    // +0x58 timed objects (effects) sent to the client
    int maxTimedId_;                            // +0x6C
    PartyStorage party_;                        // +0x70 an embedded CParty; slot 18 its item manager
    mfc::CTypedPtrList<CItem> items_;           // +0xE0 items given to the player (counted; CPtrList)
    int partyId_;                               // +0xFC the key of the party in the party manager (PartyNextId); sent with the client's login
    int sessionKind100_;                        // +0x100 3: the client runs a setup on the controllers of new units
    std::uint32_t lastScreen_;                  // +0x104 the session kind before the last game-state message (5: the camp)
    std::uint8_t godMode_;                      // +0x108 the script's GodMode(player, on); the combat code tests it
    int logTraffic_;                            // +0x10C logs the size of every flush
    int lastTick_;                              // +0x110 -1 after Create; the world tick last saved or restored for the player
    mfc::CList<std::uint32_t> recharging_;      // +0x114 ids of items recharging (saved with the world from version 0x55)
    std::uint32_t dirty_;                       // +0x130 state to send (bits the server's state messages clear); -1 after the reset
};
static_assert(offsetof(CPlayer, link_) == 0x1C);
static_assert(offsetof(CPlayer, knownObjects_) == 0x28);
static_assert(offsetof(CPlayer, maxUnitId_) == 0x54);
static_assert(offsetof(CPlayer, maxTimedId_) == 0x6C);
static_assert(offsetof(CPlayer, party_) == 0x70);
static_assert(offsetof(CPlayer, items_) == 0xE0);
static_assert(offsetof(CPlayer, godMode_) == 0x108);
static_assert(offsetof(CPlayer, logTraffic_) == 0x10C);
static_assert(offsetof(CPlayer, recharging_) == 0x114);
static_assert(offsetof(CPlayer, connection_) == 0x20);
static_assert(offsetof(CPlayer, units_) == 0x40);
static_assert(offsetof(CPlayer, partyId_) == 0xFC);
static_assert(offsetof(CPlayer, sessionKind100_) == 0x100);
static_assert(offsetof(CPlayer, lastScreen_) == 0x104);
static_assert(offsetof(CPlayer, lastTick_) == 0x110);
static_assert(offsetof(CPlayer, dirty_) == 0x130);
static_assert(sizeof(CPlayer) == 0x134);

// The message dispatchers' fatal error: `format` with the previous and the current
// message type (their low bytes); the FatalError result.
int __fastcall PlayerMessageError(const char* format, std::uint32_t previous, std::uint32_t type);
