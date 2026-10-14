#pragma once

// The network message classes (vtables 0073BBF8 ... 0073BDD0, 0x18 apart; message types 1..17 as
// SessionBase::RegisterMessageTypes registers their factories). Names provisional; see
// docs/notes/cleanup-network.md and docs/notes/network.md.
//
// Every class has, besides its slots, a factory (the type registry's, a static Create), a
// constructor body (Construct; only for the classes whose constructor is a function of its own,
// net_messages.cpp) and a destructor body (Destruct); the scalar deleting destructors (slot 0) are
// in deleting_destructors_400.cpp. Slot 1 is Serialize (empty), slot 2 (OnShutdown) resets the
// message (destroyed and built again in place; EAX is left undefined by the original), slot 3
// clones it, slots 4 / 5 write and read the fields at the cursor's byte position.
//
// Like the sessions, the classes are __declspec(novtable) and never built by C++: the game's code
// or our rewrites of its bodies store the game's vtables, through which the virtual calls dispatch.
// The slot bodies are registered through `...Hook` shims (docs/rewrite-guide.md rule 4).

#include "net_classes.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace net {

// An STLport vector of counted pointers (start, finish, end of storage), as the file and party
// messages embed it.
using RefVector = stlp::vector<RefPtr>;
static_assert(sizeof(RefVector) == 12);

// ---- Client to server ---------------------------------------------------------------------------

// 0073BBF8 (type 5, 0x34 bytes): the local player's login, kept by the client at +0x2600. The
// server's handlers make a player record and a binding from it (net_server_join.cpp).
class __declspec(novtable) LoginMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BBF8;

    int OnShutdown() override;                  // 2: 00413570
    LoginMessage* Clone() override;             // 3: 00413440
    void Write(BufferCursor* cursor) override;  // 4: 004136B0
    void Read(BufferCursor* cursor) override;   // 5: 00413750

    static Message* __fastcall Create();        // 004170A0
    LoginMessage* Construct();                  // 00413350
    void Destruct();                            // 00413800

    StringRep name_;             // +0x0C the player's name
    StringRep text_;             // +0x18
    std::uint8_t host_;          // +0x24 asks to host (honoured with a local client)
    std::uint8_t unknown_25[3];
    std::uint32_t state_;        // +0x28 4 when made; the player record's state
    std::int32_t id_;            // +0x2C -1 when made; the player record's id
    RefPtr binding_;             // +0x30 the server's binding for this login (not sent)
};
static_assert(offsetof(LoginMessage, name_) == 0x0C);
static_assert(offsetof(LoginMessage, host_) == 0x24);
static_assert(offsetof(LoginMessage, state_) == 0x28);
static_assert(offsetof(LoginMessage, binding_) == 0x30);
static_assert(sizeof(LoginMessage) == 0x34);

// One string at +0xC (0x18 bytes): the base of SayMessage, ZoneMessage and MapMessage, which
// differ only in their vtables. The helpers are the slot bodies with the class's vtable.
class __declspec(novtable) TextMessage : public Message {
public:
    StringRep text_;             // +0x0C

protected:
    static Message* CreateAs(std::uintptr_t vtable);
    TextMessage* CloneAs(std::uintptr_t vtable);
    void ResetAs(std::uintptr_t vtable);
    void WriteText(BufferCursor* cursor);
    void ReadText(BufferCursor* cursor);
    void DestructAs(std::uintptr_t vtable);
};
static_assert(offsetof(TextMessage, text_) == 0x0C);
static_assert(sizeof(TextMessage) == 0x18);

// 0073BC28 (type 6): a chat line from a client (the server's handler 0041B1C0 announces it with
// the player's number and name).
class __declspec(novtable) SayMessage : public TextMessage {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BC28;

    int OnShutdown() override;                  // 2: 00413A50
    SayMessage* Clone() override;               // 3: 00413930
    void Write(BufferCursor* cursor) override;  // 4: 00413B10
    void Read(BufferCursor* cursor) override;   // 5: 00413B50

