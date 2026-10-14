#pragma once

// The server side of a player's connection (CPlayerServer, vtable 00743280; 0066C730-00671080) as far
// as the message writers use it, and the party record lookup. Each writer appends one message to
// the outgoing stream at +0x48 (a CMemoryStream; its slot 2 writes bytes): a type byte, then the
// payload. The single-player game runs the same server / client pair, so the per-tick player state
// (0058B750), script variables and unit events all go through them. Names are provisional; see
// docs/notes/objects.md.

#include "engine_object.h"
#include "maths.h"
#include "net_delta.h"
#include "script.h"

#include "lib/containers.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

class CPlayer;
class CParty;   // party.h
class CUnit;    // unit.h
namespace netlink {
class NetLink;  // net_link.h
}

// A pointer array of the sync (SyncPlayer's statics, the player state's sent objects): data on a
// 32-byte boundary (0047B940), size, capacity, grow-by.
using PlayerObjectArray = lib::AlignedArray<CEngineObject*>;

// One row of the server's and the client's handler tables (player_messages.cpp, player_client.cpp):
// a message type and the address of its thiscall handler.
struct HandlerEntry {
    std::uint8_t type;
    std::uintptr_t address;
};
static_assert(sizeof(HandlerEntry) == 8 && offsetof(HandlerEntry, address) == 4);

// A unit placement (0x28 bytes; built by 004CEE20): the unit's id, position, rotation and two
// values (CWorldServer::StoreParty stores the unit's +0x268 / +0x26C, CreateUnits restores them).
struct UnitPlacement {
    int id;
    std::uint32_t position[3];      // +0x04 (float bits)
    std::uint32_t rotation[4];      // +0x10
    std::uint32_t value20;          // +0x20
    std::uint32_t value24;          // +0x24
};
static_assert(sizeof(UnitPlacement) == 0x28);

class CPlayerServer {
public:
    // Variables and state.
    void SetVariable(const char* name, std::uint32_t value);        // 0066EE10: message 0x81
    void SetVariableMax(const char* name, float value);             // 0066EE80: raises a variable, 0x81
    void DeleteVariable(const char* name);                          // 0066EF30: message 0x82
    void SendGameTime();                                            // 0066F180: message 0x07
    int SendState02(int value);                                     // 0066EBD0: message 0x02 (0047DA80 payload)
    int SendState70(std::uint8_t a, std::uint16_t b);               // 0066EC10: message 0x70
    int SendState71(std::uint32_t a, std::uint32_t b);              // 0066EC70: message 0x71
    int SendState72(std::uint32_t x, std::uint32_t y, std::uint32_t z, std::uint32_t w);   // 0066ECD0: 0x72
    void SendState73(void* source);                                 // 0066F240: message 0x73 (006D4570 payload)
    int SendPosition04(const Vector3* at, std::uint32_t a, std::uint32_t b);   // 0066ED30: message 0x04
    std::uint32_t SendPosition0F(const Vector3* at, const Vector3* other, std::uint8_t flag);   // 0066EDA0: 0x0F
    // Simple messages.
    void Send17();                                                  // 0066EA10 (slot 18)
    void SendObject0A(const void* object);                          // 0066EA30 (slot 19): id
    void SendPair16(std::uint32_t a, std::uint32_t b);              // 0066EA70
    void SendObjects0C(const void* list);                           // 0066EAC0: ids and +0x28 of a list
    void SendObject14(const void* object);                          // 0066EB40 (slot 22): id, +0x28
    void SendText0D(const char* text);                              // 0066EB90
    void SendText08(const char* text, std::uint32_t value);         // 0066F1F0
    void SendValue15(std::uint32_t value);                          // 0066F280
    void SendPair18(std::uint32_t a, std::uint32_t b);              // 0066F2C0
    void SendObject43(const void* object);                          // 0066D6A0: id, nothing without an object
    void SendObject0B(const void* object, std::uint32_t value);     // 0066D6E0: id, value
    // 0066D730 (RET 8; A-Z1): message 0x12, a byte and `value` (the 2nd argument; the first is not
    // read). The original never writes the byte: whatever its stack holds goes out. Ours sends 0;
    // the client's handler (00667E20) skips the byte.
    void SendValue12(std::uint32_t unused, std::uint32_t value);
    void SendText13(std::uint32_t unused, const char* text);        // 0066D770: the text is the 2nd argument
    void SendText(std::uint8_t type, const char* text);             // 0066D7B0
    void WriteTo(CStream* target);                                  // 0066D230: target->CopyFrom(out_) (slot 7)
    void SetDisconnect();                                           // 0066D250: state 4
    void SetFlag70(std::uint8_t value);                             // 0066D260

