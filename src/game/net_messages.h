#pragma once

// The network message classes (message types 1..17 as SessionBase::RegisterMessageTypes registers their
// factories). Names provisional; see docs/notes/cleanup-network.md and docs/notes/network.md. Every class has
// a factory (the type registry's, a static Create: a new message with no references), a constructor, a copy
// constructor (slot 3, Clone, makes the copy with it) and a destructor (net_messages.cpp). Slot 1 is
// Serialize (empty), slot 2 (OnShutdown) resets the message (destroyed and built again in place; EAX is left
// undefined by the original), slots 4 / 5 write and read the fields at the cursor's byte position.

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

// Type 5, 0x34 bytes: the local player's login, kept by the client. The
// server's handlers make a player record and a binding from it (net_server_join.cpp).
class LoginMessage final : public Message {
public:
    LoginMessage();
    LoginMessage(const LoginMessage& other);     // Clone's copy
    ~LoginMessage() override;

    int OnShutdown() override;
    LoginMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    stlp::string name_;             // +0x0C the player's name
    stlp::string text_;             // +0x18
    std::uint8_t host_ = 0;         // +0x24 asks to host (honoured with a local client)
    std::uint8_t unused_25[3];
    std::uint32_t state_ = 4;       // +0x28 the player record's state
    std::int32_t id_ = -1;          // +0x2C the player record's id
    RefPtrTo<BindingMessage> binding_{};   // +0x30 the server's binding for this login (not sent)
};

// One string: the base of SayMessage, ZoneMessage and MapMessage, which
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

// Type 6: a chat line from a client (the server's handler announces it with the player's number and name).
class SayMessage final : public TextMessage {
public:
    SayMessage();
    SayMessage(const SayMessage& other) : TextMessage(other) {}       // Clone's copy
    ~SayMessage() override {}

    int OnShutdown() override;
    SayMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();
};

// Type 7, 0x1C bytes: a command for the server (ClientSession::SendCommand).
class CommandMessage final : public Message {
public:
    CommandMessage();                                // kind_ is not set
    CommandMessage(const CommandMessage& other);     // Clone's copy
    ~CommandMessage() override {}

    int OnShutdown() override;
    CommandMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::uint32_t kind_;         // +0x0C
    stlp::string text_;          // +0x10
};

// Two counted pointers; only the first is sent. The base of BindingMessage and
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

// Type 13: a client's request for a shared file (first_ the file message, second_ the
// requesting connection on the server).
class FileRequestMessage final : public PairMessage {
public:
    FileRequestMessage();
    FileRequestMessage(const FileRequestMessage& other) : PairMessage(other) {}   // Clone's copy
    ~FileRequestMessage() override {}

    int OnShutdown() override;
    FileRequestMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtr& FileRef() { return first_; }
    RefPtrTo<Connection>& ConnectionRef() { return second_; }
};

// Type 15, 0x34 bytes: a trade offer (the party a player offers to a partner; its
// members are PartyMemberMessages). Kept by the client, by the server on +0x25B8.
class PartyMessage final : public Message {
public:
    PartyMessage();
    PartyMessage(const PartyMessage& other);     // Clone's copy
    ~PartyMessage() override;

    int OnShutdown() override;
    PartyMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

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

// Type 16, 0x34 bytes: one member of an offered party, serialised into a memory stream.
class PartyMemberMessage final : public Message {
public:
    PartyMemberMessage() {}
    PartyMemberMessage(const PartyMemberMessage& other);   // Clone's copy
    ~PartyMemberMessage() override {}

    int OnShutdown() override;
    PartyMemberMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    CMemoryStream stream_;       // +0x0C
};

// ---- Server to client ---------------------------------------------------------------------------

// Type 1, 0x3C bytes: a player record (the server's +0x2590, the client's +0x25D8).
class PlayerMessage final : public Message {
public:
    PlayerMessage();
    PlayerMessage(const PlayerMessage& other);     // Clone's copy
    ~PlayerMessage() override {}

    int OnShutdown() override;
    PlayerMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

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

// Type 2, 0x20 bytes: a chat or system line (the server's +0x2594, the client's +0x25DC).
class LineMessage final : public Message {
public:
    LineMessage();                               // number_ is not set
    LineMessage(const LineMessage& other);       // Clone's copy
    ~LineMessage() override;