    static Message* __fastcall Create();        // 004171A0
    SayMessage* Construct();                    // 004138A0
    void Destruct();                            // 00413C40
};
static_assert(sizeof(SayMessage) == 0x18);

// 0073BC40 (type 7, 0x1C bytes): a command for the server (ClientSession::SendCommand).
class __declspec(novtable) CommandMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BC40;

    int OnShutdown() override;                  // 2: 00413E40
    CommandMessage* Clone() override;           // 3: 00413D20
    void Write(BufferCursor* cursor) override;  // 4: 00413F00
    void Read(BufferCursor* cursor) override;   // 5: 00413F50

    static Message* __fastcall Create();        // 00417250
    CommandMessage* Construct();                // 00413C90 (kind_ is not set)
    void Destruct();                            // 00414060

    std::uint32_t kind_;         // +0x0C
    StringRep text_;             // +0x10
};
static_assert(offsetof(CommandMessage, text_) == 0x10);
static_assert(sizeof(CommandMessage) == 0x1C);

// Two counted pointers (0x14 bytes); only the first is sent. The base of BindingMessage and
// FileRequestMessage, which differ only in their vtables and the names of the pointers.
class __declspec(novtable) PairMessage : public Message {
public:
    RefPtr first_;               // +0x0C
    RefPtr second_;              // +0x10 (not sent)

protected:
    static Message* CreateAs(std::uintptr_t vtable);
    PairMessage* CloneAs(std::uintptr_t vtable);
    void ResetAs(std::uintptr_t vtable);
    void WriteFirst(BufferCursor* cursor);
    void ReadFirst(BufferCursor* cursor);
    void DestructAs(std::uintptr_t vtable);
    void ConstructAs(std::uintptr_t vtable);
};
static_assert(offsetof(PairMessage, first_) == 0x0C);
static_assert(sizeof(PairMessage) == 0x14);

// 0073BC58 (type 13): a client's request for a shared file (first_ the file message, second_ the
// requesting connection on the server).
class __declspec(novtable) FileRequestMessage : public PairMessage {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BC58;

    int OnShutdown() override;                  // 2: 00414130
    FileRequestMessage* Clone() override;       // 3: 004140D0
    void Write(BufferCursor* cursor) override;  // 4: 004141F0
    void Read(BufferCursor* cursor) override;   // 5: 00414210

    static Message* __fastcall Create();        // 004179E0
    FileRequestMessage* Construct();            // 004140B0
    void Destruct();                            // 00414270

    RefPtr& FileRef() { return first_; }
    RefPtr& ConnectionRef() { return second_; }
};
static_assert(sizeof(FileRequestMessage) == 0x14);

// 0073BDD0 (type 15, 0x34 bytes): a trade offer (the party a player offers to a partner; its
// members are PartyMemberMessages). Kept by the client at +0x260C, by the server on +0x25B8.
class __declspec(novtable) PartyMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BDD0;

    int OnShutdown() override;                  // 2: 004177A0
    PartyMessage* Clone() override;             // 3: 004176A0
    void Write(BufferCursor* cursor) override;  // 4: 00412AC0
    void Read(BufferCursor* cursor) override;   // 5: 00412B70

    static Message* __fastcall Create();        // 00417650
    void Destruct();                            // 004178F0

    RefVector members_;              // +0x0C the party members (PartyMemberMessage)
    std::uint32_t money_;            // +0x18 the offered party's money (CParty::Money)
    std::uint8_t accepted_;          // +0x1C the partner's offer is accepted
    std::uint8_t unknown_1D[3];
    std::uint32_t acceptedRevision_; // +0x20 the partner's revision accepted
    std::uint32_t revision_;         // +0x24 1 when made
    RefPtr player_;                  // +0x28 the offering player's record
    RefPtr partner_;                 // +0x2C the partner's record
    std::uint8_t trading_;           // +0x30 in a trade (server side; not sent)
    std::uint8_t unknown_31[3];
};
static_assert(offsetof(PartyMessage, money_) == 0x18);
static_assert(offsetof(PartyMessage, accepted_) == 0x1C);
static_assert(offsetof(PartyMessage, revision_) == 0x24);
static_assert(offsetof(PartyMessage, player_) == 0x28);
static_assert(offsetof(PartyMessage, trading_) == 0x30);
static_assert(sizeof(PartyMessage) == 0x34);