    // The per-tick sync (player_sync.cpp; called by CWorldServer::SyncPlayer, world_sync.cpp).
    // 0066D100. Compresses the outgoing messages (the coder at +0x88) and sends them on the player's
    // link (slot 8); the messages are emptied. The send's result (a bool), 0 without a link.
    int Flush();
    // 0066D7F0 / 0066DDE0 (RET 4). Each new map object (message 0x40: header, scale, names, the
    // server companion's names and strings, a unit's name and +0x23C, its full state) / timed
    // object (0x50: id, class, full state) is sent and kept, counted, in the player's map. 1.
    int SendObjectsAdded(const PlayerObjectArray* objects);
    int SendTimedAdded(const PlayerObjectArray* objects);
    // 0066DBA0 / 0066E020 (RET 4). The objects changed in this world update (their stamp equals the
    // world's update count) send their delta (0x41 / 0x51: id, then the delta stream). 1.
    int SendObjectsChanged(const PlayerObjectArray* objects);
    int SendTimedChanged(const PlayerObjectArray* objects);
    // 0066DAD0 / 0066DF40 (RET 4). Objects gone from view: dropped from the player's map and
    // released (0x42 / 0x52: id). 1.
    int SendObjectsRemoved(const PlayerObjectArray* objects);
    int SendTimedRemoved(const PlayerObjectArray* objects);
    // 0066F080. Every item of the player's item list (+0x114) the party's item manager knows is
    // sent (SendObject14, slot 22).
    void SendItems();
    // 0066E7D0 (RET 8; A-H10). For a joined player (state 1): message 0x05, a stream with the
    // given items' spells and then the items (0069FB60 gathers them, 0055E8A0 writes each list),
    // and message 0x06: the party's money, the flag's low byte, the items' ids and -1.
    void SendItemsGiven(const mfc::CTypedPtrList<CEngineObject>* items, int flag);
    // 0066E430. Message 0x60: the world's +0x6C flag, +0x24, the party units (partyUnits_) by id and
    // whether the client should refresh the party (refreshPending_: 1 when a unit of the player has
    // a level at most 2, 2 when a party unit fights or is the enemy of a hostile neighbour), then the
    // objects shared with a multiplayer client's single hero. Returns 1.
    int SendPartyState();
    // 0066DC90 (RET 4). A map object leaves the world: when the client knows it, it is dropped from
    // the player's map and the sent list (each drop releases it) and message 0x42 goes out; it
    // leaves both groups in any case. Returns 1.
    int ForgetObject(CEngineObject* object);
    // 0066E110 (RET 4). Whether `object` is one of the party units (partyUnits_).
    BOOL IsPartyUnit(CEngineObject* object);
    // 0066EF80. Message 0x80: the party saved (with its variables) as a sized block; the next
    // refresh in 900..1800 updates (ISAAC).
    void SendParty();
    // 0066D270 (slot 23). Every packet waiting on the link is decompressed (+0x28B0) into the
    // received stream (007C83D0) and its messages run through the handlers; before the player has
    // joined (state 0) only types 0 and 8, the rest of such a packet is dropped. A read past the
    // end, a type without a handler or a refusing handler ends in the fatal error 00664B70 (its
    // result returned). 0 when leaving (state 3, also after a handler) or without a link, else 1.
    int ReceiveMessages();
    // 0066E130. The party units (partyUnits_) recomputed: with +0x24 at -1, group50F8_ plus the
    // player's units, the units they see and what they know (the brains' +0x12C lists), and every
    // brain then knows all of it; otherwise partyUnits_ becomes a copy of group50F8_ and +0x24 -1.
    void UpdatePartyUnits();

