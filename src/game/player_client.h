#pragma once

// The client side of a player's connection (CPlayerClient, vtable 00743180; 00666FD0-0066A880): it
// sends the client's requests (+0x5C) compressed to the server and runs the server's messages
// (+0x34) through its handler table (built by 00667230). The single-player game runs it next to
// CPlayerServer (player_messages.h) over the loopback link. Names are provisional; see
// docs/notes/objects.md.

#include "player_messages.h"

#include <cstddef>
#include <cstdint>

class CPlayerClient : public CPlayerConnection {
public:
    // player_client_orders.cpp (A-H14): the local view's orders, queued for the server (out_), each
    // a type byte and a payload; all return 1. Units go as their ids (-1 for a null entry).
    // 0066A7D0 / 0066A720 (RET 0xC). 0x30 walk / 0x3A go to: the units, `run`, the point.
    int SendWalk(const mfc::CArray<CUnit*>* units, const Vector3* to, int run);
    int SendGoTo(const mfc::CArray<CUnit*>* units, const Vector3* to, int run);
    // 0066A880 (RET 0x10). 0x31 attack: the units, `ctrl`, `how`, the target's id.
    int SendAttack(const mfc::CArray<CUnit*>* units, CEngineObject* target, int how, int ctrl);
    // 0066AC70 / 0066ACD0 (RET 0x10). 0x33 / 0x32 cast spell `slot` of `caster` on `target` / at
    // `point`, with the low byte of `ctrl`.
    int SendCastOnUnit(int slot, CUnit* caster, CEngineObject* target, int ctrl);
    int SendCastAtPoint(int slot, CUnit* caster, const Vector3* point, int ctrl);
    // 0066AD40 / 0066ADD0 (RET 0x10). The same with quick item `slot` (slot | 0x80000000); a
    // potion (category 8) with a count above 0 loses one at once.
    int SendItemOnUnit(int slot, CUnit* user, CEngineObject* target, int ctrl);
    int SendItemAtPoint(int slot, CUnit* user, const Vector3* point, int ctrl);
    // 0066AE70 / 0066AED0 (RET 8). 0x34 weapon slot / 0x35 movement mode: the unit's id and the
    // value's low byte; the unit keeps the value (+0x270 / moveMode2).
    int SetWeapon(CUnit* unit, int slot);
    int SetMoveMode(CUnit* unit, int mode);
    // 0066AF30 (RET 8). CUnit::SetUnknown438(value), then 0x3B: the id and the value's low byte.
    int SetUnitFlag438(CUnit* unit, int value);
    // 0066AFE0 (RET 8). 0x39: the unit's id and the target's (-1 for none).
    int SendFollow(CUnit* unit, CEngineObject* target);
    // 0066B040 (RET 4). 0x10: whether `value` (a byte) is set.
    int SendFlag10(std::uint8_t value);
    // A-H15.
    // 0066AA20 (RET 0x14). 0x36 use order for one unit (count 1, its id or -1): `flag`, `kind`,
    // `value`, the target's id (CPlayerServer::OnUseOrder). The original builds a one-element
    // CArray<CUnit*> on the heap for it.
    int SendUseOrder(CUnit* unit, CEngineObject* target, int kind, int value, int flag);
    // 0066AB80 (RET 0xC). 0x36 for `units` on a dead `target`: `flag`, kind 0x18A95, value -1.
    int SendUseDead(const mfc::CArray<CUnit*>* units, CEngineObject* target, int flag);
    // 0066B080. 0x50: the player's party saved without its variables (CParty::SaveParty, which
    // draws an ISAAC key) as a sized block. Returns 1.
    int SendPartyUpdate();
    // The zone requests. When the client is between zones (state 3) they first return the local
    // party (GameFlow::ReturnLocalParty), send it (SendPartyUpdate) and the joined message 0x05
    // (state 2). The result is what the original leaves in EAX: the stream position after the
    // last write (004A53F0's result).
    // 0066B140 (RET 4). 0x03: take exit `exit`.
    int TakeExit(int exit);
    // 0066B1B0 (RET 8). 0x06: go to `zone` (the text with its zero) at `entrance`.
    int RequestZone(const char* zone, int entrance);
    // 0066B2C0. Only the state 3 part (0x05 joined); nothing otherwise.
    void Rejoin();
    // 0066B280 (RET 4). 0x04 and `value`; state 3. The original returns what the stream's Write
    // leaves in EAX (its one caller, 00578B00, passes it on unused); ours returns the position.
    int SendLeave04(int value);
    // 0066B300. 0x09 (resume).
    int SendResume();

