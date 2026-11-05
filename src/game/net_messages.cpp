// The network message classes (net_messages.h), the message base and CreateMessage: for each class its constructor,
// OnShutdown, Write, Read and destructor (Clone and the copy constructor only where a message is shared twice); the
// same for the lobby's server entry (net_classes.h).
// More: docs/notes/network.md, "Sessions and messages".
#include "net_messages.h"
#include "net_connection.h"

#include <cstdint>
#include <cstring>

namespace net {

namespace {

// The empty text the server entry's reset assigns.

void AssignEmptyText(std::string& s) {
    const char* text = "?";
    s.assign(text, text + std::strlen(text));
}

// Object references through the connection writing or reading the message.
template <typename T>
void PutObject(Connection* connection, BufferCursor* cursor, const Ref<T>& ref) {
    connection->WriteObjectRef(cursor, ref.Get());
}
template <typename T>
void TakeObject(Connection* connection, BufferCursor* cursor, Ref<T>& ref) {
    Message* object = connection->ReadObjectRef(cursor);
    ref.Assign(object && object->Type() == T::kType ? static_cast<T*>(object) : nullptr);   // a stream naming another class reads as none
}

} // namespace

// The message base.

Message::Message() {}

Message::~Message() {}

Message* CreateMessage(std::uint8_t type) {
    switch (static_cast<MessageType>(type)) {
    case PlayerMessage::kType: return new PlayerMessage();
    case LineMessage::kType: return new LineMessage();
    case HostMessage::kType: return new HostMessage();
    case BindingMessage::kType: return new BindingMessage();
    case LoginMessage::kType: return new LoginMessage();
    case SayMessage::kType: return new SayMessage();
    case CommandMessage::kType: return new CommandMessage();
    case ZoneMessage::kType: return new ZoneMessage();
    case MapMessage::kType: return new MapMessage();
    case VariableMessage::kType: return new VariableMessage();
    case FileMessage::kType: return new FileMessage();
    case FileChunkMessage::kType: return new FileChunkMessage();
    case FileRequestMessage::kType: return new FileRequestMessage();
    case AwardMessage::kType: return new AwardMessage();
    case PartyMessage::kType: return new PartyMessage();
    case PartyMemberMessage::kType: return new PartyMemberMessage();
    case TradeMessage::kType: return new TradeMessage();
    }
    return nullptr;
}

// LoginMessage.

LoginMessage::LoginMessage() {}
LoginMessage::~LoginMessage() {}

void LoginMessage::OnShutdown() {
    Message::OnShutdown();
    name.clear();
    text.clear();
    host = 0;
    state = PlayerStatus::Fresh;
    id = -1;
    binding.Reset();
}
void LoginMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->PutString(name);
    cursor->PutString(text);
    cursor->Put(state);
    cursor->Put(host);
    cursor->Put(id);
}
void LoginMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->TakeString(name);
    cursor->TakeString(text);
    cursor->Take(state);
    cursor->Take(host);
    cursor->Take(id);
}

// TextMessage: SayMessage, ZoneMessage, MapMessage.

TextMessage::TextMessage() {}
TextMessage::TextMessage(const TextMessage& other) : Message(other), text(other.text) {}
TextMessage::~TextMessage() {}
void TextMessage::WriteText(BufferCursor* cursor) { cursor->PutString(text); }
void TextMessage::ReadText(BufferCursor* cursor) { cursor->TakeString(text); }
void TextMessage::OnShutdown() {
    Message::OnShutdown();
    text.clear();
}

SayMessage::SayMessage() {}
SayMessage* SayMessage::Clone() { return new SayMessage(*this); }
void SayMessage::Write(Connection*, BufferCursor* cursor) { WriteText(cursor); }
void SayMessage::Read(Connection*, BufferCursor* cursor) { ReadText(cursor); }

ZoneMessage::ZoneMessage() {}
void ZoneMessage::Write(Connection*, BufferCursor* cursor) { WriteText(cursor); }
void ZoneMessage::Read(Connection*, BufferCursor* cursor) { ReadText(cursor); }

void MapMessage::Write(Connection*, BufferCursor* cursor) { WriteText(cursor); }
void MapMessage::Read(Connection*, BufferCursor* cursor) { ReadText(cursor); }

// CommandMessage.

CommandMessage::CommandMessage() {}
CommandMessage::CommandMessage(const CommandMessage& other) : Message(other), kind(other.kind), text(other.text) {}