    // player_server_handlers.cpp (A-H13).
    // 0066F4C0 (slot 20). The player's leader died. Outside a network game (IsMultiplayerClient):
    // message 0x09, then 0x0B (the hero's id, 0xFF) when the first record's unit exists. In one,
    // a party of one hero is revived: it loses a share (007C5490) of the money, which goes with the
    // player's given items into a shared object (AddSharedObject, the old id, then TakeAllItems);
    // the record gets a new id (the old one, plus 1000000 when below 1001000000, then the first
    // id from there that no world object has), the slots restored and a share (007C5494) of its experience above its NPC record's floor taken off.
    // The party collected (when deployed, flag70_) and sent (SendParty), lastPosition_.z
    // -1313, redeployed (Redeploy with scripts) and the new hero's brain gets the units (class
    // 0x50) the old one knew (+0x12C) and the old hero, each once and counted.
    void OnLeaderDied();
    // 00670310 (message 0x36, from the client's ViewOrderTarget 0066AA20 / dead-unit order
    // 0066AB80). Unit ids (a count, then the ids; units missing or blocked, flag 0x800000, are
    // left out), a flag, a kind, a value and the target object's id. With two units or more the
    // one nearest to the order's aim point (UseOrderDistanceSquared) is asked first: the first
    // whose controller accepts the use order (CheckOrder) or would accept it from where its
    // path ends (CheckOrderAt; its position without a path) does it, the rest are dropped one by
    // one; when none does, the nearest is chosen. With one unit that unit. The chosen unit's
    // brain's Player mode then runs CommandUse(object, kind, value, flag != 0). Returns 1.
    int OnUseOrder();

    // player_server_handlers.cpp (A-H14): the rest of the client's messages. Each returns 1, or 0
    // to refuse (the dispatcher's fatal error).
    // 0066F310 (RET 8). The player leaves for zone `zone`, entrance `entrance` (joined players,
    // state 1, only): in campaign modes 1 and 3 a dead or missing leader runs slot 20 (the
    // leader's death) and, outside a network game, nothing more; the party is collected when
    // deployed (flag70_); state 3; the party's zone and entrance set; single player hands the
    // party to the party manager (004A8890), multiplayer sends it (a lone hero's record gets id
    // 999999950); then message 0x08 (the zone with its zero, the entrance) and a flush.
    void LeaveToZone(const char* zone, int entrance);
    int OnRefreshParty();      // 0066FAA0, 0x02: ForgetKnownObjects, SendParty
    int OnTakeExit();          // 00670C20, 0x03: exit index -> GetZoneExit -> LeaveToZone (0 when none)
    int OnJoined();            // 00670DA0, 0x05: state 2 -> 1, the session kind, Redeploy with the placements, +0x130 bit 4
    int OnLeaveToZone();       // 00670E00, 0x06: zone text, entrance -> LeaveToZone
    int OnBriefingDone();      // 00670EB0, 0x07: briefing name -> CompleteBriefing(player id)
    // 00670F50, 0x08: a party (LoadParty with variables); one of a single record gets a fresh party
    // id (PartyNextId into the player's +0xFC), its record the next free object id from 00794E3C,
    // and is stored in the party manager's map. 0 for any other record count.
    int OnNewParty();
    int OnResume();            // 00671060, 0x09: state 4 -> 1; Redeploy (no placements) unless deployed
    // 0066FFF0 / 0066FBD0, 0x30 / 0x3A: unit ids (missing or blocked ones left out), a run flag and
    // a point; each unit's Player mode walks / goes (CommandWalk / CommandGoTo) to the point plus
    // its formation offset (FormationOffsets).
    int OnWalk();
    int OnGoTo();
    // 00670190, 0x31: unit ids, two flags, an object id; each unit's Player mode attacks it.
    int OnAttack();
    int OnPartyUpdate();       // 00670B60, 0x50: a sized block loaded into the party (no variables)
    // player_server_orders.cpp (A-H23): the last client messages. Units are the player's world's
    // (0055FB00); "able" means present, not blocked (flag 0x800000) and not dead (slot 55).
    int OnLeave();             // 0066FA90, 0x01: the connection's slot 16 (Leave)
    // 00670CE0, 0x04 (joined players, state 1, else 0): the game state into the player's +0x100,
    // the party collected (CollectParty with the placements) and sent, message 0x07 (the state and
    // the world tick), dirty bit 0x10 cleared, state 2.
    int OnGameStateRequest();
    // 0066FAC0, 0x10: a pause byte. A multiplayer client that is also the attached game server,
    // asking to pause: only the session leader (its id the party's first record's unit) may, and
    // its name is announced (text 0xC); then the world's SetPaused(pause != 0).
    int OnPause();
    // 00670820 / 006708D0, 0x33 / 0x32: {spell, unit id, target id / point, flag byte}: an able unit
    // with a Player mode casts at the object (world slot 40) / point (CommandCast / CommandCastAt).
    int OnCastAtObject();
    int OnCastAtPoint();
    // 00670960 / 006709D0, 0x34 / 0x35: {unit id, byte}: an able unit's wanted weapon slot / a living
    // unit's movement style (no blocked test).
    int OnWantedWeapon();
    int OnMoveStyle();
    int OnUnitLookup();        // 00670A40, 0x38: two dwords, the first looked up as a unit (unused)
    // 00670A90, 0x39: {unit id, leader id}: a unit's Player mode follows the leader (may be null).
    int OnFollow();
    // 0066FB70, 0x3B: a unit id, then a byte: CUnit::SetUnknown438(signed byte) when the unit exists.
    int OnUnitFlag438();
    int OnLastPosition();      // 00670B20, 0x40: 8 bytes into lastPosition_ x, y
    // 0066F890, 0x00 (A-H19): the login (LoginMessage). 0 unless the state is 0 (after the read).
    // A client version other than 0x64: message 0x11 with the server's "SrvErrMsg" /
    // "WrongClientVersion" text (formatted with 1, 0), state 3, 0. 0 when the name or password
    // is not printable text (IsPrintableText(.., 0x3F, 1)). By game mode: 1 the party manager's
    // EndRedeploy and OpenEdgeExits on the party; 2 / 3 the party stored under +0xFC loaded
    // (PartyMapLoad; 0 when it is not stored); 4 Clear(1, 0). Then message 0x00 with the player's
    // id, SendParty, state 1, 1.
    int OnLogin();
    // 0066F0C0. Message 0x83: the party saved without its variables as a sized block.
    void SendPartyUpdate();
    // 0066F450 (from the player state tick 0058B750). A joined, deployed player's party is sent
    // again: "Resending party of player <id>" to the log, the records synced (00590C70), SendParty,
    // then 00591070.
    void ResendParty();
    // 0066F840 (RET 8; the briefing screen). Message 0x19: a byte and a dword.
    void SendPair19(std::uint8_t a, std::uint32_t b);

