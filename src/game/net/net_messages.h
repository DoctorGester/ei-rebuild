#pragma once

// The network message classes, types 1..17 (MessageType; CreateMessage makes one by type); see docs/notes/network.md.
// OnShutdown gives the fields a constructor sets their
// new values (references dropped; a field the constructor leaves unset keeps its value), Write / Read the fields at
// the cursor. Clone (with the copy constructor) is only for the messages that are shared twice (RefObject::Clone).

#include "game/net/net_session.h"
#include "engine/core/core_refcount.h"
#include "engine/core/core_streams.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace net {

class BindingMessage;
class FileMessage;
class FileChunkMessage;
class PartyMemberMessage;
class PlayerMessage;

// ---- Client to server ---------------------------------------------------------------------------

// Type 5: the local player's login, kept by the client; the server makes a player record and a binding of it.
class LoginMessage final : public Message {
public:
    LoginMessage();
    ~LoginMessage() override;

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Login;
    MessageType Type() const override { return kType; }

    std::string name;
    std::string text;
    std::uint8_t host = 0;          // asks to host (honoured with a local client)
    PlayerStatus state = PlayerStatus::Fresh;     // where the player is
    std::int32_t id = -1;           // the player record's id
    Ref<BindingMessage> binding{};    // the server's binding for this login (not sent)
};

// One string: the base of SayMessage, ZoneMessage and MapMessage, which differ only in their classes.
class TextMessage : public Message {
public:
    void OnShutdown() override;

    std::string text;

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

    SayMessage* Clone() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Say;
    MessageType Type() const override { return kType; }
};

// Type 7: a command for the server (ClientSession::SendCommand).
class CommandMessage final : public Message {
public:
    CommandMessage();                                // kind is not set
    CommandMessage(const CommandMessage& other);
    ~CommandMessage() override {}

    void OnShutdown() override;
    CommandMessage* Clone() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Command;
    MessageType Type() const override { return kType; }

    CommandKind kind = {};
    std::string text;
};

// Type 13: a client's request for a shared file: the file message (sent) and the requesting connection on the
// server.
class FileRequestMessage final : public Message {
public:
    FileRequestMessage();
    ~FileRequestMessage() override;

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::FileRequest;
    MessageType Type() const override { return kType; }

    Ref<FileMessage> file{};
    Ref<Connection> connection{};    // not sent
};

// Type 15: a trade offer, the party a player offers to a partner.
class PartyMessage final : public Message {
public:
    PartyMessage();
    ~PartyMessage() override;

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Party;
    MessageType Type() const override { return kType; }

    std::vector<HeldRef<PartyMemberMessage>> members;
    std::uint32_t money = 0;         // the offered party's money (Party::Money)
    std::uint8_t accepted = 0;       // the partner's offer is accepted
    std::uint32_t acceptedRevision = 0;    // the partner's revision accepted
    std::uint32_t revision = 1;
    Ref<PlayerMessage> player{};      // the offering player's record
    Ref<PlayerMessage> partner{};     // the partner's record
    std::uint8_t trading = 0;        // in a trade (server side; not sent)
};

// Type 16: one member of an offered party, serialised into a memory stream.
class PartyMemberMessage final : public Message {
public:
    PartyMemberMessage() {}
    ~PartyMemberMessage() override {}

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::PartyMember;
    MessageType Type() const override { return kType; }

    MemoryStream stream;
};

// ---- Server to client ---------------------------------------------------------------------------

// Type 1: a player record.
class PlayerMessage final : public Message {
public:
    PlayerMessage();
    ~PlayerMessage() override {}

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Player;
    MessageType Type() const override { return kType; }

    std::string name;
    std::string text;
    PlayerStatus state = PlayerStatus::InBase;    // from the login
    std::uint8_t host = 0;          // the hosting player
    std::int32_t id = -1;           // from the login
    std::int32_t serial = -1;       // from the server's player serial (not sent)
    std::int32_t number = -1;       // the lowest number free when joining (not sent)
    std::uint32_t firstLine = 0;    // the first chat line it receives (not sent)
};

