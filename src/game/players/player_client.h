#pragma once

// The client side of a player's connection: it sends the client's requests compressed to the server and runs
// the server's messages through their handlers (DispatchMessages). The single-player game runs it next to PlayerServer over
// the loopback link. See docs/notes/objects.md.

#include "game/players/player_messages.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class PlayerClient final : public PlayerConnection {
public:
    PlayerClient();
    ~PlayerClient() override;

    void Create() override { ResetState(); }
    void Destroy() override { ResetState(); }
    // Connecting: once the link is connected, the party (message 8, with the variables, when sendParty is set)
    // and the login (message 0: version 100, "Tango", "Very top secret", the player's partyId) are queued,
    // LoginSent, and everything is sent (SendToServer's result); without a connected link 1. LoginSent: 1.
    // SendingCamera: the camera's anchor x, y as message 0x40, then sent. Other states: sent.
    bool RunTick() override;
    const char* ClassName() override;
    ClassId GetClassId() override;
    void ForgetSentObjects() override {}
    // A unit joined (`player` set) or left the player: the interface is told; on a multiplayer client, when `unit`
    // is the party's hero (its first record), its id (-1 when it left) goes in the login record and the record's
    // version rises.
    void UnitChanged(Unit* unit) override;
    // Connecting, partyUnitIds freed, both streams emptied, localSession and sendParty cleared, both coders
    // reset.
    void ResetState() override;
    // One packet from the link, decompressed into in and dispatched: 1 when there was none or it ran
    // through, 0 without a link, when the receive failed or the dispatch returned 0.
    int Receive() override;

    // The local view's orders, queued for the server (out), each a type byte and a payload.
    // Units go as their ids (-1 for a null entry).
    // 0x30 walk / 0x3A go to: the units, `run`, the point.
    void SendWalk(const std::vector<Unit*>& units, const Vector3& to, int run);
    void SendGoTo(const std::vector<Unit*>& units, const Vector3& to, int run);
    // 0x31 attack: the units, `ctrl`, `how`, the target's id.
    void SendAttack(const std::vector<Unit*>& units, EngineObject* target, int how, int ctrl);
    // 0x33 / 0x32 cast spell `slot` of `caster` on `target` / at `point`, with the low byte of `ctrl`.
    void SendCastOnUnit(int slot, Unit* caster, EngineObject* target, int ctrl);
    void SendCastAtPoint(int slot, Unit* caster, const Vector3& point, int ctrl);
    // The same with quick item `slot` (slot | 0x80000000); a potion (category 8) with a count above 0 loses
    // one at once.
    void SendItemOnUnit(int slot, Unit* user, EngineObject* target, int ctrl);
    void SendItemAtPoint(int slot, Unit* user, const Vector3& point, int ctrl);
    // 0x34 weapon slot / 0x35 movement mode: the unit's id and the value's low byte; the unit keeps the value
    // (pendingWeapon / moveMode2).
    void SetWeapon(Unit* unit, int slot);
    void SetMoveMode(Unit* unit, int mode);
    // Unit::SetFightAtWill(value), then 0x3B: the id and the value's low byte.
    void SetUnitFightAtWill(Unit* unit, int value);
    // 0x39: the unit's id and the target's (-1 for none).
    void SendFollow(Unit* unit, EngineObject* target);
    // 0x10: whether `value` (a byte) is set.
    void SendPause(std::uint8_t value);
    // 0x36 use order for one unit (count 1, its id or -1): `flag`, `kind`, `value`, the target's id
    // (PlayerServer::OnUseOrder).
    void SendUseOrder(Unit* unit, EngineObject* target, int kind, int value, int flag);
    // 0x36 for `units` on a dead `target`: `flag`, kind UseSource::SearchBody, value -1.
    void SendUseDead(const std::vector<Unit*>& units, EngineObject* target, int flag);
    // 0x50: the player's party saved without its variables (Party::SaveParty, which draws an ISAAC key) as
    // a sized block.
    void SendPartyUpdate();
    // Ours. 0x51: the saved hero `companion` (its party without variables, a sized block) joins the hero; `restore`
    // is the list asked for again at a zone's start, the only add the server takes (the list changes in the base).
    void SendAddCompanion(Party* companion, bool restore);
    // Ours: the saved companions (SavedParties::companions) asked for again once the hero stands in a zone whose
    // host takes companions; set by OnCompanions, done by RunTick.
    void RestoreCompanions();
    // The zone requests. When the client is between zones they first return the local party
    // (GameFlow::ReturnLocalParty), send it (SendPartyUpdate) and the joined message 0x05 (SendingCamera).
    // 0x03: take exit `exit`.
    void TakeExit(int exit);
    // 0x06: go to `zone` (the text with its zero) at `entrance`.
    void RequestZone(std::string_view zone, int entrance);
    // Only the BetweenZones part (0x05 joined); nothing otherwise.
    void Rejoin();
    // 0x04 and `value`; State::BetweenZones.
    void RequestGameState(int value);
    // 0x09 (resume).
    void SendResume();

    // Compresses the queued requests and sends them on the link; the queue is emptied unless there is no
    // link. 1 without a link, 0 when it is not connected, else the send's result.
    bool SendToServer();
    // Runs every message of in (from its start) through the handlers by type. 1 at the end of the stream or
    // once a handler made the session leave (LeaveAfterTick: the packet's other messages are dropped); a read past
    // the end, a type without a handler or a refusing handler: a warning and 0 (the session disconnects).
    int DispatchMessages();
    // The session leaves the server after this client world tick (WorldClient::RunTick disconnects the player;
    // the interface shows `reason` in its disconnect box). A handler whose data this game cannot take (a loader
    // refused it) calls it and returns true. The first reason is kept. Returns true.
    bool LeaveAfterTick(std::string reason);
    // The client world's frame, with the local server's stream. Appends the bytes of `source` (rewound) to
    // the received stream, whose pending bits are stored first; its read position stays.
    void AppendReceived(MemoryStream* source);

    // The message handlers; each returns true unless noted (false: refused, the dispatcher's error).
    // Message 0x81. A variable (name, value bits) set in the party's table (read first, the value dropped); a
    // quest variable ("q.", longer than 2) goes to the quest log and the client world too.
    bool OnSetVariable();
    // Message 0x82. A variable (name) removed; a quest variable is announced with value 0.
    bool OnDeleteVariable();
    // Message 0x08. The game flow changes zone (name, entrance).
    bool OnZoneChange();
    // Message 0x0D. The music system plays a jingle (name).
    bool OnJingle();
    // Message 0x02. The zone: the player cleared, then the client world loads the block read from the
    // stream (Rebirth-patched). False when the block cannot be read.
    bool OnZone();
    // Messages 0x03 / 0x05. A block for the client world's two readers; false when it cannot be read. A block
    // naming items or spells this game's databases lack (the reader refused it) makes the session leave.
    bool OnItemsAndSpells();
    bool OnNewItemsAndSpells();
    // Message 0x0A. The player drops an item (id); the quest log is refreshed with an empty list.
    bool OnItemRemoved();
    // Message 0x17. A player update, then the quest log refreshed with an empty list.
    bool OnQuestItemsChanged();
    // Messages 0x15 / 0x18. Experience shown over each of the player's units / over one unit (id): the
    // amount (a float) times 1.04 ^ (the unit's params.intelligence - 25), rounded through a float, kind 8.
    bool OnExperienceAll();
    bool OnExperience();
    // Message 0x06. The party's money (read into it) and items by id (until -1; found in the client world's
    // item manager, gathered in reverse) with a flag byte: the player takes them and the quest log shows
    // them with the money gained. False when the stream runs out.
    bool OnItemsReceived();
    // Message 0x80. The party: a sized block loaded into the player's party (with variables; a multiplayer
    // client keeps its own); the player's recharge list gets every item whose charge is below its maximum;
    // the client world applies the variables, "party received on client" is logged and the game flow's
    // ReturnLocalParty runs. A party naming items or spells this game's databases lack is emptied instead and
    // the session leaves (KeepParty).
    bool OnParty();
    // Message 0x83. A sized block loaded into the party without its variables; one this game cannot take as for
    // OnParty.
    bool OnPartyUpdate();
    // The party loaded from the server: true when its items and spells name records of this game's databases
    // (HasEveryRecord); else it is emptied (Party::Clear, variables kept) and the session leaves (LeaveAfterTick).
    bool KeepParty(Party* party);
    // Message 0x0E. A path shown as markers: kind (0..3), a flag, x, y, a unit id and a value. Except for
    // kind 3 a scratch unit (class 0x32) reads the path through a delta field and, with markers on, a
    // particle source 0x2039 (ShapeChange: its inputs the kind and the path's points up to the value) runs one tick on
    // it; with markers on, a
    // source 0x203A (0x203B with the flag) at (x, y, 0) and the first one go to the unit's client controller.
    // Neither source is released here.
    bool OnPathMarker();
    // Message 0x40 (multiplayer). A map object the server announces: header (id, class, prototype index,
    // flags), scale, second name, model, two texture names, part names, script name, prototype value. The
    // world's object manager makes it; units get their model's height and action speeds; peasant heads get
    // their prototype's hair ("hr.%.2d") or lose it; the figure is built (false when that fails), a few models
    // lose or gain parts or a particle source; the textures are swapped (units of class 0x32: "mo%dp" /
    // "mo%ds" by id); the player keeps it (counted); its state is read (false when that fails); it is filed in
    // the client world and its visibility level is the party variable its second name names.
    bool OnNewObject();
    // Message 0x13. A text run as the interface's string event.
    bool OnStringEvent();
    // Message 0x60. The party state (PlayerServer::SendPartyState): the old party units lose
    // MapObjectFlag::InParty and their ids go into a set; the world's pause flag (byte); a dword (dropped); the new
    // party ids (partyUnitIds resized without keeping its contents), whose units get the flag and, when new to the
    // party, in a session of kind 1 without an owner and with a game-flow condition, their controller's
    // fade-in; the set emptied; the world's partyDanger; the other players' unit ids.
    bool OnPartyState();

    // Message 0x00. Login accepted: a dword, SendingCamera, the player's owner gives the player that id
    // (PlayerManager::ChangeId).
    bool OnLoginAccepted();
    // Message 0x01. The player disconnects (Player::Disconnect).
    bool OnDisconnect();
    // Ours. Message 0x84: a dword, the companions the host takes (companionLimit).
    bool OnCompanions();
    // Message 0x11. The server's reason for dropping the player (a text): the session leaves with it
    // (LeaveAfterTick; the original had no handler, and the dispatcher's error ended the game).
    bool OnError();
    // Message 0x04. A point and two dwords (heading, mode): the client world centres on the point; in the
    // party's zone "gz20g" the camera takes distance 23 and a 30-degree pitch (SetDistancePitch), elsewhere
    // view 6 (mode 3) or 7 (RestoreView); then SetHeading. False when the mode could not be read.
    bool OnCameraPlacement();
    // Message 0x07. The game state and the world tick: the player's lastScreen keeps the old state. While
    // localSession is set only an Enter key-down goes to the interface; otherwise the state is stored, the
    // interface makes its screen, the tick is set, state 3 refreshes the client world's units, every unit
    // of the player goes through UnitChanged (the walk stops at a null one) and a multiplayer client
    // updates from the server.
    bool OnGameState();
    // Message 0x09. The interface's OnGameLost.
    bool OnGameLost();
    // Message 0x0B. Two dwords to the interface's OnUnitSpeech.
    bool OnUnitSpeech();
    // Message 0x0C. (id, float) pairs until id -1: the client world's item gets the float as its charge; an
    // unknown id's float is not read (the next id is read from it). False when the stream fails.
    bool OnObjectValues();
    // Message 0x0F. Two points and a byte: the camera looks from the first at the second (LookFrom) and
    // goes free or not (SetFree).
    bool OnFreeCamera();
    // Message 0x12. A byte and a dword: the dword to the player's world.
    bool OnWorldValue();
    // Message 0x14. (id, float): the party's item gets the float as its charge, and the player's ItemUsed.
    bool OnItemValue();
    // Message 0x16. (unit id, index): the party member's weapon record `index` is removed (signed bound
    // check) and the client unit's controller shows no weapon.
    bool OnWeaponRemoved();
    // Message 0x19. A kind byte and a unit id: 0 the client world controls the unit, 1 / 2 mark / unmark the base
    // person (WorldClient::MarkPerson; our briefing marks its people itself and sends none), 3 deselects; false
    // for other kinds.
    bool OnUnitControl();
    // Message 0x41. A known map object reads its delta; platforms (0x54, 0x5D) refresh. False when unknown or
    // the read fails.
    bool OnObjectChanged();
    // Message 0x42. A known map object goes: a unit of the player leaves it, the entry is erased, the object
    // flagged 1 and released, the client world removes the id. False when unknown.
    bool OnObjectRemoved();
    // Message 0x43. The object manager's unit (class 0x50) with this id: its controller's Died.
    bool OnUnitDied();
    // Messages 0x50 / 0x51 / 0x52. A timed object: (id, type) made by the world's timed object manager, read
    // in full, filed (counted; the highest id kept) / read as a delta / erased, flagged 1 and released. 0
    // when it cannot be made, read or found.
    bool OnTimedObjectAdded();
    bool OnTimedObjectChanged();
    bool OnTimedObjectRemoved();
    // Message 0x70. The weather ("RainSnow"): its setter (kind byte, 16-bit strength as a float).
    bool OnWeather();
    // Message 0x71. Two floats: the client world's time speed (second) and time (first), then its lighting.
    bool OnWorldTime();
    // Message 0x72. A point and a dword to the client world.
    bool OnWind();
    // Message 0x73. The client world reads the stream.
    bool OnCountdowns();

    // Where the client is in joining and in changing zones (the harness reads it).
    enum class State : int {
        Connecting = 0,
        LoginSent = 1,          // the login (and the party) is sent; the server's login answer is next
        SendingCamera = 2,      // the camera's anchor goes to the server on every Pump
        BetweenZones = 3,       // RequestGameState asked for a screen; Rejoin leaves it
    };
    State state = {};

    MemoryStream in;                   // the server's messages being run
    MemoryStream out;                  // requests for the server
    // A local session (GameStartLocalSession sets it): a unit's network owner becomes the local
    // player whatever its id (Unit::ReadNetState), and the game-state message only presses Enter.
    std::uint8_t localSession = 0;
    // The login sends the party first: the flow sets it once the local player took the requested party,
    // clears it for a new zone.
    std::uint8_t sendParty = 0;
    CoderState compressor;          // for the requests sent
    CoderState decompressor;        // for the messages received
    // The ids of the party's units (message 0x60; the HUD's map and the client world's NearestVisibleUnit
    // read them).
    std::vector<int> partyUnitIds;
    // The ids of the other players' units the map shows (message 0x60).
    std::vector<int> otherUnitIds;
    // Why the session leaves after this tick (LeaveAfterTick); empty while it stays.
    std::string leaveReason;
    // Ours: the companions this zone's host takes (ServerMessage::Companions), 0 none (ResetState).
    std::uint32_t companionLimit = 0;
    bool restoreCompanions = false;   // ours: RestoreCompanions still to do in this zone (ResetState clears it)
};

