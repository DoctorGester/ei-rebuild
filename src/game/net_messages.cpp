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
const char* EmptyText() { return "?"; }

void AssignEmptyText(std::string& s) {
    const char* text = EmptyText();
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

Message* LoginMessage::Create() { return NewCounted<LoginMessage>(); }
void LoginMessage::OnShutdown() {
    Rebuild(this);
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

SayMessage::SayMessage() {}
Message* SayMessage::Create() { return NewCounted<SayMessage>(); }
SayMessage* SayMessage::Clone() { return NewCounted<SayMessage>(*this); }
void SayMessage::OnShutdown() {
    Rebuild(this);
}
void SayMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void SayMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

ZoneMessage::ZoneMessage() {}
Message* ZoneMessage::Create() { return NewCounted<ZoneMessage>(); }
void ZoneMessage::OnShutdown() {
    Rebuild(this);
}
void ZoneMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void ZoneMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

Message* MapMessage::Create() { return NewCounted<MapMessage>(); }
void MapMessage::OnShutdown() {
    Rebuild(this);
}
void MapMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void MapMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

// CommandMessage.

CommandMessage::CommandMessage() {}
CommandMessage::CommandMessage(const CommandMessage& other) : Message(other), kind(other.kind), text(other.text) {}

Message* CommandMessage::Create() { return NewCounted<CommandMessage>(); }
CommandMessage* CommandMessage::Clone() { return NewCounted<CommandMessage>(*this); }
void CommandMessage::OnShutdown() {
    Rebuild(this);
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
Message* BindingMessage::Create() { return NewCounted<BindingMessage>(); }
void BindingMessage::OnShutdown() {
    Rebuild(this);
}
void BindingMessage::Write(BufferCursor* cursor) { PutObject(cursor, player); }
void BindingMessage::Read(BufferCursor* cursor) { TakeObject(cursor, player); }

FileRequestMessage::FileRequestMessage() {}
FileRequestMessage::~FileRequestMessage() {}
Message* FileRequestMessage::Create() { return NewCounted<FileRequestMessage>(); }
void FileRequestMessage::OnShutdown() {
    Rebuild(this);
}
void FileRequestMessage::Write(BufferCursor* cursor) { PutObject(cursor, file); }
void FileRequestMessage::Read(BufferCursor* cursor) { TakeObject(cursor, file); }

// PlayerMessage.

PlayerMessage::PlayerMessage() {}

Message* PlayerMessage::Create() { return NewCounted<PlayerMessage>(); }
void PlayerMessage::OnShutdown() {
    Rebuild(this);
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

Message* LineMessage::Create() { return NewCounted<LineMessage>(); }
void LineMessage::OnShutdown() {
    Rebuild(this);
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

Message* HostMessage::Create() { return NewCounted<HostMessage>(); }
void HostMessage::OnShutdown() {
    Rebuild(this);
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

Message* VariableMessage::Create() { return NewCounted<VariableMessage>(); }
void VariableMessage::OnShutdown() {
    Rebuild(this);
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

Message* FileMessage::Create() { return NewCounted<FileMessage>(); }
void FileMessage::OnShutdown() {
    Rebuild(this);
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

Message* FileChunkMessage::Create() { return NewCounted<FileChunkMessage>(); }
FileChunkMessage* FileChunkMessage::Clone() { return NewCounted<FileChunkMessage>(*this); }
void FileChunkMessage::OnShutdown() {
    Rebuild(this);
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

Message* AwardMessage::Create() { return NewCounted<AwardMessage>(); }
AwardMessage* AwardMessage::Clone() { return NewCounted<AwardMessage>(*this); }
void AwardMessage::OnShutdown() {
    Rebuild(this);
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

Message* TradeMessage::Create() { return NewCounted<TradeMessage>(); }
void TradeMessage::OnShutdown() {
    Rebuild(this);
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


Message* PartyMemberMessage::Create() { return NewCounted<PartyMemberMessage>(); }
void PartyMemberMessage::OnShutdown() {
    Rebuild(this);
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

Message* PartyMessage::Create() { return NewCounted<PartyMessage>(); }
void PartyMessage::OnShutdown() {
    Rebuild(this);
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

ServerEntry* ServerEntry::Clone() { return NewCounted<ServerEntry>(*this); }
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
