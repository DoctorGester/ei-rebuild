#pragma once

// The network message classes, types 1..17 (SessionBase::RegisterMessageTypes); see docs/notes/network.md.
// Each has a factory (Create: a new message with no references); Clone copies with the copy constructor,
// OnShutdown resets the message in place (its result is undefined), Write / Read the fields at the cursor.

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

// Type 5: the local player's login, kept by the client; the server makes a player record and a binding of it.
class LoginMessage final : public Message {
public:
    LoginMessage();
    LoginMessage(const LoginMessage& other);
    ~LoginMessage() override;

    int OnShutdown() override;
    LoginMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::string name_;
    std::string text_;
    std::uint8_t host_ = 0;         // asks to host (honoured with a local client)
    std::uint8_t unused_25[3];
    std::uint32_t state_ = 4;       // the player record's state
    std::int32_t id_ = -1;          // the player record's id
    RefPtrTo<BindingMessage> binding_{};   // the server's binding for this login (not sent)
};

// One string: the base of SayMessage, ZoneMessage and MapMessage, which differ only in their classes.
class TextMessage : public Message {
public:
    std::string text_;

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
    SayMessage(const SayMessage& other) : TextMessage(other) {}
    ~SayMessage() override {}

    int OnShutdown() override;
    SayMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();
};

// Type 7: a command for the server (ClientSession::SendCommand).
class CommandMessage final : public Message {
public:
    CommandMessage();                                // kind_ is not set
    CommandMessage(const CommandMessage& other);
    ~CommandMessage() override {}

    int OnShutdown() override;
    CommandMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::uint32_t kind_;
    std::string text_;
};

// Two counted pointers; only the first is sent. The base of BindingMessage and
// FileRequestMessage, which differ only in their classes and the names of the pointers.
class PairMessage : public Message {
public:
    RefPtr first_{};
    RefPtrTo<Connection> second_{};   // the connection on the server (not sent)

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
    FileRequestMessage(const FileRequestMessage& other) : PairMessage(other) {}
    ~FileRequestMessage() override {}

    int OnShutdown() override;
    FileRequestMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtr& FileRef() { return first_; }
    RefPtrTo<Connection>& ConnectionRef() { return second_; }
};

// Type 15: a trade offer, the party a player offers to a partner.
class PartyMessage final : public Message {
public:
    PartyMessage();
    PartyMessage(const PartyMessage& other);
    ~PartyMessage() override;

    int OnShutdown() override;
    PartyMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefVector members_;              // PartyMemberMessages
    std::uint32_t money_ = 0;        // the offered party's money (CParty::Money)
    std::uint8_t accepted_ = 0;      // the partner's offer is accepted
    std::uint8_t unused_1D[3];
    std::uint32_t acceptedRevision_ = 0;   // the partner's revision accepted
    std::uint32_t revision_ = 1;
    RefPtrTo<PlayerMessage> player_{};     // the offering player's record
    RefPtrTo<PlayerMessage> partner_{};    // the partner's record
    std::uint8_t trading_ = 0;       // in a trade (server side; not sent)
    std::uint8_t unused_31[3];
};

// Type 16: one member of an offered party, serialised into a memory stream.
class PartyMemberMessage final : public Message {
public:
    PartyMemberMessage() {}
    PartyMemberMessage(const PartyMemberMessage& other);
    ~PartyMemberMessage() override {}

    int OnShutdown() override;
    PartyMemberMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    CMemoryStream stream_;
};

// ---- Server to client ---------------------------------------------------------------------------

// Type 1: a player record.
class PlayerMessage final : public Message {
public:
    PlayerMessage();
    PlayerMessage(const PlayerMessage& other);
    ~PlayerMessage() override {}

    int OnShutdown() override;
    PlayerMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::string name_;
    std::string text_;
    std::uint32_t state_ = 1;       // from the login
    std::uint8_t host_ = 0;         // the hosting player
    std::uint8_t unused_29[3];
    std::int32_t id_ = -1;          // from the login
    std::int32_t serial_ = -1;      // from the server's player serial (not sent)
    std::int32_t number_ = -1;      // the lowest number free when joining (not sent)
    std::uint32_t firstLine_;       // the first chat line it receives (not sent)
};

// Type 2: a chat or system line.
class LineMessage final : public Message {
public:
    LineMessage();                               // number_ is not set
    LineMessage(const LineMessage& other);
    ~LineMessage() override;

