#pragma once

// The network message classes (game.exe's vtables 0073BBF8 ... 0073BDD0, 0x18 apart; message types 1..17 as
// SessionBase::RegisterMessageTypes registers their factories). Names provisional; see
// docs/notes/cleanup-network.md and docs/notes/network.md.
//
// Every class has a factory (the type registry's, a static Create: a new message with no references), a
// constructor, a copy constructor (slot 3, Clone, makes the copy with it) and a destructor (net_messages.cpp).
// Slot 1 is Serialize (empty), slot 2 (OnShutdown) resets the message (destroyed and built again in place; EAX is
// left undefined by the original), slots 4 / 5 write and read the fields at the cursor's byte position.

#include "net_classes.h"
#include "core_refcount.h"
#include "core_streams.h"

#include "lib/crt.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace net {

class BindingMessage;
class FileMessage;
class PlayerMessage;

// An STLport vector of counted pointers (start, finish, end of storage), as the file and party
// messages embed it.
using RefVector = stlp::vector<RefPtr>;

// ---- Client to server ---------------------------------------------------------------------------

// 0073BBF8 (type 5, 0x34 bytes): the local player's login, kept by the client at +0x2600. The
// server's handlers make a player record and a binding from it (net_server_join.cpp).
class LoginMessage final : public Message {
public:
    LoginMessage();                              // 00413350
    LoginMessage(const LoginMessage& other);     // Clone's copy (00413440, inlined)
    ~LoginMessage() override;                    // 00413800 (deleting: 004137E0)

    int OnShutdown() override;                  // 2: 00413570
    LoginMessage* Clone() override;             // 3: 00413440
    void Write(BufferCursor* cursor) override;  // 4: 004136B0
    void Read(BufferCursor* cursor) override;   // 5: 00413750

    static Message* __fastcall Create();        // 004170A0

    stlp::string name_;             // +0x0C the player's name
    stlp::string text_;             // +0x18
    std::uint8_t host_ = 0;         // +0x24 asks to host (honoured with a local client)
    std::uint8_t unused_25[3];
    std::uint32_t state_ = 4;       // +0x28 the player record's state
    std::int32_t id_ = -1;          // +0x2C the player record's id
    RefPtrTo<BindingMessage> binding_{};   // +0x30 the server's binding for this login (not sent)
};

// One string at +0xC (0x18 bytes): the base of SayMessage, ZoneMessage and MapMessage, which
// differ only in their classes (game.exe has no table of its own for this layer).
class TextMessage : public Message {
public:
    stlp::string text_;             // +0x0C

protected:
    TextMessage();
    TextMessage(const TextMessage& other);
    ~TextMessage() override;
    void WriteText(BufferCursor* cursor);
    void ReadText(BufferCursor* cursor);
};

// 0073BC28 (type 6): a chat line from a client (the server's handler 0041B1C0 announces it with
// the player's number and name).
class SayMessage final : public TextMessage {
public:
    SayMessage();                                                      // 004138A0
    SayMessage(const SayMessage& other) : TextMessage(other) {}       // Clone's copy
    ~SayMessage() override {}                                          // 00413C40 (deleting: 00413C20)

    int OnShutdown() override;                  // 2: 00413A50
    SayMessage* Clone() override;               // 3: 00413930
    void Write(BufferCursor* cursor) override;  // 4: 00413B10
    void Read(BufferCursor* cursor) override;   // 5: 00413B50

    static Message* __fastcall Create();        // 004171A0
};

// 0073BC40 (type 7, 0x1C bytes): a command for the server (ClientSession::SendCommand).
class CommandMessage final : public Message {
public:
    CommandMessage();                                // 00413C90 (kind_ is not set)
    CommandMessage(const CommandMessage& other);     // Clone's copy
    ~CommandMessage() override {}                    // 00414060 (deleting: 00414040)

    int OnShutdown() override;                  // 2: 00413E40
    CommandMessage* Clone() override;           // 3: 00413D20
    void Write(BufferCursor* cursor) override;  // 4: 00413F00
    void Read(BufferCursor* cursor) override;   // 5: 00413F50

    static Message* __fastcall Create();        // 00417250

    std::uint32_t kind_;         // +0x0C
    stlp::string text_;          // +0x10
};

// Two counted pointers (0x14 bytes); only the first is sent. The base of BindingMessage and
// FileRequestMessage, which differ only in their classes and the names of the pointers.
class PairMessage : public Message {
public:
    RefPtr first_{};                  // +0x0C
    RefPtrTo<Connection> second_{};   // +0x10 the connection on the server (not sent)

protected:
    PairMessage() {}
    PairMessage(const PairMessage& other);
    ~PairMessage() override;
    void WriteFirst(BufferCursor* cursor);
    void ReadFirst(BufferCursor* cursor);
};