    // player_client.cpp (A-H8).
    // 00667680 (slot 6). State 0: once the link is connected, the party (message 8, with the
    // variables, when +0x85 is set) and the login (message 0: version 100, "Tango", "Very top
    // secret", the player's +0xFC) are queued, state 1, and everything is sent (SendToServer's
    // result); without a connected link 1. State 1: 1. State 2: the camera's anchor x, y as
    // message 0x40, then sent. Other states: sent.
    int Pump();
    // 00667810. Compresses the queued requests (coder +0x88) and sends them on the link (slot 8);
    // the queue is emptied unless there is no link. 1 without a link, 0 when it is not connected,
    // else the send's result.
    int SendToServer();
    // 00667970. Runs every message of the received stream (+0x34, from its start) through the
    // handlers by type. 1 at the end of the stream; a read past the end, a type without a handler
    // or a refusing handler ends in the fatal error 00664B70 (its result returned).
    int DispatchMessages();
    // 00667B40 (RET 4; A-H25; the client world's frame 00567AB0 with the local server's stream).
    // Appends the bytes of `source` (rewound) to the received stream, whose pending bits are
    // stored first; its read position stays.
    void AppendReceived(BitStream* source);
    // 00667A30 (slot 23). One packet from the link, decompressed (+0x28B0) into +0x34 and
    // dispatched: 1 when there was none or it ran through, 0 without a link, when the receive
    // failed or the dispatch returned 0.
    int ReceiveMessages();

    // Message handlers (player_client.cpp; each returns 1).
    // 00667D80 (RET 4). A text from the received stream into a CString constructed at `out`;
    // returns `out`.
    void* ReadText(void* out);
    // 00669850 (0x81). A variable (name, value bits) set in the party's table (read first, the
    // value dropped); a quest variable ("q.", longer than 2) goes to the quest log and the client
    // world too.
    int OnSetVariable();
    // 00669960 (0x82). A variable (name) removed; a quest variable is announced with value 0.
    int OnDeleteVariable();
    // 00669EB0 (0x08). The game flow (007AFE20) changes zone (name, entrance).
    int OnZoneChange();
    // 00669F40 (0x0D). The music system (*007AF760) plays a jingle (name).
    int OnJingle();
    // 00669010 (0x02). The zone: the player cleared (00665200), then the client world loads the
    // block read from the stream (00567AB0, Rebirth-patched). 0 when the block cannot be read.
    int OnZone();
    // 006690F0 / 006691C0 (0x03 / 0x05). A block for the client world's 00567A60 / 00567950.
    // 0 when it cannot be read.
    int OnWorldBlock3();
    int OnWorldBlock5();
    // 00669290 (0x0A). The player drops an item (id); the quest log is refreshed with an empty
    // list.
    int OnItemRemoved();
    // 00669350 (0x17). The player's 00665550, then the quest log refreshed with an empty list.
    int OnQuestItemsChanged();
    // 0066A480 (0x15) / 0066A5A0 (0x18). Experience shown over each of the player's units / over
    // one unit (id): the amount (a float) times 0073E128 ^ (the unit's +0x33E - 25), rounded
    // through a float to the nearest integer, kind 8 (the client world's 0056CE40).
    int OnExperienceAll();
    int OnExperience();
    // A-H10.
    // 00669490 (0x06). The party's money (read into it) and items by id (until -1; found in the
    // client world's item manager, gathered in reverse) with a flag byte: the player takes them
    // (00665480), the quest log shows them with the money gained. 0 when the stream runs out.
    int OnItemsReceived();
    // 00669A30 (0x80). The party: a sized block loaded into the player's party (with variables;
    // a multiplayer client keeps its own); the player's recharge list (+0x114) gets every item
    // whose charge is below its maximum; the client world applies the variables (005652E0), "party
    // received on client" is logged and the game flow's ReturnLocalParty runs.
    int OnParty();
    // 00669C90 (0x83). A sized block loaded into the party without its variables.
    int OnPartyUpdate();
    // 00669FA0 (0x0E). A path shown as markers: kind (0..3), a flag, x, y, a unit id and a value.
    // Except for kind 3 a scratch unit (class 0x32) reads the path through a delta field and,
    // with markers on (0079BE70), a particle source 0x2039 (colour by kind, +0x11C the value)
    // runs one tick on it; with markers on, a source 0x203A (0x203B with the flag) at (x, y, 0)
    // and the first one go to the unit's client controller (00535240). Neither source is
    // released here.
    int OnPathMarker();
    // 00667EC0 (0x40; multiplayer). A map object the server announces: header (id, class,
    // prototype index, flags), scale, second name, model, two texture names, part names, script
    // name, prototype value. The world's object manager makes it; units get their model's height
    // and action speeds; peasant heads get their prototype's hair ("hr.%.2d") or lose it; the figure
    // is built (0 when that fails), a few models lose or gain parts or a particle source; the
    // textures are swapped (units of class 0x32: "mo%dp" / "mo%ds" by id); the player keeps it
    // (counted); its state is read (0 when that fails); it is filed in the client world and its
    // visibility level is the party variable its second name names.
    int OnNewObject();
    // 00667E60 (0x13). A text run as the interface's string event (005F8070).
    int OnStringEvent();
    // 00668C50 (0x60, A-H17). The party state (CPlayerServer::SendPartyState): the old party units
    // (+0x50D8) lose unit flag 0x400000 and their ids go into the set 007C8370; the world's pause
    // flag (byte); a dword (dropped); the new party ids (+0x50D8 resized without keeping its
    // contents), whose units get the flag and, when new to the party, in a session of kind 1
    // without an owner (+0x24C) and with 007AFCA4 >= 1, their controller's slot 43 (fade in); the
    // set emptied; the danger state to the world's +0x6E4; the other players' unit ids (+0x50E8).
    int OnPartyState();
    // 00667230 (slot 2). Clear's state (the +0x50D8 array through SetSize(0)), then the handler
    // table: 256 entries (grown as an aligned array), zeroed, and the 40 handlers by message type
    // (their game addresses, so the rewritten ones run through their hooks). Returns 1.
    BOOL CreateClient();
    // 00667510 (slot 3) and its copy 00667600 (slot 15). State 0, the array +0x50D8 freed, both
    // streams emptied (Resize(0)), +0x84 and the send-party flag cleared, both coders reset.
    void Clear();
    // 00669710 (RET 4) / 00669750. With a link: request 2 is queued and the link records into
    // `path` (NetLink::OpenRecording) / the link stops recording.
    void StartRecording(const char* path);
    void StopRecording();