// 0073BD78 (type 16, 0x34 bytes): one member of an offered party, serialised into a memory stream.
class __declspec(novtable) PartyMemberMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BD78;

    int OnShutdown() override;                  // 2: 004174F0
    PartyMemberMessage* Clone() override;       // 3: 00417420
    void Write(BufferCursor* cursor) override;  // 4: 00417560
    void Read(BufferCursor* cursor) override;   // 5: 004175B0

    static Message* __fastcall Create();        // 00417300
    void Destruct();                            // 00417620

    CMemoryStream stream_;       // +0x0C
};
static_assert(offsetof(PartyMemberMessage, stream_) == 0x0C);
static_assert(sizeof(PartyMemberMessage) == 0x34);

// ---- Server to client ---------------------------------------------------------------------------

// 0073BC70 (type 1, 0x3C bytes): a player record (the server's +0x2590, the client's +0x25D8).
class __declspec(novtable) PlayerMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BC70;

    int OnShutdown() override;                  // 2: 00414650
    PlayerMessage* Clone() override;            // 3: 00414500
    void Write(BufferCursor* cursor) override;  // 4: 00414770
    void Read(BufferCursor* cursor) override;   // 5: 00414820

    static Message* __fastcall Create();        // 00414420
    PlayerMessage* Construct();                 // 0041C680
    void Destruct();                            // 004148E0

    StringRep name_;             // +0x0C
    StringRep text_;             // +0x18
    std::uint32_t state_;        // +0x24 1 when made (from the login)
    std::uint8_t host_;          // +0x28 the hosting player
    std::uint8_t unknown_29[3];
    std::int32_t id_;            // +0x2C -1 when made (from the login)
    std::int32_t serial_;        // +0x30 from the counter at 0079B60C (not sent)
    std::int32_t number_;        // +0x34 the lowest number free when joining (not sent)
    std::uint32_t firstLine_;    // +0x38 the first chat line it receives (not sent)
};
static_assert(offsetof(PlayerMessage, state_) == 0x24);
static_assert(offsetof(PlayerMessage, host_) == 0x28);
static_assert(offsetof(PlayerMessage, id_) == 0x2C);
static_assert(offsetof(PlayerMessage, serial_) == 0x30);
static_assert(offsetof(PlayerMessage, firstLine_) == 0x38);
static_assert(sizeof(PlayerMessage) == 0x3C);

// 0073BC88 (type 2, 0x20 bytes): a chat or system line (the server's +0x2594, the client's +0x25DC).
class __declspec(novtable) LineMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BC88;

    int OnShutdown() override;                  // 2: 00414AC0
    LineMessage* Clone() override;              // 3: 00414A00
    void Write(BufferCursor* cursor) override;  // 4: 00414BB0
    void Read(BufferCursor* cursor) override;   // 5: 00414C00

    static Message* __fastcall Create();        // 00414960
    LineMessage* Construct();                   // 0041C740 (number_ is not set)
    void Destruct();                            // 00414D10

    std::uint32_t number_;       // +0x0C
    StringRep text_;             // +0x10
    RefPtr connection_;          // +0x1C the only receiver (null: everyone; not sent)
};
static_assert(offsetof(LineMessage, text_) == 0x10);
static_assert(offsetof(LineMessage, connection_) == 0x1C);
static_assert(sizeof(LineMessage) == 0x20);