// 0073BC58 (type 13): a client's request for a shared file (first_ the file message, second_ the
// requesting connection on the server).
class FileRequestMessage final : public PairMessage {
public:
    FileRequestMessage();                                                          // 004140B0
    FileRequestMessage(const FileRequestMessage& other) : PairMessage(other) {}   // Clone's copy
    ~FileRequestMessage() override {}                                              // 00414270 (deleting: 00414250)

    int OnShutdown() override;                  // 2: 00414130
    FileRequestMessage* Clone() override;       // 3: 004140D0
    void Write(BufferCursor* cursor) override;  // 4: 004141F0
    void Read(BufferCursor* cursor) override;   // 5: 00414210

    static Message* __fastcall Create();        // 004179E0

    RefPtr& FileRef() { return first_; }
    RefPtrTo<Connection>& ConnectionRef() { return second_; }
};

// 0073BDD0 (type 15, 0x34 bytes): a trade offer (the party a player offers to a partner; its
// members are PartyMemberMessages). Kept by the client at +0x260C, by the server on +0x25B8.
class PartyMessage final : public Message {
public:
    PartyMessage();
    PartyMessage(const PartyMessage& other);     // Clone's copy
    ~PartyMessage() override;                    // 004178F0 (deleting: 004178D0)

    int OnShutdown() override;                  // 2: 004177A0
    PartyMessage* Clone() override;             // 3: 004176A0
    void Write(BufferCursor* cursor) override;  // 4: 00412AC0
    void Read(BufferCursor* cursor) override;   // 5: 00412B70

    static Message* __fastcall Create();        // 00417650

    RefVector members_;              // +0x0C the party members (PartyMemberMessage)
    std::uint32_t money_ = 0;        // +0x18 the offered party's money (CParty::Money)
    std::uint8_t accepted_ = 0;      // +0x1C the partner's offer is accepted
    std::uint8_t unused_1D[3];
    std::uint32_t acceptedRevision_ = 0;   // +0x20 the partner's revision accepted
    std::uint32_t revision_ = 1;           // +0x24
    RefPtrTo<PlayerMessage> player_{};     // +0x28 the offering player's record
    RefPtrTo<PlayerMessage> partner_{};    // +0x2C the partner's record
    std::uint8_t trading_ = 0;       // +0x30 in a trade (server side; not sent)
    std::uint8_t unused_31[3];
};

// 0073BD78 (type 16, 0x34 bytes): one member of an offered party, serialised into a memory stream.
class PartyMemberMessage final : public Message {
public:
    PartyMemberMessage() {}
    PartyMemberMessage(const PartyMemberMessage& other);   // Clone's copy
    ~PartyMemberMessage() override {}                      // 00417620 (deleting: 00417600)

    int OnShutdown() override;                  // 2: 004174F0
    PartyMemberMessage* Clone() override;       // 3: 00417420
    void Write(BufferCursor* cursor) override;  // 4: 00417560
    void Read(BufferCursor* cursor) override;   // 5: 004175B0

    static Message* __fastcall Create();        // 00417300

    CMemoryStream stream_;       // +0x0C
};

// ---- Server to client ---------------------------------------------------------------------------

// 0073BC70 (type 1, 0x3C bytes): a player record (the server's +0x2590, the client's +0x25D8).
class PlayerMessage final : public Message {
public:
    PlayerMessage();                               // 0041C680
    PlayerMessage(const PlayerMessage& other);     // Clone's copy
    ~PlayerMessage() override {}                   // 004148E0 (deleting: 004148C0)

    int OnShutdown() override;                  // 2: 00414650
    PlayerMessage* Clone() override;            // 3: 00414500
    void Write(BufferCursor* cursor) override;  // 4: 00414770
    void Read(BufferCursor* cursor) override;   // 5: 00414820

    static Message* __fastcall Create();        // 00414420

    stlp::string name_;             // +0x0C
    stlp::string text_;             // +0x18
    std::uint32_t state_ = 1;       // +0x24 (from the login)
    std::uint8_t host_ = 0;         // +0x28 the hosting player
    std::uint8_t unused_29[3];
    std::int32_t id_ = -1;          // +0x2C (from the login)
    std::int32_t serial_ = -1;      // +0x30 from the server's player serial (not sent)
    std::int32_t number_ = -1;      // +0x34 the lowest number free when joining (not sent)
    std::uint32_t firstLine_;       // +0x38 the first chat line it receives (not sent)
};