CommandMessage* CommandMessage::Clone() { return new CommandMessage(*this); }
void CommandMessage::OnShutdown() {
    Message::OnShutdown();
    text.clear();
}
void CommandMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->Put(kind);
    cursor->PutString(text);
}
void CommandMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->Take(kind);
    cursor->TakeString(text);
}

BindingMessage::BindingMessage() {}
BindingMessage::~BindingMessage() {}
void BindingMessage::OnShutdown() {
    Message::OnShutdown();
    player.Reset();
    connection.Reset();
}
void BindingMessage::Write(Connection* peer, BufferCursor* cursor) { PutObject(peer, cursor, player); }
void BindingMessage::Read(Connection* peer, BufferCursor* cursor) { TakeObject(peer, cursor, player); }

FileRequestMessage::FileRequestMessage() {}
FileRequestMessage::~FileRequestMessage() {}
void FileRequestMessage::OnShutdown() {
    Message::OnShutdown();
    file.Reset();
    connection.Reset();
}
void FileRequestMessage::Write(Connection* peer, BufferCursor* cursor) { PutObject(peer, cursor, file); }
void FileRequestMessage::Read(Connection* peer, BufferCursor* cursor) { TakeObject(peer, cursor, file); }

// PlayerMessage.

PlayerMessage::PlayerMessage() {}

void PlayerMessage::OnShutdown() {
    Message::OnShutdown();
    name.clear();
    text.clear();
    state = PlayerStatus::InBase;
    host = 0;
    id = -1;
    serial = -1;
    number = -1;
}
void PlayerMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->PutString(name);
    cursor->PutString(text);
    cursor->Put(state);
    cursor->Put(host);
    cursor->Put(id);
    cursor->Put(serial);
}
void PlayerMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->TakeString(name);
    cursor->TakeString(text);
    cursor->Take(state);
    cursor->Take(host);
    cursor->Take(id);
    cursor->Take(serial);
}

// LineMessage.

LineMessage::LineMessage() {}
LineMessage::~LineMessage() {}

void LineMessage::OnShutdown() {
    Message::OnShutdown();
    text.clear();
    connection.Reset();
}
void LineMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->Put(number);
    cursor->PutString(text);
}
void LineMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->Take(number);
    cursor->TakeString(text);
}

// HostMessage.

HostMessage::HostMessage() {}

void HostMessage::OnShutdown() {
    Message::OnShutdown();
    address.clear();
    zone.clear();
}
void HostMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->PutString(address);
    cursor->PutString(zone);
}
void HostMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->TakeString(address);
    cursor->TakeString(zone);
}

// VariableMessage.

VariableMessage::VariableMessage() {}

void VariableMessage::OnShutdown() {
    Message::OnShutdown();
    name.clear();
}
void VariableMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->PutString(name);
    cursor->Put(value);
    cursor->Put(serial);
}
void VariableMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->TakeString(name);
    cursor->Take(value);
    cursor->Take(serial);
}

// FileMessage.

FileMessage::FileMessage() {}
FileMessage::~FileMessage() {}

void FileMessage::OnShutdown() {
    Message::OnShutdown();
    name.clear();
    chunks.clear();
}
void FileMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->Put(chunkCount);
    cursor->Put(size);
    cursor->PutString(name);
}
void FileMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->Take(chunkCount);
    cursor->Take(size);
    cursor->TakeString(name);
}

// FileChunkMessage.

FileChunkMessage::FileChunkMessage(const FileChunkMessage& other) : Message(other) {
    file.Assign(other.file);
    index = other.index;
    std::memcpy(data, other.data, kDataSize);
}
FileChunkMessage::~FileChunkMessage() {}

FileChunkMessage* FileChunkMessage::Clone() { return new FileChunkMessage(*this); }
void FileChunkMessage::OnShutdown() {
    Message::OnShutdown();
    file.Reset();
}
void FileChunkMessage::Write(Connection* peer, BufferCursor* cursor) {
    PutObject(peer, cursor, file);
    cursor->Put(index);
    cursor->Put(data);
}
void FileChunkMessage::Read(Connection* peer, BufferCursor* cursor) {
    TakeObject(peer, cursor, file);
    cursor->Take(index);
    cursor->Take(data);
}

// AwardMessage.

AwardMessage::AwardMessage() {}
AwardMessage::AwardMessage(const AwardMessage& other) : Message(other), kind(other.kind), text(other.text) {
    connection.Assign(other.connection);
    value = other.value;
}
AwardMessage::~AwardMessage() {}

