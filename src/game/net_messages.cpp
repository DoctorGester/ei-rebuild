// The network message classes (net_messages.h) and the message base: for each class its factory, constructor,
// copy (Clone's), OnShutdown, Clone, Write, Read and destructor; the same for the lobby's server entry
// (net_classes.h; no factory).
// More: docs/notes/comments-c22i.md, "net_messages.cpp".
#include "ei/x87.h"
#include "net_messages.h"
#include "asset_text.h"
#include "net_connection.h"

#include "lib/crt.h"

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
void TakeString(BufferCursor* cursor, std::string& s) {
    const char* text = reinterpret_cast<const char*>(cursor->p);
    std::size_t length = std::strlen(text);
    s.assign(text, text + length);
    text::FromAssetText(&s);
    cursor->p += length + 1;
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
LoginMessage::LoginMessage(const LoginMessage& other)
    : Message(other), name(other.name), text(other.text), host(other.host), state(other.state),
      id(other.id) {
    binding.Assign(other.binding);
}
LoginMessage::~LoginMessage() {}

Message* LoginMessage::Create() { return NewCounted<LoginMessage>(); }
LoginMessage* LoginMessage::Clone() { return NewCounted<LoginMessage>(*this); }
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
ZoneMessage* ZoneMessage::Clone() { return NewCounted<ZoneMessage>(*this); }
void ZoneMessage::OnShutdown() {
    Rebuild(this);
}
void ZoneMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void ZoneMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

Message* MapMessage::Create() { return NewCounted<MapMessage>(); }
MapMessage* MapMessage::Clone() { return NewCounted<MapMessage>(*this); }
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

// PairMessage: BindingMessage, FileRequestMessage.

PairMessage::PairMessage() {}
PairMessage::PairMessage(const PairMessage& other) : Message(other) {
    first.Assign(other.first);
    second.Assign(other.second);
}
PairMessage::~PairMessage() {}
void PairMessage::WriteFirst(BufferCursor* cursor) { PutObject(cursor, first); }
void PairMessage::ReadFirst(BufferCursor* cursor) { TakeObject(cursor, first); }

BindingMessage::BindingMessage() {}
Message* BindingMessage::Create() { return NewCounted<BindingMessage>(); }
BindingMessage* BindingMessage::Clone() { return NewCounted<BindingMessage>(*this); }
void BindingMessage::OnShutdown() {
    Rebuild(this);
}
void BindingMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }
void BindingMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }

FileRequestMessage::FileRequestMessage() {}
Message* FileRequestMessage::Create() { return NewCounted<FileRequestMessage>(); }
FileRequestMessage* FileRequestMessage::Clone() { return NewCounted<FileRequestMessage>(*this); }
void FileRequestMessage::OnShutdown() {
    Rebuild(this);
}
void FileRequestMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }
void FileRequestMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }

// PlayerMessage.

PlayerMessage::PlayerMessage() {}
PlayerMessage::PlayerMessage(const PlayerMessage& other)
    : Message(other), name(other.name), text(other.text), state(other.state), host(other.host),
      id(other.id), serial(other.serial), number(other.number), firstLine(other.firstLine) {}

Message* PlayerMessage::Create() { return NewCounted<PlayerMessage>(); }
PlayerMessage* PlayerMessage::Clone() { return NewCounted<PlayerMessage>(*this); }
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
LineMessage::LineMessage(const LineMessage& other) : Message(other), number(other.number), text(other.text) {
    connection.Assign(other.connection);
}
LineMessage::~LineMessage() {}

Message* LineMessage::Create() { return NewCounted<LineMessage>(); }
LineMessage* LineMessage::Clone() { return NewCounted<LineMessage>(*this); }
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
HostMessage::HostMessage(const HostMessage& other) : Message(other), address(other.address), zone(other.zone) {}

Message* HostMessage::Create() { return NewCounted<HostMessage>(); }
HostMessage* HostMessage::Clone() { return NewCounted<HostMessage>(*this); }
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
VariableMessage::VariableMessage(const VariableMessage& other) : Message(other), name(other.name) {
    x87::CopyBits(value, other.value);
    serial = other.serial;
}

Message* VariableMessage::Create() { return NewCounted<VariableMessage>(); }
VariableMessage* VariableMessage::Clone() { return NewCounted<VariableMessage>(*this); }
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
FileMessage::FileMessage(const FileMessage& other)
    : Message(other), chunkCount(other.chunkCount), size(other.size), name(other.name), chunks(other.chunks) {}
FileMessage::~FileMessage() {}

Message* FileMessage::Create() { return NewCounted<FileMessage>(); }
FileMessage* FileMessage::Clone() { return NewCounted<FileMessage>(*this); }
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
    x87::CopyBits(value, other.value);
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

TradeMessage::TradeMessage(const TradeMessage& other)
    : Message(other), done(other.done), revision(other.revision), partnerRevision(other.partnerRevision) {
    player.Assign(other.player);
    partner.Assign(other.partner);
    offer.Assign(other.offer);
    partnerOffer.Assign(other.partnerOffer);
}
TradeMessage::~TradeMessage() {}

Message* TradeMessage::Create() { return NewCounted<TradeMessage>(); }
TradeMessage* TradeMessage::Clone() { return NewCounted<TradeMessage>(*this); }
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

// Original bug kept: the copy's stream is sized like ours, then its fresh bytes (uninitialised in the original,
// zeros here) are read into ours instead of ours being written into it.
PartyMemberMessage::PartyMemberMessage(const PartyMemberMessage& other) : Message(other) {
    stream.SetSize(other.stream.size);
    ByteStream* copy = &stream;
    copy->Read(const_cast<std::uint8_t*>(other.stream.data.data()), stream.size);
}

Message* PartyMemberMessage::Create() { return NewCounted<PartyMemberMessage>(); }
PartyMemberMessage* PartyMemberMessage::Clone() {
    return NewCounted<PartyMemberMessage>(*this);
}
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
PartyMessage::PartyMessage(const PartyMessage& other)
    : Message(other), members(other.members), money(other.money), accepted(other.accepted),
      acceptedRevision(other.acceptedRevision), revision(other.revision), trading(other.trading) {
    player.Assign(other.player);
    partner.Assign(other.partner);
}
PartyMessage::~PartyMessage() {}

Message* PartyMessage::Create() { return NewCounted<PartyMessage>(); }
PartyMessage* PartyMessage::Clone() { return NewCounted<PartyMessage>(*this); }
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
    for (std::int32_t i = 0; i < count; ++i) members[i].Assign(g_currentConnection->ReadObjectRef(cursor));
    Take(cursor, money);
    Take(cursor, accepted);
    Take(cursor, acceptedRevision);
    Take(cursor, revision);
    TakeObject(cursor, player);
    TakeObject(cursor, partner);
}

// ServerEntryBase and ServerEntry (net_classes.h): made by the server list code.

ServerEntryBase::ServerEntryBase(const ServerEntryBase& other) : RefObject(), address(other.address) {
    x87::CopyBits(timeout, other.timeout);     // the floats copied bit for bit
    x87::CopyBits(ping, other.ping);
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
void ServerEntry::OnShutdown() {
    Rebuild(this);
}
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