    // A-H15: the lifetime and the connection slots (player_messages.cpp unless noted).
    // 0066C540. Constructor body (0x5128 bytes, made by the server's specific manager): the
    // engine object's fields, an empty handler table, the outgoing stream (a queue stream), the
    // placements, both coders, the sent arrays and groups, then ClearState. Returns this.
    CPlayerServer* Construct();
    // 0066C770. Destructor body: ClearState (an ISAAC draw, as the original), the groups
    // destroyed, the sent arrays' blocks freed, both coders' output ended, the placements' and
    // the stream's storage freed, the handler table's block freed, then CEngineObject's.
    void DestructBody();
    // 0066C750 (slot 0, RET 4). DestructBody, then operator delete when bit 0 of `flags` is set.
    CPlayerServer* DeletingDestruct(unsigned flags);
    // 0066CC50 (slot 3) and 0066CF20 (slot 15), and CreateServer's first part: the outgoing
    // messages emptied, both sent arrays released and freed, state 0, the last position 0 and
    // +0x44 -1, both groups emptied, +0x24 -1, no refresh pending, flag70_ 0, the placements
    // emptied, both coders reset, the next party refresh in 900..1800 updates (ISAAC).
    void ClearState();
    // 0066CE50 (slot 13). Both sent arrays released and freed, both groups emptied.
    void ForgetSentObjects();
    // 0066D090 (slot 21). group50F8_ emptied.
    void ClearGroup50F8();
    // 0066D020 (slot 16; player_server_handlers.cpp). A joined player (state 1) collects its
    // party when deployed (flag70_, then cleared) and, in a multiplayer game, sends it
    // (SendParty); then state 3 with message 0x01 unless already 3, and Flush.
    void Leave();
    // 0066D490 (RET 0x18). Message 0x0E about a path of `unit` (nothing for null): kind, flag,
    // the point's two dwords, the unit's id and `limitBits`; except for kind 3 the path
    // (005C7DB0) written through a delta field on the stack, its channel 0 output appended.
    void SendPathMessage(CEngineObject* unit, void* path, const float* point, int kind, int flag,
                         std::uint32_t limitBits);

    CPlayer* Owner() const { return player_; }