// 0073BCA0 (type 3, 0x24 bytes): where the host's game is (the server's +0x25A0, the client's
// +0x25EC; built by world_game.cpp's AnnounceHost).
class __declspec(novtable) HostMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BCA0;

    int OnShutdown() override;                  // 2: 00414FA0
    HostMessage* Clone() override;              // 3: 00414E80
    void Write(BufferCursor* cursor) override;  // 4: 004150B0
    void Read(BufferCursor* cursor) override;   // 5: 00415120

    static Message* __fastcall Create();        // 00414DB0
    HostMessage* Construct();                   // 0041C7C0
    void Destruct();                            // 004152A0

    StringRep address_;          // +0x0C "host:port"
    StringRep zone_;             // +0x18
};
static_assert(offsetof(HostMessage, zone_) == 0x18);
static_assert(sizeof(HostMessage) == 0x24);

// 0073BCB8 (type 4): binds a player record (first_) to its connection (second_, not sent). The
// server's +0x25A4; a client keeps its own at +0x25F0.
class __declspec(novtable) BindingMessage : public PairMessage {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BCB8;

    int OnShutdown() override;                  // 2: 004153B0
    BindingMessage* Clone() override;           // 3: 00415350
    void Write(BufferCursor* cursor) override;  // 4: 00415450
    void Read(BufferCursor* cursor) override;   // 5: 00415470

    static Message* __fastcall Create();        // 00415320
    BindingMessage* Construct();                // 0041C870
    void Destruct();                            // 004154D0

    RefPtr& PlayerRef() { return first_; }
    RefPtr& ConnectionRef() { return second_; }
};
static_assert(sizeof(BindingMessage) == 0x14);

// 0073BCD0 (type 8): a zone the host may pick next (the server's +0x25AC, the client's +0x25F8).
class __declspec(novtable) ZoneMessage : public TextMessage {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BCD0;

    int OnShutdown() override;                  // 2: 00415740
    ZoneMessage* Clone() override;              // 3: 00415620
    void Write(BufferCursor* cursor) override;  // 4: 00415800
    void Read(BufferCursor* cursor) override;   // 5: 00415840

    static Message* __fastcall Create();        // 00415570
    ZoneMessage* Construct();                   // 00418C30
    void Destruct();                            // 00415930
};
static_assert(sizeof(ZoneMessage) == 0x18);

// 0073BD00 (type 9): the map being played (the server's +0x25A8, the client's +0x25F4).
class __declspec(novtable) MapMessage : public TextMessage {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BD00;

    int OnShutdown() override;                  // 2: 00415F20
    MapMessage* Clone() override;               // 3: 00415E00
    void Write(BufferCursor* cursor) override;  // 4: 00415FE0
    void Read(BufferCursor* cursor) override;   // 5: 00416020

    static Message* __fastcall Create();        // 00415D50
    void Destruct();                            // 00416110
};
static_assert(sizeof(MapMessage) == 0x18);

// 0073BCE8 (type 10, 0x20 bytes): a script variable (the server's +0x25B0, the client's +0x25FC).
class __declspec(novtable) VariableMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BCE8;

    int OnShutdown() override;                  // 2: 00415B60
    VariableMessage* Clone() override;          // 3: 00415A30
    void Write(BufferCursor* cursor) override;  // 4: 00415C20
    void Read(BufferCursor* cursor) override;   // 5: 00415C80

    static Message* __fastcall Create();        // 00415980 (value_ and serial_ are not set)
    void Destruct();                            // 00415D00

    StringRep name_;             // +0x0C
    float value_;                // +0x18
    std::uint32_t serial_;       // +0x1C from the counter at 0079B608
};
static_assert(offsetof(VariableMessage, value_) == 0x18);
static_assert(sizeof(VariableMessage) == 0x20);