AwardMessage* AwardMessage::Clone() { return new AwardMessage(*this); }
void AwardMessage::OnShutdown() {
    Message::OnShutdown();
    connection.Reset();
    text.clear();
}
void AwardMessage::Write(Connection*, BufferCursor* cursor) {
    cursor->Put(kind);
    cursor->Put(value);
    cursor->PutString(text);
}
void AwardMessage::Read(Connection*, BufferCursor* cursor) {
    cursor->Take(kind);
    cursor->Take(value);
    cursor->TakeString(text);
}

// TradeMessage.

TradeMessage::~TradeMessage() {}

void TradeMessage::OnShutdown() {
    Message::OnShutdown();
    player.Reset();
    partner.Reset();
    offer.Reset();
    partnerOffer.Reset();
    done = 0;
    revision = 0;
    partnerRevision = 0;
}
void TradeMessage::Write(Connection* peer, BufferCursor* cursor) {
    PutObject(peer, cursor, offer);
    PutObject(peer, cursor, partnerOffer);
    cursor->Put(done);
    cursor->Put(revision);
    cursor->Put(partnerRevision);
}
void TradeMessage::Read(Connection* peer, BufferCursor* cursor) {
    TakeObject(peer, cursor, offer);
    TakeObject(peer, cursor, partnerOffer);
    cursor->Take(done);
    cursor->Take(revision);
    cursor->Take(partnerRevision);
}

// PartyMemberMessage.


void PartyMemberMessage::OnShutdown() {
    Message::OnShutdown();
    stream = MemoryStream();
}
void PartyMemberMessage::Write(Connection*, BufferCursor* cursor) {
    ByteStream* currentStream = &stream;
    std::uint32_t size = currentStream->Size();
    cursor->Put(static_cast<std::uint16_t>(size));
    cursor->WriteBytes(stream.data.data(), size & 0xFFFF);
}
void PartyMemberMessage::Read(Connection*, BufferCursor* cursor) {
    std::uint16_t n;
    cursor->Take(n);
    stream.Resize(n);
    cursor->ReadBytes(stream.data.data(), n);
}

// PartyMessage.

PartyMessage::PartyMessage() {}
PartyMessage::~PartyMessage() {}

void PartyMessage::OnShutdown() {
    Message::OnShutdown();
    members.clear();
    money = 0;
    accepted = 0;
    acceptedRevision = 0;
    revision = 1;
    player.Reset();
    partner.Reset();
    trading = 0;
}
void PartyMessage::Write(Connection* peer, BufferCursor* cursor) {
    std::int32_t count = static_cast<std::int32_t>(members.size());
    cursor->Put(count);
    for (std::int32_t i = 0; i < count; ++i) PutObject(peer, cursor, members[i]);
    cursor->Put(money);
    cursor->Put(accepted);
    cursor->Put(acceptedRevision);
    cursor->Put(revision);
    PutObject(peer, cursor, player);
    PutObject(peer, cursor, partner);
}
void PartyMessage::Read(Connection* peer, BufferCursor* cursor) {
    std::int32_t count;
    cursor->Take(count);
    members.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) TakeObject(peer, cursor, members[i]);
    cursor->Take(money);
    cursor->Take(accepted);
    cursor->Take(acceptedRevision);
    cursor->Take(revision);
    TakeObject(peer, cursor, player);
    TakeObject(peer, cursor, partner);
}

// ServerEntryBase and ServerEntry (net_classes.h): made by the server list code.

ServerEntryBase::ServerEntryBase(const ServerEntryBase& other) : RefObject(), address(other.address) {
    timeout = other.timeout;     // the floats copied bit for bit
    ping = other.ping;
    foreign = other.foreign;
    state = other.state;
}
ServerEntryBase::~ServerEntryBase() {}

ServerEntry::ServerEntry() {
    AssignEmptyText(name);
    kind = kNoKind;
    AssignEmptyText(description);
    players = 0;
    maxPlayers = 0;
    password = 0;
}
ServerEntry::ServerEntry(const ServerEntry& other)
    : ServerEntryBase(other), name(other.name), kind(other.kind), description(other.description),
      players(other.players), maxPlayers(other.maxPlayers), password(other.password) {}

ServerEntry* ServerEntry::Clone() { return new ServerEntry(*this); }
void ServerEntry::Write(BufferCursor* cursor) {
    cursor->PutString(name);
    cursor->Put(kind);
    cursor->PutString(description);
    cursor->Put(players);
    cursor->Put(maxPlayers);
    cursor->Put(password);
}
void ServerEntry::Read(BufferCursor* cursor) {
    cursor->TakeString(name);
    cursor->Take(kind);
    cursor->TakeString(description);
    cursor->Take(players);
    cursor->Take(maxPlayers);
    cursor->Take(password);
}

} // namespace net