    void* vtable_;
    std::uint8_t engineObject_[0x1C - 0x04];
    CPlayer* player_;               // +0x1C the world player
    int state_;                     // +0x20
    // +0x24 -1, or the unit a Possession spell took (its id): the next party refresh (0066E130) then
    // sends group50F8_ (the target and what it sees) and sets it back to -1. Sent with message 0x60.
    std::uint32_t possessedId_;
    // 0066C9A0 (slot 2; player_messages.cpp). The outgoing messages emptied, both sent-object
    // arrays released and freed, state 0, the last position 0 and +0x44 -1, both unit groups
    // emptied, +0x24 -1, no refresh pending, +0x70 0, the placements emptied (0059C3D0), both
    // coders reset, the next party refresh in 900..1800 updates (ISAAC), then the handler table:
    // 256 entries, zeroed, and the 25 handlers by message type (their game addresses). Returns 1.
    BOOL CreateServer();

    // +0x28 the message handlers by type (thiscall, 0: refused), built by 0066C9A0 (256 of them).
    using Handler = int(__fastcall*)(CPlayerServer*, void*);
    lib::AlignedArray<Handler> handlers_;
    Vector3 lastPosition_;          // +0x38 sent with 0x04 / 0x0F
    int spare_;                     // +0x44 -1 after Create; nothing reads it
    BitStream out_;                 // +0x48 messages to the client (CMemoryStream)
    std::uint8_t flag70_;           // +0x70 OnLeaderDied collects the party when set, redeploys it when clear
    std::uint8_t unused_71[3];
    // +0x74 the unit placements (an MFC CArray; SetSize 0059C3D0, ConstructElements 004CEE20).
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
static_assert(offsetof(CPlayerServer, sentObjects_) == 0x50D8);
static_assert(offsetof(CPlayerServer, partyUnits_) == 0x510C);
static_assert(offsetof(CPlayerServer, refreshTimer_) == 0x5124);
static_assert(offsetof(CPlayerServer, player_) == 0x1C);
static_assert(offsetof(CPlayerServer, compressor_) == 0x88);
static_assert(offsetof(CPlayerServer, decompressor_) == 0x28B0);
static_assert(offsetof(CPlayerServer, lastPosition_) == 0x38);
static_assert(offsetof(CPlayerServer, out_) == 0x48);
static_assert(offsetof(CPlayerServer, flag70_) == 0x70);
static_assert(offsetof(CPlayerServer, handlers_) == 0x28);
static_assert(offsetof(CPlayerServer, placements_) == 0x74);

// The client's login message (0x88 bytes, read whole by CPlayerServer::OnLogin).
struct LoginMessage {
    int version;                    // +0x00 0x64
    char name[0x40];                // +0x04
    char password[0x40];            // +0x44
    std::uint32_t partyId;          // +0x84 the client player's partyId_ (cleared before the read)
};
static_assert(sizeof(LoginMessage) == 0x88);

// 0047ADA0 (__fastcall, RET 4; player_server_handlers.cpp). 1 when the first `maxLength`
// characters of `text` are printable (the game's isprint, of the signed char); a terminator
// before that gives whether at least `minLength` characters came first; 0 at the first
// unprintable one.
BOOL __fastcall IsPrintableText(const char* text, int maxLength, int minLength);

// 00670670 (__fastcall). |a - b| squared, unrounded in ST0 (the use order's nearest-unit test).
double __fastcall UseOrderDistanceSquared(const Vector3* a, const Vector3* b);
// 0066FD70 (__fastcall; A-H14). The walk orders' formation: one unit gets (0, 0, 0); several get
// the unit vector (x, y, 0) from the units' 2D centroid to each (unnormalised when it is zero).
// `offsets` (CArray<Vector3>, vtable 0073C120) is sized to the units; nothing without units.
void __fastcall FormationOffsets(const mfc::CArray<CUnit*>* units, mfc::CArray<Vector3>* offsets);

// A world player's connection to its client (CPlayer::connection_): CPlayerServer (vtable 00743280)
// or CPlayerClient (00743180). Neither class derives from this declaration (their slot bodies are
// registered by address); it names the slots after CEngineObject's that the player calls.
class CPlayerConnection : public CEngineObject {
public:
    virtual void ForgetSentObjects();       // 13: server 0066CE50 (sent objects, party units); client empty
    virtual void UnitChanged(CUnit* unit);  // 14: client 00667BA0 (the unit's controller); server empty
    virtual void ResetState();              // 15: server 0066CF20, client 00667600
    virtual void Leave();                   // 16: server 0066D020 (state 3, flushed); client empty
    // 17: server 0066E7D0 (CPlayerServer::SendItemsGiven); client empty.
    virtual void ItemsGiven(const mfc::CTypedPtrList<CEngineObject>* items, int flag);
    virtual void ItemsTaken();              // 18: server 0066EA10 (message 0x17); client empty
    virtual void ItemTaken(CEngineObject* item);   // 19: server 0066EA30 (message 0x0A); client empty
    // 20: server 0066F4C0 (CPlayerServer::OnLeaderDied; CUnitServer::UpdateServer calls it when
    // the dying unit is the player's leader); client empty.
    virtual void LeaderDied();
    virtual void TickDone();                // 21: server 0066D090 (empties +0x50F8); client empty
    virtual void ItemUsed(CEngineObject* item);    // 22: server 0066EB40 (message 0x14); client empty
    virtual int Receive();                  // 23: server 0066D270, client 00667A30