    // A-H21 (player_client.cpp).
    // 00666D10. Constructor body: CEngineObject's fields, the connection's vtable (007431E0) and no
    // player, the members constructed (the streams with the queue's vtable 0073C960), then the
    // client's vtable and Clear inlined. Returns this.
    CPlayerClient* Construct();
    // 00666FD0. Destructor body: the client's vtable, Clear inlined, both id arrays freed, the
    // coders' outputs ended, the streams and the handler table freed, the connection's vtable and no
    // player, then CEngineObject's (00455930).
    void DestructBody();
    // 00666FB0 (slot 0, RET 4). DestructBody, then operator delete when bit 0 of `flags` is set.
    CPlayerClient* DeletingDestruct(unsigned flags);

    // A-H23 (player_client_handlers.cpp): the other message handlers (docs/notes/objects.md, "Client"
    // table). Each returns 1 unless noted.
    // 00667DA0 (0x00). Login accepted: a dword, state 2, the player manager (the player's owner_)
    // gives the player that id (CPlayerManager::ChangeId).
    int OnLoginAccepted();
    // 00667DE0 (0x01). The player disconnects (CPlayer::Disconnect).
    int OnDisconnect();
    // 00667C20 (0x04). A point and two dwords (heading, mode): the client world centres on the
    // point (005662B0); in zone "gz20g" (the player's +0xD8) the camera takes distance 23 and a
    // 30-degree pitch (SetDistancePitch), elsewhere view 6 (mode 3) or 7 (RestoreView); then
    // SetHeading. Returns the last Read's byte count.
    int OnCameraPlacement();
    // 00669D50 (0x07). The game state and the world tick: the player's +0x104 keeps the old state.
    // While +0x84 is set only an Enter key-down (-1, 0xD, 0) goes to the interface; otherwise the
    // state is stored, the interface makes its screen, the tick is set, state 3 refreshes the
    // client world's units (0056DE80), every unit of the player goes through slot 14 (the walk
    // stops at a null one) and a multiplayer client updates from the server (005800C0).
    int OnGameState();
    // 00669F30 (0x09). The interface's OnGameLost.
    int OnGameLost();
    // 00667DF0 (0x0B). Two dwords to the interface's OnUnitSpeech.
    int OnUnitSpeech();
    // 006696A0 (0x0C). (id, float) pairs until id -1: the object manager's object gets the float
    // at +0x28; an unknown id's float is not read (the next id is read from it). 0 when the stream
    // fails.
    int OnObjectValues();
    // 00667D10 (0x0F). Two points and a byte: the camera looks from the first at the second
    // (LookFrom) and goes free or not (SetFree).
    int OnFreeCamera();
    // 00667E20 (0x12). A byte and a dword: the dword to the player's world (00566C10, +0x7C0).
    int OnWorldValue();
    // 0066A420 (0x14). (id, float): the item of the party's item manager gets the float at +0x28
    // and the player's ItemUsed.
    int OnItemValue();
    // 00669400 (0x16). (unit id, index): the party member's weapon record `index` is removed
    // (signed bound check) and the client unit's controller shows no weapon.
    int OnWeaponRemoved();
    // 0066A620 (0x19). A kind byte and a unit id: 0 the client world controls the unit, 1 / 2 set
    // / clear its flag 0x100 and light (not with flag 0x10), 3 deselects; 0 for other kinds.
    int OnUnitControl();
    // 006687B0 (0x41). A known map object reads its delta; platforms (0x54, 0x5D) refresh. 0 when
    // unknown or the read fails.
    int OnObjectChanged();
    // 00668850 (0x42). A known map object goes: a unit of the player leaves it, the entry is
    // erased, the object flagged 1 and released, the client world removes the id. 0 when unknown.
    int OnObjectRemoved();
    // 0066A3B0 (0x43). The object manager's unit (class 0x50) with this id: its controller's Died.
    int OnUnitDied();
    // 00668940 / 00668A30 / 00668AA0 (0x50 / 0x51 / 0x52). A timed object: (id, type) made by the
    // world's timed object manager, read in full, filed (counted; the highest id kept) / read as a
    // delta / erased, flagged 1 and released. 0 when it cannot be made, read or found.
    int OnTimedObjectAdded();
    int OnTimedObjectChanged();
    int OnTimedObjectRemoved();
    // 00669760 (0x70). The weather (007AFCAC, "RainSnow"): its 004E3FC0(kind byte, 16-bit
    // strength as a float).
    int OnWeather();
    // 006697B0 (0x71). Two floats: the client world's time speed (second) and time (first), then
    // its lighting.
    int OnWorldTime();
    // 00669800 (0x72). A point and a dword to the client world's 00564190.
    int OnWorldPoint();
    // 0066A400 (0x73). The client world's 005683B0 reads the stream.
    int OnWorldStream();
    // 00667BA0 (slot 14, RET 4). A unit joined (+0x24C set) or left the player: the interface is
    // told; a multiplayer client whose party has one member, `unit`, puts its id (-1 when it left)
    // in the login record (+0x2C) and raises its version.
    void NotifyUnitChanged(CUnit* unit);

