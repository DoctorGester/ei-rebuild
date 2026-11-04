// The network message classes (net_messages.h) and the message base: for each class its factory, constructor,
// OnShutdown, Write, Read and destructor (Clone and the copy constructor only where a message is shared twice); the same for the lobby's server entry
// (net_classes.h; no factory).
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

// Object references through the connection being written or read.
template <typename T>
void PutObject(BufferCursor* cursor, const Ref<T>& ref) {
    g_currentConnection->WriteObjectRef(cursor, ref.Get());
}
template <typename T>
void TakeObject(BufferCursor* cursor, Ref<T>& ref) {
    ref.Assign(static_cast<T*>(g_currentConnection->ReadObjectRef(cursor)));
}

} // namespace

// The message base.

Message::Message() {}

Message::~Message() {}

// LoginMessage.

LoginMessage::LoginMessage() {}
LoginMessage::~LoginMessage() {}

Message* LoginMessage::Create() { return new LoginMessage(); }
void LoginMessage::OnShutdown() {
    Message::OnShutdown();
    name.clear();
    text.clear();
    host = 0;
    state = PlayerStatus::Fresh;
    id = -1;
    binding.Reset();
}
void LoginMessage::Write(BufferCursor* cursor) {
    cursor->PutString(name);
    cursor->PutString(text);
    cursor->Put(state);
    cursor->Put(host);
    cursor->Put(id);
}
void LoginMessage::Read(BufferCursor* cursor) {
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
Message* SayMessage::Create() { return new SayMessage(); }
SayMessage* SayMessage::Clone() { return new SayMessage(*this); }
void SayMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void SayMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

ZoneMessage::ZoneMessage() {}
Message* ZoneMessage::Create() { return new ZoneMessage(); }
void ZoneMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void ZoneMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

Message* MapMessage::Create() { return new MapMessage(); }
void MapMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void MapMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

// CommandMessage.

CommandMessage::CommandMessage() {}
CommandMessage::CommandMessage(const CommandMessage& other) : Message(other), kind(other.kind), text(other.text) {}

Message* CommandMessage::Create() { return new CommandMessage(); }
CommandMessage* CommandMessage::Clone() { return new CommandMessage(*this); }
void CommandMessage::OnShutdown() {
    Message::OnShutdown();
    text.clear();
}
void CommandMessage::Write(BufferCursor* cursor) {
    cursor->Put(kind);
    cursor->PutString(text);
}
void CommandMessage::Read(BufferCursor* cursor) {
    cursor->Take(kind);
    cursor->TakeString(text);
}

BindingMessage::BindingMessage() {}
BindingMessage::~BindingMessage() {}
Message* BindingMessage::Create() { return new BindingMessage(); }
void BindingMessage::OnShutdown() {
    Message::OnShutdown();
    player.Reset();
    connection.Reset();
}
void BindingMessage::Write(BufferCursor* cursor) { PutObject(cursor, player); }
void BindingMessage::Read(BufferCursor* cursor) { TakeObject(cursor, player); }

FileRequestMessage::FileRequestMessage() {}
FileRequestMessage::~FileRequestMessage() {}
Message* FileRequestMessage::Create() { return new FileRequestMessage(); }
void FileRequestMessage::OnShutdown() {
    Message::OnShutdown();
    file.Reset();
    connection.Reset();
}
void FileRequestMessage::Write(BufferCursor* cursor) { PutObject(cursor, file); }
void FileRequestMessage::Read(BufferCursor* cursor) { TakeObject(cursor, file); }

// PlayerMessage.

PlayerMessage::PlayerMessage() {}

Message* PlayerMessage::Create() { return new PlayerMessage(); }
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
void PlayerMessage::Write(BufferCursor* cursor) {
    cursor->PutString(name);
    cursor->PutString(text);
    cursor->Put(state);
    cursor->Put(host);
    cursor->Put(id);
    cursor->Put(serial);
}
void PlayerMessage::Read(BufferCursor* cursor) {
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

Message* LineMessage::Create() { return new LineMessage(); }
void LineMessage::OnShutdown() {
    Message::OnShutdown();
    text.clear();
    connection.Reset();
}
void LineMessage::Write(BufferCursor* cursor) {
    cursor->Put(number);
    cursor->PutString(text);
}
void LineMessage::Read(BufferCursor* cursor) {
    cursor->Take(number);
    cursor->TakeString(text);
}

// HostMessage.

HostMessage::HostMessage() {}

Message* HostMessage::Create() { return new HostMessage(); }
void HostMessage::OnShutdown() {
    Message::OnShutdown();
    address.clear();
    zone.clear();
}
void HostMessage::Write(BufferCursor* cursor) {
    cursor->PutString(address);
    cursor->PutString(zone);
}
void HostMessage::Read(BufferCursor* cursor) {
    cursor->TakeString(address);
    cursor->TakeString(zone);
}

// VariableMessage.

VariableMessage::VariableMessage() {}

Message* VariableMessage::Create() { return new VariableMessage(); }
void VariableMessage::OnShutdown() {
    Message::OnShutdown();
    name.clear();
}
void VariableMessage::Write(BufferCursor* cursor) {
    cursor->PutString(name);
    cursor->Put(value);
    cursor->Put(serial);
}
void VariableMessage::Read(BufferCursor* cursor) {
    cursor->TakeString(name);
    cursor->Take(value);
    cursor->Take(serial);
}

// FileMessage.

FileMessage::FileMessage() {}
FileMessage::~FileMessage() {}

Message* FileMessage::Create() { return new FileMessage(); }
void FileMessage::OnShutdown() {
    Message::OnShutdown();
    name.clear();
    chunks.clear();
}
void FileMessage::Write(BufferCursor* cursor) {
    cursor->Put(chunkCount);
    cursor->Put(size);
    cursor->PutString(name);
}
void FileMessage::Read(BufferCursor* cursor) {
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

Message* FileChunkMessage::Create() { return new FileChunkMessage(); }
FileChunkMessage* FileChunkMessage::Clone() { return new FileChunkMessage(*this); }
void FileChunkMessage::OnShutdown() {
    Message::OnShutdown();
    file.Reset();
}
void FileChunkMessage::Write(BufferCursor* cursor) {
    PutObject(cursor, file);
    cursor->Put(index);
    cursor->Put(data);
}
void FileChunkMessage::Read(BufferCursor* cursor) {
    TakeObject(cursor, file);
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

Message* AwardMessage::Create() { return new AwardMessage(); }
AwardMessage* AwardMessage::Clone() { return new AwardMessage(*this); }
void AwardMessage::OnShutdown() {
    Message::OnShutdown();
    connection.Reset();
    text.clear();
}
void AwardMessage::Write(BufferCursor* cursor) {
    cursor->Put(kind);
    cursor->Put(value);
    cursor->PutString(text);
}
void AwardMessage::Read(BufferCursor* cursor) {
    cursor->Take(kind);
    cursor->Take(value);
    cursor->TakeString(text);
}

// TradeMessage.

TradeMessage::~TradeMessage() {}

Message* TradeMessage::Create() { return new TradeMessage(); }
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
void TradeMessage::Write(BufferCursor* cursor) {
    PutObject(cursor, offer);
    PutObject(cursor, partnerOffer);
    cursor->Put(done);
    cursor->Put(revision);
    cursor->Put(partnerRevision);
}
void TradeMessage::Read(BufferCursor* cursor) {
    TakeObject(cursor, offer);
    TakeObject(cursor, partnerOffer);
    cursor->Take(done);
    cursor->Take(revision);
    cursor->Take(partnerRevision);
}

// PartyMemberMessage.


Message* PartyMemberMessage::Create() { return new PartyMemberMessage(); }
void PartyMemberMessage::OnShutdown() {
    Message::OnShutdown();
    stream = MemoryStream();
}
void PartyMemberMessage::Write(BufferCursor* cursor) {
    ByteStream* currentStream = &stream;
    std::uint32_t size = currentStream->Size();
    cursor->Put(static_cast<std::uint16_t>(size));
    cursor->WriteBytes(stream.data.data(), size & 0xFFFF);
}
void PartyMemberMessage::Read(BufferCursor* cursor) {
    std::uint16_t n;
    cursor->Take(n);
    stream.Resize(n);
    cursor->ReadBytes(stream.data.data(), n);
}

// PartyMessage.

PartyMessage::PartyMessage() {}
PartyMessage::~PartyMessage() {}

Message* PartyMessage::Create() { return new PartyMessage(); }
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
void PartyMessage::Write(BufferCursor* cursor) {
    std::int32_t count = static_cast<std::int32_t>(members.size());
    cursor->Put(count);
    for (std::int32_t i = 0; i < count; ++i) PutObject(cursor, members[i]);
    cursor->Put(money);
    cursor->Put(accepted);
    cursor->Put(acceptedRevision);
    cursor->Put(revision);
    PutObject(cursor, player);
    PutObject(cursor, partner);
}
void PartyMessage::Read(BufferCursor* cursor) {
    std::int32_t count;
    cursor->Take(count);
    members.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) TakeObject(cursor, members[i]);
    cursor->Take(money);
    cursor->Take(accepted);
    cursor->Take(acceptedRevision);
    cursor->Take(revision);
    TakeObject(cursor, player);
    TakeObject(cursor, partner);
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