    CPlayer* player_;   // +0x1C the owner (set by CPlayer::SetConnection)
    int state_;         // +0x20 both classes' state_ (1: logged in / joined)
};
static_assert(offsetof(CPlayerConnection, player_) == 0x1C);
static_assert(offsetof(CPlayerConnection, state_) == 0x20);
static_assert(offsetof(CPlayerServer, state_) == 0x20);

// A world player (vtable 00743104; "CPlayer", class id 0xF; 0x134 bytes, made by 0066C110).
// player.cpp: the constructor, destructor, Create / Destroy and the item and unit lists.
class CPlayer : public CEngineObject {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x00743104);
    // The world objects the player knows, by id, each counted once (player_sync.cpp).
    using ObjectMap = stlp::hash_map<int, CEngineObject*, stlp::IntHash>;

    // 00664BF0. Constructor body: the maps (NextPrime(100) buckets each; their functor bytes are
    // left as they are), the party (constructed, then created), the lists (block size 10), then
    // what DestroyPlayer leaves. Returns this.
    CPlayer* Construct();
    // 00664E20. Destructor body: DestroyPlayer, the party destroyed (slot 3), the lists emptied,
    // the party destructed, the maps freed, the link released; CEngineObject's last.
    void DestructBody();
    // 00664E00 (slot 0, RET 4). DestructBody, then operator delete when bit 0 of `flags` is set.
    CPlayer* DeletingDestruct(unsigned flags);
    // 00665010 (slot 2). DestroyPlayer; returns 1.
    BOOL CreatePlayer();
    // 006650E0 (slot 3) and 00665380 (the destructor's copy). The recharge list emptied, Clear,
    // the party destroyed and created again (slots 3, 2); +0x130 0, id -1, +0x108 0; the link
    // disconnected (slot 11) and released, the connection detached and released (slot 4);
    // +0x110 -1, +0x100 and +0x104 0.
    void DestroyPlayer();
    // 00665200. The given items released and the list emptied, both known-object maps cleared
    // (0047B670), every unit taken away (CUnit::SetPlayer(null), until the first is null), the
    // unit map emptied (max id 0), the recharge list emptied, then the connection's slot 13.
    void Clear();
    // 00665950 (RET 4). The old connection loses its owner and a reference (slot 4); the new one
    // gets this player as owner and a reference.
    void SetConnection(CPlayerConnection* connection);
    // 00665A60 (RET 4).
    void SetPlayerId(int id);
    // 00665990 (RET 4). With a link the connection leaves (slot 16); Clear; the link disconnected
    // (slot 11) and released; `link` taken (counted); then the connection's slot 15.
    void Connect(netlink::NetLink* link);
    // 00665A10. As Connect without the new link and the slot 15 call.
    void Disconnect();
    // 006651D0. Both known-object maps cleared (0047B670), the connection's slot 13, +0x130 -1.
    void ForgetKnownObjects();