    int OnShutdown() override;
    LineMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::uint32_t number_;
    std::string text_;
    RefPtr connection_{};           // the only receiver (null: everyone; not sent)
};

// Type 3: where the host's game is (built by AnnounceHost).
class HostMessage final : public Message {
public:
    HostMessage();
    HostMessage(const HostMessage& other);
    ~HostMessage() override {}

    int OnShutdown() override;
    HostMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::string address_;           // "host:port"
    std::string zone_;
};

// Type 4: binds a player record (first_) to its connection (second_, not sent).
class BindingMessage final : public PairMessage {
public:
    BindingMessage();
    BindingMessage(const BindingMessage& other) : PairMessage(other) {}
    ~BindingMessage() override {}

    int OnShutdown() override;
    BindingMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtr& PlayerRef() { return first_; }
    RefPtrTo<Connection>& ConnectionRef() { return second_; }
    PlayerMessage* Player() const { return static_cast<PlayerMessage*>(first_.ptr); }
};

// Type 8: a zone the host may pick next.
class ZoneMessage final : public TextMessage {
public:
    ZoneMessage();
    ZoneMessage(const ZoneMessage& other) : TextMessage(other) {}
    ~ZoneMessage() override {}

    int OnShutdown() override;
    ZoneMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();
};

// Type 9: the map being played.
class MapMessage final : public TextMessage {
public:
    MapMessage() {}
    MapMessage(const MapMessage& other) : TextMessage(other) {}
    ~MapMessage() override {}

    int OnShutdown() override;
    MapMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();
};

// Type 10: a script variable.
class VariableMessage final : public Message {
public:
    VariableMessage();                                 // value_ and serial_ are not set
    VariableMessage(const VariableMessage& other);
    ~VariableMessage() override {}

    int OnShutdown() override;
    VariableMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::string name_;
    float value_;
    std::uint32_t serial_;       // from g_variableSerial
};

// Type 11: a shared file.
class FileMessage final : public Message {
public:
    FileMessage();
    FileMessage(const FileMessage& other);
    ~FileMessage() override;

    int OnShutdown() override;
    FileMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    std::uint16_t chunkCount_;
    std::uint16_t unused_0E;
    std::int32_t size_;          // in bytes
    std::string name_;
    RefVector chunks_;           // the chunk messages (not sent)
};

// Type 12: 64 bytes of a shared file.
class FileChunkMessage final : public Message {
public:
    static constexpr std::size_t kDataSize = 0x40;

    FileChunkMessage() {}
    FileChunkMessage(const FileChunkMessage& other);
    ~FileChunkMessage() override;

    int OnShutdown() override;
    FileChunkMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtrTo<FileMessage> file_{};
    std::uint16_t index_;
    std::uint8_t data_[kDataSize];
    std::uint8_t unused_52[2];
};

// Type 14: an award for one connection (the server queues it with a delay; ClientSession::ApplyAwards).
class AwardMessage final : public Message {
public:
    AwardMessage();
    AwardMessage(const AwardMessage& other);
    ~AwardMessage() override;

    int OnShutdown() override;
    AwardMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtr connection_{};        // the receiver (not sent)
    std::uint32_t kind_;         // 0 experience, 1 gold, 2 item, 3 remove item, 4 quest reset
    float value_;
    std::string text_;           // the item's name
};

// Type 17: a trade between two offers.
class TradeMessage final : public Message {
public:
    TradeMessage() {}
    TradeMessage(const TradeMessage& other);
    ~TradeMessage() override;

    int OnShutdown() override;
    TradeMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* __fastcall Create();

    RefPtrTo<PlayerMessage> player_{};       // the first offer's player (not sent)
    RefPtrTo<PlayerMessage> partner_{};      // its partner (not sent)
    RefPtrTo<PartyMessage> offer_{};
    RefPtrTo<PartyMessage> partnerOffer_{};
    std::uint8_t done_ = 0;                  // both sides accepted
    std::uint8_t unused_1D[3];
    std::uint32_t revision_ = 0;             // the first offer's revision traded
    std::uint32_t partnerRevision_ = 0;      // the second's
};

// The serial the server's next variable or map record gets.
inline std::uint32_t g_variableSerial = /*0079B608*/ {};

} // namespace net