    int OnShutdown() override;
    LineMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::uint32_t number_;          // +0x0C
    stlp::string text_;             // +0x10
    RefPtr connection_{};           // +0x1C the only receiver (null: everyone; not sent)
};

// Type 3, 0x24 bytes: where the host's game is (the server's +0x25A0, the client's
// +0x25EC; built by world_game.cpp's AnnounceHost).
class HostMessage final : public Message {
public:
    HostMessage();
    HostMessage(const HostMessage& other);       // Clone's copy
    ~HostMessage() override {}

    int OnShutdown() override;
    HostMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    stlp::string address_;          // +0x0C "host:port"
    stlp::string zone_;             // +0x18
};

// Type 4: binds a player record (first_) to its connection (second_, not sent). The
// server's +0x25A4; a client keeps its own.
class BindingMessage final : public PairMessage {
public:
    BindingMessage();
    BindingMessage(const BindingMessage& other) : PairMessage(other) {}   // Clone's copy
    ~BindingMessage() override {}

    int OnShutdown() override;
    BindingMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtr& PlayerRef() { return first_; }
    RefPtrTo<Connection>& ConnectionRef() { return second_; }
    // The bound player record.
    PlayerMessage* Player() const { return static_cast<PlayerMessage*>(first_.ptr); }
};

// Type 8: a zone the host may pick next (the server's +0x25AC, the client's +0x25F8).
class ZoneMessage final : public TextMessage {
public:
    ZoneMessage();
    ZoneMessage(const ZoneMessage& other) : TextMessage(other) {}       // Clone's copy
    ~ZoneMessage() override {}

    int OnShutdown() override;
    ZoneMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();
};

// Type 9: the map being played (the server's +0x25A8, the client's +0x25F4).
class MapMessage final : public TextMessage {
public:
    MapMessage() {}
    MapMessage(const MapMessage& other) : TextMessage(other) {}   // Clone's copy
    ~MapMessage() override {}

    int OnShutdown() override;
    MapMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();
};

// Type 10, 0x20 bytes: a script variable (the server's +0x25B0, the client's +0x25FC).
class VariableMessage final : public Message {
public:
    VariableMessage();                                 // value_ and serial_ are not set
    VariableMessage(const VariableMessage& other);     // Clone's copy
    ~VariableMessage() override {}

    int OnShutdown() override;
    VariableMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    stlp::string name_;          // +0x0C
    float value_;                // +0x18
    std::uint32_t serial_;       // +0x1C from g_variableSerial
};

// Type 11, 0x2C bytes: a shared file (the server's +0x2598, the client's +0x25E0).
class FileMessage final : public Message {
public:
    FileMessage();
    FileMessage(const FileMessage& other);       // Clone's copy
    ~FileMessage() override;

    int OnShutdown() override;
    FileMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::uint16_t chunkCount_;   // +0x0C
    std::uint16_t unused_0E;
    std::int32_t size_;          // +0x10 bytes
    stlp::string name_;          // +0x14
    RefVector chunks_;           // +0x20 the chunk messages (not sent)
};

// Type 12, 0x54 bytes: 64 bytes of a shared file (the client's +0x25E4).
class FileChunkMessage final : public Message {
public:
    static constexpr std::size_t kDataSize = 0x40;

    FileChunkMessage() {}
    FileChunkMessage(const FileChunkMessage& other);   // Clone's copy
    ~FileChunkMessage() override;

    int OnShutdown() override;
    FileChunkMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtrTo<FileMessage> file_{};       // +0x0C the file message
    std::uint16_t index_;                // +0x10
    std::uint8_t data_[kDataSize];       // +0x12
    std::uint8_t unused_52[2];
};

// Type 14, 0x24 bytes: an award for one connection (the server's +0x25B4 with a delay,
// the client's +0x2608; ClientSession::ApplyAwards).
class AwardMessage final : public Message {
public:
    AwardMessage();
    AwardMessage(const AwardMessage& other);     // Clone's copy
    ~AwardMessage() override;

    int OnShutdown() override;
    AwardMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtr connection_{};        // +0x0C the receiver (not sent)
    std::uint32_t kind_;         // +0x10 0 experience, 1 gold, 2 item, 3 remove item, 4 quest reset
    float value_;                // +0x14
    stlp::string text_;          // +0x18 the item's name
};

// Type 17, 0x28 bytes: a trade between two offers (the server's +0x25BC, the client's
// +0x2618).
class TradeMessage final : public Message {
public:
    TradeMessage() {}
    TradeMessage(const TradeMessage& other);     // Clone's copy
    ~TradeMessage() override;

    int OnShutdown() override;
    TradeMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

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