    // 00665440 (RET 4). Whether `unit` is the party's leader: there are records and its id is the
    // first record's key (+0x1C). 0 for null.
    BOOL IsLeader(const CEngineObject* unit);
    // 00665470 (RET 8). The connection's slot 17 with both arguments, then every item of `items`
    // counted and appended to the given items.
    void GiveItems(const mfc::CTypedPtrList<CEngineObject>* items, int flag);
    // 00665550. The connection's slot 18, then every given item taken off the head and released.
    void TakeAllItems();
    // 00665640 (RET 4). 1 when a given loot item (0x3009) has `kind` (CItemLoot::kind_), or an item
    // a unit of the player carries has it at +0x4C (any class; the walk stops at a null unit).
    BOOL HasLoot(int kind);
    // 00665780 / 00665870 (RET 4). The first given loot item of `kind` / item with id `id`: the
    // connection's slot 19, out of the list, released; 1, or 0 when there is none.
    BOOL TakeLoot(int kind);
    BOOL TakeItem(int id);
    // 00665A70 (RET 4). A unit not yet in units_ is counted, raises the max id and is put in;
    // then the connection's slot 14. Nothing for null or a known unit.
    void AddUnit(CUnit* unit);
    // 00665B50 (RET 4). A unit in units_ is erased, released (slot 4) and given to the
    // connection's slot 14.
    void RemoveUnit(CUnit* unit);
    // 00665BE0 (RET 4). The item's id joins the recharge list unless there; the connection's
    // slot 22.
    void ItemUsed(CEngineObject* item);
    // 00665CC0 (RET 4). The recharge list emptied, then the party assigned from `source`
    // (CParty::Assign, whose result is returned).
    CParty* AssignParty(CParty* source);
    // 00665D20. Every unit at a taming stage (its controller's slot 39) leaves the player
    // (CUnit::SetPlayer(null)); the walk stops at a null entry.
    void ReleaseTamedUnits();

    // 00665DE0. Ticks the objects in the list at +0x114 (slot 6; looked up by id through the
    // party's slot 18 manager) and drops the ones that are gone or whose tick returned 0, then runs
    // slot 21 of the connection.
    void Tick();

    // The embedded party (party.h; the class is abstract in C++, so the member is its bytes).
    CParty* Party() { return reinterpret_cast<CParty*>(&party_); }

    netlink::NetLink* link_;                    // +0x1C the connection to the player's client, null: none
    CPlayerConnection* connection_;             // +0x20 CPlayerServer / CPlayerClient (counted)
    int playerId_;                              // +0x24 player number (0 = the local player)
    ObjectMap knownObjects_;                    // +0x28 map objects sent to the client
    int maxObjectId_;                           // +0x3C highest id put in knownObjects_
    ObjectMap units_;                           // +0x40 the player's units by id (counted)
    int maxUnitId_;                             // +0x54 highest id put in units_
    ObjectMap knownTimed_;                      // +0x58 timed objects (effects) sent to the client
    int maxTimedId_;                            // +0x6C
    alignas(4) std::uint8_t party_[0x70];       // +0x70 an embedded CParty; slot 18 its item manager
    mfc::CTypedPtrList<CEngineObject> items_;   // +0xE0 items given to the player (counted; CPtrList)
    std::uint32_t partyId_;                     // +0xFC the key of the party in the party manager (PartyNextId); sent with the client's login (00667680)
    int sessionKind100_;                        // +0x100 3: the client runs 00538DB0 on the controllers of new units (00667EC0)
    std::uint32_t lastScreen_;                  // +0x104 the session kind before the last game-state message (5: the camp)
    std::uint8_t godMode_;                      // +0x108 the script's GodMode(player, on); the combat code tests it
    int logTraffic_;                            // +0x10C logs the size of every flush
    int lastTick_;                              // +0x110 -1 after Create; the world tick last saved or restored for the player
    mfc::CTypedPtrList<void> objects_;          // +0x114 ids of items recharging (vtable 00743138)
    int dirty_;                                 // +0x130 state to send (bits the server's state messages clear); -1 after 006651D0
};
static_assert(offsetof(CPlayer, link_) == 0x1C);
static_assert(offsetof(CPlayer, knownObjects_) == 0x28);
static_assert(offsetof(CPlayer, maxUnitId_) == 0x54);
static_assert(offsetof(CPlayer, maxTimedId_) == 0x6C);
static_assert(offsetof(CPlayer, party_) == 0x70);
static_assert(offsetof(CPlayer, items_) == 0xE0);
static_assert(offsetof(CPlayer, godMode_) == 0x108);
static_assert(offsetof(CPlayer, logTraffic_) == 0x10C);
static_assert(offsetof(CPlayer, objects_) == 0x114);
static_assert(sizeof(CPlayer) == 0x134);