// 0073BD18 (type 11, 0x2C bytes): a shared file (the server's +0x2598, the client's +0x25E0).
class __declspec(novtable) FileMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BD18;

    int OnShutdown() override;                  // 2: 00416390
    FileMessage* Clone() override;              // 3: 00416220
    void Write(BufferCursor* cursor) override;  // 4: 004164D0
    void Read(BufferCursor* cursor) override;   // 5: 00416530

    static Message* __fastcall Create();        // 00416160
    void Destruct();                            // 00416640

    std::uint16_t chunkCount_;   // +0x0C
    std::uint16_t unknown_0E;
    std::int32_t size_;          // +0x10 bytes
    StringRep name_;             // +0x14
    RefVector chunks_;           // +0x20 the chunk messages (not sent)
};
static_assert(offsetof(FileMessage, size_) == 0x10);
static_assert(offsetof(FileMessage, name_) == 0x14);
static_assert(offsetof(FileMessage, chunks_) == 0x20);
static_assert(sizeof(FileMessage) == 0x2C);

// 0073BD30 (type 12, 0x54 bytes): 64 bytes of a shared file (the client's +0x25E4).
class __declspec(novtable) FileChunkMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BD30;
    static constexpr std::size_t kDataSize = 0x40;

    int OnShutdown() override;                  // 2: 00416780
    FileChunkMessage* Clone() override;         // 3: 00416720
    void Write(BufferCursor* cursor) override;  // 4: 00416800
    void Read(BufferCursor* cursor) override;   // 5: 00416850

    static Message* __fastcall Create();        // 004166F0
    void Destruct();                            // 004168E0

    RefPtr file_;                        // +0x0C the file message
    std::uint16_t index_;                // +0x10
    std::uint8_t data_[kDataSize];       // +0x12
    std::uint8_t unknown_52[2];
};
static_assert(offsetof(FileChunkMessage, index_) == 0x10);
static_assert(offsetof(FileChunkMessage, data_) == 0x12);
static_assert(sizeof(FileChunkMessage) == 0x54);

// 0073BD48 (type 14, 0x24 bytes): an award for one connection (the server's +0x25B4 with a delay,
// the client's +0x2608; ClientSession::ApplyAwards).
class __declspec(novtable) AwardMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BD48;

    int OnShutdown() override;                  // 2: 00416B30
    AwardMessage* Clone() override;             // 3: 00416A10
    void Write(BufferCursor* cursor) override;  // 4: 00416C00
    void Read(BufferCursor* cursor) override;   // 5: 00416C60

    static Message* __fastcall Create();        // 00416940
    void Destruct();                            // 00416D70

    RefPtr connection_;          // +0x0C the receiver (not sent)
    std::uint32_t kind_;         // +0x10 0 experience, 1 gold, 2 item, 3 remove item, 4 quest reset
    float value_;                // +0x14
    StringRep text_;             // +0x18 the item's name
};
static_assert(offsetof(AwardMessage, kind_) == 0x10);
static_assert(offsetof(AwardMessage, text_) == 0x18);
static_assert(sizeof(AwardMessage) == 0x24);

// 0073BD60 (type 17, 0x28 bytes): a trade between two offers (the server's +0x25BC, the client's
// +0x2618).
class __declspec(novtable) TradeMessage : public Message {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BD60;

    int OnShutdown() override;                  // 2: 00416EB0
    TradeMessage* Clone() override;             // 3: 00416E30
    void Write(BufferCursor* cursor) override;  // 4: 00412C80
    void Read(BufferCursor* cursor) override;   // 5: 00412CE0

    static Message* __fastcall Create();        // 00416DF0
    void Destruct();                            // 00416FC0

    RefPtr player_;              // +0x0C the first offer's player (not sent)
    RefPtr partner_;             // +0x10 its partner (not sent)
    RefPtr offer_;               // +0x14 PartyMessage
    RefPtr partnerOffer_;        // +0x18 PartyMessage
    std::uint8_t done_;          // +0x1C both sides accepted
    std::uint8_t unknown_1D[3];
    std::uint32_t revision_;         // +0x20 the first offer's revision traded
    std::uint32_t partnerRevision_;  // +0x24 the second's
};
static_assert(offsetof(TradeMessage, offer_) == 0x14);
static_assert(offsetof(TradeMessage, done_) == 0x1C);
static_assert(offsetof(TradeMessage, partnerRevision_) == 0x24);
static_assert(sizeof(TradeMessage) == 0x28);

} // namespace net