// 0073BC88 (type 2, 0x20 bytes): a chat or system line (the server's +0x2594, the client's +0x25DC).
class LineMessage final : public Message {
public:
    LineMessage();                               // 0041C740 (number_ is not set)
    LineMessage(const LineMessage& other);       // Clone's copy
    ~LineMessage() override;                     // 00414D10 (deleting: 00414CF0)

    int OnShutdown() override;                  // 2: 00414AC0
    LineMessage* Clone() override;              // 3: 00414A00
    void Write(BufferCursor* cursor) override;  // 4: 00414BB0
    void Read(BufferCursor* cursor) override;   // 5: 00414C00

    static Message* __fastcall Create();        // 00414960

    std::uint32_t number_;          // +0x0C
    stlp::string text_;             // +0x10
    RefPtr connection_{};           // +0x1C the only receiver (null: everyone; not sent)
};

// 0073BCA0 (type 3, 0x24 bytes): where the host's game is (the server's +0x25A0, the client's
// +0x25EC; built by world_game.cpp's AnnounceHost).
class HostMessage final : public Message {
public:
    HostMessage();                               // 0041C7C0
    HostMessage(const HostMessage& other);       // Clone's copy
    ~HostMessage() override {}                   // 004152A0 (deleting: 00415280)

    int OnShutdown() override;                  // 2: 00414FA0
    HostMessage* Clone() override;              // 3: 00414E80
    void Write(BufferCursor* cursor) override;  // 4: 004150B0
    void Read(BufferCursor* cursor) override;   // 5: 00415120

    static Message* __fastcall Create();        // 00414DB0

    stlp::string address_;          // +0x0C "host:port"
    stlp::string zone_;             // +0x18
};

// 0073BCB8 (type 4): binds a player record (first_) to its connection (second_, not sent). The
// server's +0x25A4; a client keeps its own at +0x25F0.
class BindingMessage final : public PairMessage {
public:
    BindingMessage();                                                      // 0041C870
    BindingMessage(const BindingMessage& other) : PairMessage(other) {}   // Clone's copy
    ~BindingMessage() override {}                                          // 004154D0 (deleting: 004154B0)

    int OnShutdown() override;                  // 2: 004153B0
    BindingMessage* Clone() override;           // 3: 00415350
    void Write(BufferCursor* cursor) override;  // 4: 00415450
    void Read(BufferCursor* cursor) override;   // 5: 00415470

    static Message* __fastcall Create();        // 00415320

    RefPtr& PlayerRef() { return first_; }
    RefPtrTo<Connection>& ConnectionRef() { return second_; }
    // The bound player record.
    PlayerMessage* Player() const { return static_cast<PlayerMessage*>(first_.ptr); }
};

// 0073BCD0 (type 8): a zone the host may pick next (the server's +0x25AC, the client's +0x25F8).
class ZoneMessage final : public TextMessage {
public:
    ZoneMessage();                                                       // 00418C30
    ZoneMessage(const ZoneMessage& other) : TextMessage(other) {}       // Clone's copy
    ~ZoneMessage() override {}                                           // 00415930 (deleting: 00415910)

    int OnShutdown() override;                  // 2: 00415740
    ZoneMessage* Clone() override;              // 3: 00415620
    void Write(BufferCursor* cursor) override;  // 4: 00415800
    void Read(BufferCursor* cursor) override;   // 5: 00415840

    static Message* __fastcall Create();        // 00415570
};

// 0073BD00 (type 9): the map being played (the server's +0x25A8, the client's +0x25F4).
class MapMessage final : public TextMessage {
public:
    MapMessage() {}
    MapMessage(const MapMessage& other) : TextMessage(other) {}   // Clone's copy
    ~MapMessage() override {}                                      // 00416110 (deleting: 004160F0)

    int OnShutdown() override;                  // 2: 00415F20
    MapMessage* Clone() override;               // 3: 00415E00
    void Write(BufferCursor* cursor) override;  // 4: 00415FE0
    void Read(BufferCursor* cursor) override;   // 5: 00416020

    static Message* __fastcall Create();        // 00415D50
};

// 0073BCE8 (type 10, 0x20 bytes): a script variable (the server's +0x25B0, the client's +0x25FC).
class VariableMessage final : public Message {
public:
    VariableMessage();                                 // value_ and serial_ are not set
    VariableMessage(const VariableMessage& other);     // Clone's copy
    ~VariableMessage() override {}                     // 00415D00 (deleting: 00415CE0)

    int OnShutdown() override;                  // 2: 00415B60
    VariableMessage* Clone() override;          // 3: 00415A30
    void Write(BufferCursor* cursor) override;  // 4: 00415C20
    void Read(BufferCursor* cursor) override;   // 5: 00415C80

