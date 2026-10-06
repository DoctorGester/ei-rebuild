#pragma once

// The client side of a player's connection (CPlayerClient, vtable 00743188; 00666FD0-0066A880): it
// sends the client's requests (+0x5C) compressed to the server and runs the server's messages
// (+0x34) through its handler table (built by 00667230). The single-player game runs it next to
// CPlayerServer (player_messages.h) over the loopback link. Names are provisional; see
// docs/notes/objects.md.

#include "player_messages.h"

#include <cstddef>
#include <cstdint>

class CPlayerClient {
public:
    // player_client.cpp (A-H8).
    // 00667680 (slot 4). State 0: once the link is connected, the party (message 8, with the
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
    // 00667A30 (slot 21). One packet from the link, decompressed (+0x28B0) into +0x34 and
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
    // 00667E60 (0x13). A text run as the interface's string event (005F8070).
    int OnStringEvent();
    // 00669710 (RET 4) / 00669750. With a link: request 2 is queued and the link records into
    // `path` (NetLink::OpenRecording) / the link stops recording.
    void StartRecording(const char* path);
    void StopRecording();

    CPlayer* Owner() const { return reinterpret_cast<CPlayer*>(player_); }

    void* vtable_;
    std::uint8_t engineObject_[0x1C - 0x04];
    std::uint8_t* player_;          // +0x1C the world player
    int state_;                     // +0x20 0 connecting, 1 logged in, 2 sending the camera, ...
    // +0x24 the message handlers by type (thiscall, 0: refused), built by 00667230; +0x28 their count.
    int(__fastcall** handlers_)(CPlayerClient*, void*);
    int handlerCount_;              // +0x28
    std::uint8_t unknown_2C[0x34 - 0x2C];
    BitStream in_;                  // +0x34 the server's messages being run
    BitStream out_;                 // +0x5C requests for the server
    std::uint8_t unknown_84;
    std::uint8_t sendParty_;        // +0x85 the login sends the party first
    std::uint8_t unknown_86[2];
    CoderState compressor_;         // +0x88 for the requests sent
    CoderState decompressor_;       // +0x28B0 for the messages received
};
static_assert(offsetof(CPlayerClient, handlers_) == 0x24);
static_assert(offsetof(CPlayerClient, in_) == 0x34);
static_assert(offsetof(CPlayerClient, out_) == 0x5C);
static_assert(offsetof(CPlayerClient, sendParty_) == 0x85);
static_assert(offsetof(CPlayerClient, decompressor_) == 0x28B0);
