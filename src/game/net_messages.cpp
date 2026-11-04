// The network message classes (net_messages.h) and the message base: for each class its factory, constructor,
// OnShutdown, Write, Read and destructor (Clone and the copy constructor only where a message is shared twice); the same for the lobby's server entry
// (net_classes.h; no factory).
// More: docs/notes/network.md, "Sessions and messages".
#include "net_messages.h"
#include "asset_text.h"
#include "net_connection.h"

#include "lib/crt.h"
#include "lib/str.h"

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

// The byte cursor.
template <typename T>
void Put(BufferCursor* cursor, T value) {
    std::memcpy(cursor->p, &value, sizeof(T));
    cursor->p += sizeof(T);
}
template <typename T>
void Take(BufferCursor* cursor, T& value) {
    std::memcpy(&value, cursor->p, sizeof(T));
    cursor->p += sizeof(T);
}
void PutBytes(BufferCursor* cursor, const void* from, std::size_t n) {
    std::memcpy(cursor->p, from, n);
    cursor->p += n;
}
void TakeBytes(BufferCursor* cursor, void* to, std::size_t n) {
    std::memcpy(to, cursor->p, n);
    cursor->p += n;
}
// Text on the wire is in the asset code page, as vanilla peers send it (asset_text.h).
void PutString(BufferCursor* cursor, const std::string& s) {
    const std::string bytes = text::ToAssetText(std::string_view(s.c_str()));
    PutBytes(cursor, bytes.c_str(), bytes.size() + 1);
}
// The text runs to its zero byte, or to the end of the bytes read (the cursor's limit) without one; the original
// read on past a packet's end until it met a zero byte.
void TakeString(BufferCursor* cursor, std::string& s) {
    const char* text = reinterpret_cast<const char*>(cursor->p);
    std::size_t room = SIZE_MAX;
    if (cursor->limit) room = cursor->p < cursor->limit ? static_cast<std::size_t>(cursor->limit - cursor->p) : 0;
    const std::size_t length = room == SIZE_MAX ? std::strlen(text) : str::BoundedLength(text, room);
    s.assign(text, text + length);
    text::FromAssetText(&s);
    cursor->p += length < room ? length + 1 : length;
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
    PutString(cursor, name);
    PutString(cursor, text);
    Put(cursor, state);
    Put(cursor, host);
    Put(cursor, id);
}
void LoginMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, name);
    TakeString(cursor, text);
    Take(cursor, state);
    Take(cursor, host);
    Take(cursor, id);
}

// TextMessage: SayMessage, ZoneMessage, MapMessage.

TextMessage::TextMessage() {}
TextMessage::TextMessage(const TextMessage& other) : Message(other), text(other.text) {}
TextMessage::~TextMessage() {}
void TextMessage::WriteText(BufferCursor* cursor) { PutString(cursor, text); }
void TextMessage::ReadText(BufferCursor* cursor) { TakeString(cursor, text); }

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
    Put(cursor, kind);
    PutString(cursor, text);
}
void CommandMessage::Read(BufferCursor* cursor) {
    Take(cursor, kind);
    TakeString(cursor, text);
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
    PutString(cursor, name);
    PutString(cursor, text);
    Put(cursor, state);
    Put(cursor, host);
    Put(cursor, id);
    Put(cursor, serial);
}
void PlayerMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, name);
    TakeString(cursor, text);
    Take(cursor, state);
    Take(cursor, host);
    Take(cursor, id);
    Take(cursor, serial);
}

// LineMessage.

LineMessage::LineMessage() {}
LineMessage::~LineMessage() {}

Message* LineMessage::Create() { return NewCounted<LineMessage>(); }
void LineMessage::OnShutdown() {
    Rebuild(this);
}
void LineMessage::Write(BufferCursor* cursor) {
    Put(cursor, number);
    PutString(cursor, text);
}
void LineMessage::Read(BufferCursor* cursor) {
    Take(cursor, number);
    TakeString(cursor, text);
}

// HostMessage.

HostMessage::HostMessage() {}