    static Message* __fastcall Create();        // 00415980

    stlp::string name_;          // +0x0C
    float value_;                // +0x18
    std::uint32_t serial_;       // +0x1C from g_variableSerial
};

// 0073BD18 (type 11, 0x2C bytes): a shared file (the server's +0x2598, the client's +0x25E0).
class FileMessage final : public Message {
public:
    FileMessage();
    FileMessage(const FileMessage& other);       // Clone's copy
    ~FileMessage() override;                     // 00416640 (deleting: 00416620)

    int OnShutdown() override;                  // 2: 00416390
    FileMessage* Clone() override;              // 3: 00416220
    void Write(BufferCursor* cursor) override;  // 4: 004164D0
    void Read(BufferCursor* cursor) override;   // 5: 00416530

    static Message* __fastcall Create();        // 00416160

    std::uint16_t chunkCount_;   // +0x0C
    std::uint16_t unused_0E;
    std::int32_t size_;          // +0x10 bytes
    stlp::string name_;          // +0x14
    RefVector chunks_;           // +0x20 the chunk messages (not sent)
};

// 0073BD30 (type 12, 0x54 bytes): 64 bytes of a shared file (the client's +0x25E4).
class FileChunkMessage final : public Message {
public:
    static constexpr std::size_t kDataSize = 0x40;

    FileChunkMessage() {}
    FileChunkMessage(const FileChunkMessage& other);   // Clone's copy
    ~FileChunkMessage() override;                      // 004168E0 (deleting: 004168C0)

    int OnShutdown() override;                  // 2: 00416780
    FileChunkMessage* Clone() override;         // 3: 00416720
    void Write(BufferCursor* cursor) override;  // 4: 00416800
    void Read(BufferCursor* cursor) override;   // 5: 00416850

    static Message* __fastcall Create();        // 004166F0

    RefPtrTo<FileMessage> file_{};       // +0x0C the file message
    std::uint16_t index_;                // +0x10
    std::uint8_t data_[kDataSize];       // +0x12
    std::uint8_t unused_52[2];
};

// 0073BD48 (type 14, 0x24 bytes): an award for one connection (the server's +0x25B4 with a delay,
// the client's +0x2608; ClientSession::ApplyAwards).
class AwardMessage final : public Message {
public:
    AwardMessage();
    AwardMessage(const AwardMessage& other);     // Clone's copy
    ~AwardMessage() override;                    // 00416D70 (deleting: 00416D50)

    int OnShutdown() override;                  // 2: 00416B30
    AwardMessage* Clone() override;             // 3: 00416A10
    void Write(BufferCursor* cursor) override;  // 4: 00416C00
    void Read(BufferCursor* cursor) override;   // 5: 00416C60

    static Message* __fastcall Create();        // 00416940

    RefPtr connection_{};        // +0x0C the receiver (not sent)
    std::uint32_t kind_;         // +0x10 0 experience, 1 gold, 2 item, 3 remove item, 4 quest reset
    float value_;                // +0x14
    stlp::string text_;          // +0x18 the item's name
};

// 0073BD60 (type 17, 0x28 bytes): a trade between two offers (the server's +0x25BC, the client's
// +0x2618).
class TradeMessage final : public Message {
public:
    TradeMessage() {}
    TradeMessage(const TradeMessage& other);     // Clone's copy
    ~TradeMessage() override;                    // 00416FC0 (deleting: 00416FA0)

    int OnShutdown() override;                  // 2: 00416EB0
    TradeMessage* Clone() override;             // 3: 00416E30
    void Write(BufferCursor* cursor) override;  // 4: 00412C80
    void Read(BufferCursor* cursor) override;   // 5: 00412CE0

    static Message* __fastcall Create();        // 00416DF0

    RefPtrTo<PlayerMessage> player_{};       // +0x0C the first offer's player (not sent)
    RefPtrTo<PlayerMessage> partner_{};      // +0x10 its partner (not sent)
    RefPtrTo<PartyMessage> offer_{};         // +0x14
    RefPtrTo<PartyMessage> partnerOffer_{};  // +0x18
    std::uint8_t done_ = 0;                  // +0x1C both sides accepted
    std::uint8_t unused_1D[3];
    std::uint32_t revision_ = 0;             // +0x20 the first offer's revision traded
    std::uint32_t partnerRevision_ = 0;      // +0x24 the second's
};

// The serial the next variable or map record of the server gets (net_server_vars.cpp, net_server_map.cpp).
inline std::uint32_t g_variableSerial = /*0079B608*/ {};

} // namespace net