// Type 2: a chat or system line.
class LineMessage final : public Message {
public:
    LineMessage();                               // number is not set
    ~LineMessage() override;

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Line;
    MessageType Type() const override { return kType; }

    std::uint32_t number = 0;
    std::string text;
    Ref<RefObject> connection{};            // the only receiver (null: everyone; not sent)
};

// Type 3: where the host's game is (built by AnnounceHost).
class HostMessage final : public Message {
public:
    HostMessage();
    ~HostMessage() override {}

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Host;
    MessageType Type() const override { return kType; }

    std::string address;            // "host:port"
    std::string zone;
};

// Type 4: binds a player record (sent) to its connection on the server.
class BindingMessage final : public Message {
public:
    BindingMessage();
    ~BindingMessage() override;

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Binding;
    MessageType Type() const override { return kType; }

    Ref<PlayerMessage> player{};
    Ref<Connection> connection{};    // not sent
};

// Type 8: a zone the host may pick next.
class ZoneMessage final : public TextMessage {
public:
    ZoneMessage();
    ~ZoneMessage() override {}

    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Zone;
    MessageType Type() const override { return kType; }
};

// Type 9: the map being played.
class MapMessage final : public TextMessage {
public:
    MapMessage() {}
    ~MapMessage() override {}

    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Map;
    MessageType Type() const override { return kType; }
};

// Type 10: a script variable.
class VariableMessage final : public Message {
public:
    VariableMessage();                                 // value and serial are not set
    ~VariableMessage() override {}

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Variable;
    MessageType Type() const override { return kType; }

    std::string name;
    float value = 0.0f;
    std::uint32_t serial = 0;    // from g_variableSerial
};

// Type 11: a shared file.
class FileMessage final : public Message {
public:
    FileMessage();
    ~FileMessage() override;

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::File;
    MessageType Type() const override { return kType; }

    std::uint16_t chunkCount = 0;
    std::int32_t size = 0;       // in bytes
    std::string name;
    std::vector<SharedRef<FileChunkMessage>> chunks;    // not sent
};

// Type 12: 64 bytes of a shared file.
class FileChunkMessage final : public Message {
public:
    static constexpr std::size_t kDataSize = 0x40;

    FileChunkMessage() {}
    FileChunkMessage(const FileChunkMessage& other);
    ~FileChunkMessage() override;

    void OnShutdown() override;
    FileChunkMessage* Clone() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::FileChunk;
    MessageType Type() const override { return kType; }

    Ref<FileMessage> file{};
    std::uint16_t index = 0;
    std::uint8_t data[kDataSize] = {};
};

// Type 14: an award for one connection (the server queues it with a delay; ClientSession::ApplyAwards).
class AwardMessage final : public Message {
public:
    AwardMessage();
    AwardMessage(const AwardMessage& other);
    ~AwardMessage() override;

    void OnShutdown() override;
    AwardMessage* Clone() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Award;
    MessageType Type() const override { return kType; }

    Ref<RefObject> connection{};         // the receiver (not sent)
    AwardKind kind = {};
    float value = 0.0f;
    std::string text;            // the item's name
};

// Type 17: a trade between two offers.
class TradeMessage final : public Message {
public:
    TradeMessage() {}
    ~TradeMessage() override;

    void OnShutdown() override;
    void Write(Connection* peer, BufferCursor* cursor) override;
    void Read(Connection* peer, BufferCursor* cursor) override;

    static constexpr MessageType kType = MessageType::Trade;
    MessageType Type() const override { return kType; }

    Ref<PlayerMessage> player{};        // the first offer's player (not sent)
    Ref<PlayerMessage> partner{};       // its partner (not sent)
    Ref<PartyMessage> offer{};
    Ref<PartyMessage> partnerOffer{};
    std::uint8_t done = 0;                   // both sides accepted
    std::uint32_t revision = 0;              // the first offer's revision traded
    std::uint32_t partnerRevision = 0;       // the second's
};

// The serial the server's next variable or map record gets.
inline std::uint32_t g_variableSerial = /*0079B608*/ {};

} // namespace net