Message* HostMessage::Create() { return NewCounted<HostMessage>(); }
void HostMessage::OnShutdown() {
    Rebuild(this);
}
void HostMessage::Write(BufferCursor* cursor) {
    PutString(cursor, address);
    PutString(cursor, zone);
}
void HostMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, address);
    TakeString(cursor, zone);
}

// VariableMessage.

VariableMessage::VariableMessage() {}

Message* VariableMessage::Create() { return NewCounted<VariableMessage>(); }
void VariableMessage::OnShutdown() {
    Rebuild(this);
}
void VariableMessage::Write(BufferCursor* cursor) {
    PutString(cursor, name);
    PutBytes(cursor, &value, 4);
    Put(cursor, serial);
}
void VariableMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, name);
    TakeBytes(cursor, &value, 4);
    Take(cursor, serial);
}

// FileMessage.

FileMessage::FileMessage() {}
FileMessage::~FileMessage() {}

Message* FileMessage::Create() { return NewCounted<FileMessage>(); }
void FileMessage::OnShutdown() {
    Rebuild(this);
}
void FileMessage::Write(BufferCursor* cursor) {
    Put(cursor, chunkCount);
    Put(cursor, size);
    PutString(cursor, name);
}
void FileMessage::Read(BufferCursor* cursor) {
    Take(cursor, chunkCount);
    Take(cursor, size);
    TakeString(cursor, name);
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
    Put(cursor, index);
    PutBytes(cursor, data, kDataSize);
}
void FileChunkMessage::Read(BufferCursor* cursor) {
    TakeObject(cursor, file);
    Take(cursor, index);
    TakeBytes(cursor, data, kDataSize);
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
    Put(cursor, kind);
    PutBytes(cursor, &value, 4);
    PutString(cursor, text);
}
void AwardMessage::Read(BufferCursor* cursor) {
    Take(cursor, kind);
    TakeBytes(cursor, &value, 4);
    TakeString(cursor, text);
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
    Put(cursor, done);
    Put(cursor, revision);
    Put(cursor, partnerRevision);
}
void TradeMessage::Read(BufferCursor* cursor) {
    TakeObject(cursor, offer);
    TakeObject(cursor, partnerOffer);
    Take(cursor, done);
    Take(cursor, revision);
    Take(cursor, partnerRevision);
}

// PartyMemberMessage.


Message* PartyMemberMessage::Create() { return NewCounted<PartyMemberMessage>(); }
void PartyMemberMessage::OnShutdown() {
    Rebuild(this);
}
void PartyMemberMessage::Write(BufferCursor* cursor) {
    ByteStream* currentStream = &stream;
    std::uint32_t size = currentStream->Size();
    Put(cursor, static_cast<std::uint16_t>(size));
    PutBytes(cursor, stream.data.data(), size & 0xFFFF);
}
void PartyMemberMessage::Read(BufferCursor* cursor) {
    std::uint16_t n;
    Take(cursor, n);
    stream.Resize(n);
    TakeBytes(cursor, stream.data.data(), n);
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
    Put(cursor, count);
    for (std::int32_t i = 0; i < count; ++i) PutObject(cursor, members[i]);
    Put(cursor, money);
    Put(cursor, accepted);
    Put(cursor, acceptedRevision);
    Put(cursor, revision);
    PutObject(cursor, player);
    PutObject(cursor, partner);
}
void PartyMessage::Read(BufferCursor* cursor) {
    std::int32_t count;
    Take(cursor, count);
    members.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) TakeObject(cursor, members[i]);
    Take(cursor, money);
    Take(cursor, accepted);
    Take(cursor, acceptedRevision);
    Take(cursor, revision);
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
    kind = 4;
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
    PutString(cursor, name);
    Put(cursor, kind);
    PutString(cursor, description);
    Put(cursor, players);
    Put(cursor, maxPlayers);
    Put(cursor, password);
}
void ServerEntry::Read(BufferCursor* cursor) {
    TakeString(cursor, name);
    Take(cursor, kind);
    TakeString(cursor, description);
    Take(cursor, players);
    Take(cursor, maxPlayers);
    Take(cursor, password);
}

} // namespace net
