#pragma once

// The network message classes, types 1..17 (SessionBase::RegisterMessageTypes); see docs/notes/network.md.
// Each has a factory (Create: a new message with no references); Clone copies with the copy constructor,
// OnShutdown resets the message in place (its result is undefined), Write / Read the fields at the cursor.

#include "net_classes.h"
#include "core_refcount.h"
#include "core_streams.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace net {

class BindingMessage;
class FileMessage;
class PlayerMessage;

// ---- Client to server ---------------------------------------------------------------------------

// Type 5: the local player's login, kept by the client; the server makes a player record and a binding of it.
class LoginMessage final : public Message {
public:
    LoginMessage();
    LoginMessage(const LoginMessage& other);
    ~LoginMessage() override;

    void OnShutdown() override;
    LoginMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::string name;
    std::string text;
    std::uint8_t host = 0;          // asks to host (honoured with a local client)
    std::uint32_t state = 4;        // the player record's state
    std::int32_t id = -1;           // the player record's id
    Ref<BindingMessage> binding{};    // the server's binding for this login (not sent)
};

// One string: the base of SayMessage, ZoneMessage and MapMessage, which differ only in their classes.
class TextMessage : public Message {
public:
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

    void OnShutdown() override;
    SayMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();
};

// Type 7: a command for the server (ClientSession::SendCommand).
class CommandMessage final : public Message {
public:
    CommandMessage();                                // kind is not set
    CommandMessage(const CommandMessage& other);
    ~CommandMessage() override {}

    void OnShutdown() override;
    CommandMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::uint32_t kind;
    std::string text;
};

// Two counted pointers; only the first is sent. The base of BindingMessage and
// FileRequestMessage, which differ only in their classes and the names of the pointers.
class PairMessage : public Message {
public:
    Ref<CRefObject> first{};
    Ref<Connection> second{};    // the connection on the server (not sent)

protected:
    PairMessage();
    PairMessage(const PairMessage& other);
    ~PairMessage() override;
    void WriteFirst(BufferCursor* cursor);
    void ReadFirst(BufferCursor* cursor);
};

// Type 13: a client's request for a shared file (first the file message, second the
// requesting connection on the server).
class FileRequestMessage final : public PairMessage {
public:
    FileRequestMessage();
    FileRequestMessage(const FileRequestMessage& other) : PairMessage(other) {}
    ~FileRequestMessage() override {}

    void OnShutdown() override;
    FileRequestMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    Ref<CRefObject>& FileRef() { return first; }
    Ref<Connection>& ConnectionRef() { return second; }
};

// Type 15: a trade offer, the party a player offers to a partner.
class PartyMessage final : public Message {
public:
    PartyMessage();
    PartyMessage(const PartyMessage& other);
    ~PartyMessage() override;

    void OnShutdown() override;
    PartyMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::vector<HeldRef<CRefObject>> members;               // PartyMemberMessages
    std::uint32_t money = 0;         // the offered party's money (CParty::Money)
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
    PartyMemberMessage(const PartyMemberMessage& other);
    ~PartyMemberMessage() override {}

    void OnShutdown() override;
    PartyMemberMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    CMemoryStream stream;
};

// ---- Server to client ---------------------------------------------------------------------------

// Type 1: a player record.
class PlayerMessage final : public Message {
public:
    PlayerMessage();
    PlayerMessage(const PlayerMessage& other);
    ~PlayerMessage() override {}

    void OnShutdown() override;
    PlayerMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::string name;
    std::string text;
    std::uint32_t state = 1;        // from the login
    std::uint8_t host = 0;          // the hosting player
    std::int32_t id = -1;           // from the login
    std::int32_t serial = -1;       // from the server's player serial (not sent)
    std::int32_t number = -1;       // the lowest number free when joining (not sent)
    std::uint32_t firstLine;        // the first chat line it receives (not sent)
};

// Type 2: a chat or system line.
class LineMessage final : public Message {
public:
    LineMessage();                               // number is not set
    LineMessage(const LineMessage& other);
    ~LineMessage() override;

    void OnShutdown() override;
    LineMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::uint32_t number;
    std::string text;
    Ref<CRefObject> connection{};            // the only receiver (null: everyone; not sent)
};

// Type 3: where the host's game is (built by AnnounceHost).
class HostMessage final : public Message {
public:
    HostMessage();
    HostMessage(const HostMessage& other);
    ~HostMessage() override {}

    void OnShutdown() override;
    HostMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::string address;            // "host:port"
    std::string zone;
};

// Type 4: binds a player record (first) to its connection (second, not sent).
class BindingMessage final : public PairMessage {
public:
    BindingMessage();
    BindingMessage(const BindingMessage& other) : PairMessage(other) {}
    ~BindingMessage() override {}

    void OnShutdown() override;
    BindingMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    Ref<CRefObject>& PlayerRef() { return first; }
    Ref<Connection>& ConnectionRef() { return second; }
    PlayerMessage* Player() const { return static_cast<PlayerMessage*>(first.Get()); }
};

// Type 8: a zone the host may pick next.
class ZoneMessage final : public TextMessage {
public:
    ZoneMessage();
    ZoneMessage(const ZoneMessage& other) : TextMessage(other) {}
    ~ZoneMessage() override {}

    void OnShutdown() override;
    ZoneMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();
};

// Type 9: the map being played.
class MapMessage final : public TextMessage {
public:
    MapMessage() {}
    MapMessage(const MapMessage& other) : TextMessage(other) {}
    ~MapMessage() override {}

    void OnShutdown() override;
    MapMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();
};

// Type 10: a script variable.
class VariableMessage final : public Message {
public:
    VariableMessage();                                 // value and serial are not set
    VariableMessage(const VariableMessage& other);
    ~VariableMessage() override {}

    void OnShutdown() override;
    VariableMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::string name;
    float value;
    std::uint32_t serial;        // from g_variableSerial
};

// Type 11: a shared file.
class FileMessage final : public Message {
public:
    FileMessage();
    FileMessage(const FileMessage& other);
    ~FileMessage() override;

    void OnShutdown() override;
    FileMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    std::uint16_t chunkCount;
    std::int32_t size;           // in bytes
    std::string name;
    std::vector<SharedRef<CRefObject>> chunks;    // the chunk messages (not sent)
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
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    Ref<FileMessage> file{};
    std::uint16_t index;
    std::uint8_t data[kDataSize];
};

// Type 14: an award for one connection (the server queues it with a delay; ClientSession::ApplyAwards).
class AwardMessage final : public Message {
public:
    AwardMessage();
    AwardMessage(const AwardMessage& other);
    ~AwardMessage() override;

    void OnShutdown() override;
    AwardMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

    Ref<CRefObject> connection{};         // the receiver (not sent)
    std::uint32_t kind;          // 0 experience, 1 gold, 2 item, 3 remove item, 4 quest reset
    float value;
    std::string text;            // the item's name
};

// Type 17: a trade between two offers.
class TradeMessage final : public Message {
public:
    TradeMessage() {}
    TradeMessage(const TradeMessage& other);
    ~TradeMessage() override;

    void OnShutdown() override;
    TradeMessage* Clone() override;
    void Write(BufferCursor* cursor) override;
    void Read(BufferCursor* cursor) override;

    static Message* Create();

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