    // +0x20 state_: 0 connecting, 1 logged in, 2 sending the camera, 3 between zones.
    // +0x24 the message handlers by type (thiscall, 0: refused), built by 00667230 (256 of them).
    using Handler = int(__fastcall*)(CPlayerClient*, void*);
    lib::AlignedArray<Handler> handlers_;
    BitStream in_;                  // +0x34 the server's messages being run
    BitStream out_;                 // +0x5C requests for the server
    // +0x84 a local session (GameStartLocalSession sets it): a unit's network owner becomes the local
    // player whatever its id (UnitReadNetState), and the game-state message only presses Enter.
    std::uint8_t localSession_;
    // +0x85 the login sends the party first: the flow sets it once the local player took the
    // requested party, clears it for a new zone.
    std::uint8_t sendParty_;
    std::uint8_t unused_86[2];
    CoderState compressor_;         // +0x88 for the requests sent
    CoderState decompressor_;       // +0x28B0 for the messages received
    // +0x50D8 the ids of the party's units (message 0x60; Clear frees it; the HUD's map and the client world's NearestVisibleUnit read them)
    lib::AlignedArray<int> partyUnitIds_;
    // +0x50E8 the ids of the other players' units the map shows (message 0x60)
    lib::AlignedArray<int> otherUnitIds_;
};
static_assert(offsetof(CPlayerClient, partyUnitIds_) == 0x50D8);
static_assert(offsetof(CPlayerClient, otherUnitIds_) == 0x50E8);
static_assert(offsetof(CPlayerClient, handlers_) == 0x24);
static_assert(offsetof(CPlayerClient, in_) == 0x34);
static_assert(offsetof(CPlayerClient, out_) == 0x5C);
static_assert(offsetof(CPlayerClient, sendParty_) == 0x85);
static_assert(offsetof(CPlayerClient, decompressor_) == 0x28B0);
static_assert(offsetof(CPlayerClient, localSession_) == 0x84);
static_assert(sizeof(CPlayerClient) == 0x50F8);

// 00666EC0 / 00666F70 (RET 4) and 00666F90 / 00666FA0 (player_client.cpp, A-H21): CPlayerConnection's
// own destructor body and deleting destructor; CPlayerClient's class name and id.
void __fastcall ConnectionDestructBody(CPlayerConnection* self);
const char* __fastcall PlayerClientClassName(CPlayerClient*);
int __fastcall PlayerClientClassId(CPlayerClient*);
