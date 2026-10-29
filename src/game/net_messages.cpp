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

// Counted pointers as the classes inline them.
void CopyCounted(RefPtr& to, const RefPtr& from) {
    to.ptr = from.ptr;
    if (to.ptr) to.ptr->AddRef();
}
void ReleaseCounted(RefPtr& ref) {
    CRefObject* object = ref.ptr;
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) delete object;
}
void AssignCounted(RefPtr& ref, CRefObject* object) {
    CRefObject* previous = ref.ptr;
    ref.ptr = object;
    if (object) object->AddRef();
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) delete previous;
    }
}

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
void PutObject(BufferCursor* cursor, const RefPtr& ref) { g_currentConnection->WriteObjectRef(cursor, ref.ptr); }
void TakeObject(BufferCursor* cursor, RefPtr& ref) { AssignCounted(ref, g_currentConnection->ReadObjectRef(cursor)); }

} // namespace

// The message base.

Message::Message() {}

Message::~Message() {}

// LoginMessage.

LoginMessage::LoginMessage() {}
LoginMessage::LoginMessage(const LoginMessage& other)
    : Message(other), name_(other.name_), text_(other.text_), host_(other.host_), state_(other.state_),
      id_(other.id_) {
    CopyCounted(binding_, other.binding_);
}
LoginMessage::~LoginMessage() { ReleaseCounted(binding_); }

Message* LoginMessage::Create() { return NewCounted<LoginMessage>(); }
LoginMessage* LoginMessage::Clone() { return NewCounted<LoginMessage>(*this); }
int LoginMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void LoginMessage::Write(BufferCursor* cursor) {
    PutString(cursor, name_);
    PutString(cursor, text_);
    Put(cursor, state_);
    Put(cursor, host_);
    Put(cursor, id_);
}
void LoginMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, name_);
    TakeString(cursor, text_);
    Take(cursor, state_);
    Take(cursor, host_);
    Take(cursor, id_);
}

// TextMessage: SayMessage, ZoneMessage, MapMessage.

TextMessage::TextMessage() {}
TextMessage::TextMessage(const TextMessage& other) : Message(other), text_(other.text_) {}
TextMessage::~TextMessage() {}
void TextMessage::WriteText(BufferCursor* cursor) { PutString(cursor, text_); }
void TextMessage::ReadText(BufferCursor* cursor) { TakeString(cursor, text_); }

SayMessage::SayMessage() {}
Message* SayMessage::Create() { return NewCounted<SayMessage>(); }
SayMessage* SayMessage::Clone() { return NewCounted<SayMessage>(*this); }
int SayMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void SayMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void SayMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

ZoneMessage::ZoneMessage() {}
Message* ZoneMessage::Create() { return NewCounted<ZoneMessage>(); }
ZoneMessage* ZoneMessage::Clone() { return NewCounted<ZoneMessage>(*this); }
int ZoneMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void ZoneMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void ZoneMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

Message* MapMessage::Create() { return NewCounted<MapMessage>(); }
MapMessage* MapMessage::Clone() { return NewCounted<MapMessage>(*this); }
int MapMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void MapMessage::Write(BufferCursor* cursor) { WriteText(cursor); }
void MapMessage::Read(BufferCursor* cursor) { ReadText(cursor); }

// CommandMessage.

CommandMessage::CommandMessage() {}
CommandMessage::CommandMessage(const CommandMessage& other) : Message(other), kind_(other.kind_), text_(other.text_) {}

Message* CommandMessage::Create() { return NewCounted<CommandMessage>(); }
CommandMessage* CommandMessage::Clone() { return NewCounted<CommandMessage>(*this); }
int CommandMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void CommandMessage::Write(BufferCursor* cursor) {
    Put(cursor, kind_);
    PutString(cursor, text_);
}
void CommandMessage::Read(BufferCursor* cursor) {
    Take(cursor, kind_);
    TakeString(cursor, text_);
}

// PairMessage: BindingMessage, FileRequestMessage.

PairMessage::PairMessage(const PairMessage& other) : Message(other) {
    CopyCounted(first_, other.first_);
    CopyCounted(second_, other.second_);
}
PairMessage::~PairMessage() {
    ReleaseCounted(second_);
    ReleaseCounted(first_);
}
void PairMessage::WriteFirst(BufferCursor* cursor) { PutObject(cursor, first_); }
void PairMessage::ReadFirst(BufferCursor* cursor) { TakeObject(cursor, first_); }

BindingMessage::BindingMessage() {}
Message* BindingMessage::Create() { return NewCounted<BindingMessage>(); }
BindingMessage* BindingMessage::Clone() { return NewCounted<BindingMessage>(*this); }
int BindingMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void BindingMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }
void BindingMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }

FileRequestMessage::FileRequestMessage() {}
Message* FileRequestMessage::Create() { return NewCounted<FileRequestMessage>(); }
FileRequestMessage* FileRequestMessage::Clone() { return NewCounted<FileRequestMessage>(*this); }
int FileRequestMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void FileRequestMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }
void FileRequestMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }

// PlayerMessage.

PlayerMessage::PlayerMessage() {}
PlayerMessage::PlayerMessage(const PlayerMessage& other)
    : Message(other), name_(other.name_), text_(other.text_), state_(other.state_), host_(other.host_),
      id_(other.id_), serial_(other.serial_), number_(other.number_), firstLine_(other.firstLine_) {}

Message* PlayerMessage::Create() { return NewCounted<PlayerMessage>(); }
PlayerMessage* PlayerMessage::Clone() { return NewCounted<PlayerMessage>(*this); }
int PlayerMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void PlayerMessage::Write(BufferCursor* cursor) {
    PutString(cursor, name_);
    PutString(cursor, text_);
    Put(cursor, state_);
    Put(cursor, host_);
    Put(cursor, id_);
    Put(cursor, serial_);
}
void PlayerMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, name_);
    TakeString(cursor, text_);
    Take(cursor, state_);
    Take(cursor, host_);
    Take(cursor, id_);
    Take(cursor, serial_);
}

// LineMessage.

LineMessage::LineMessage() {}
LineMessage::LineMessage(const LineMessage& other) : Message(other), number_(other.number_), text_(other.text_) {
    CopyCounted(connection_, other.connection_);
}
LineMessage::~LineMessage() { ReleaseCounted(connection_); }

Message* LineMessage::Create() { return NewCounted<LineMessage>(); }
LineMessage* LineMessage::Clone() { return NewCounted<LineMessage>(*this); }
int LineMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void LineMessage::Write(BufferCursor* cursor) {
    Put(cursor, number_);
    PutString(cursor, text_);
}
void LineMessage::Read(BufferCursor* cursor) {
    Take(cursor, number_);
    TakeString(cursor, text_);
}

// HostMessage.

HostMessage::HostMessage() {}
HostMessage::HostMessage(const HostMessage& other) : Message(other), address_(other.address_), zone_(other.zone_) {}

Message* HostMessage::Create() { return NewCounted<HostMessage>(); }
HostMessage* HostMessage::Clone() { return NewCounted<HostMessage>(*this); }
int HostMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void HostMessage::Write(BufferCursor* cursor) {
    PutString(cursor, address_);
    PutString(cursor, zone_);
}
void HostMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, address_);
    TakeString(cursor, zone_);
}

// VariableMessage.

VariableMessage::VariableMessage() {}
VariableMessage::VariableMessage(const VariableMessage& other) : Message(other), name_(other.name_) {
    x87::CopyBits(value_, other.value_);
    serial_ = other.serial_;
}

Message* VariableMessage::Create() { return NewCounted<VariableMessage>(); }
VariableMessage* VariableMessage::Clone() { return NewCounted<VariableMessage>(*this); }
int VariableMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void VariableMessage::Write(BufferCursor* cursor) {
    PutString(cursor, name_);
    PutBytes(cursor, &value_, 4);
    Put(cursor, serial_);
}
void VariableMessage::Read(BufferCursor* cursor) {
    TakeString(cursor, name_);
    TakeBytes(cursor, &value_, 4);
    Take(cursor, serial_);
}

// FileMessage.

FileMessage::FileMessage() {}
FileMessage::FileMessage(const FileMessage& other)
    : Message(other), chunkCount_(other.chunkCount_), size_(other.size_), name_(other.name_), chunks_(other.chunks_) {}
FileMessage::~FileMessage() {}

Message* FileMessage::Create() { return NewCounted<FileMessage>(); }
FileMessage* FileMessage::Clone() { return NewCounted<FileMessage>(*this); }
int FileMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void FileMessage::Write(BufferCursor* cursor) {
    Put(cursor, chunkCount_);
    Put(cursor, size_);
    PutString(cursor, name_);
}
void FileMessage::Read(BufferCursor* cursor) {
    Take(cursor, chunkCount_);
    Take(cursor, size_);
    TakeString(cursor, name_);
}

// FileChunkMessage.

FileChunkMessage::FileChunkMessage(const FileChunkMessage& other) : Message(other) {
    CopyCounted(file_, other.file_);
    index_ = other.index_;
    std::memcpy(data_, other.data_, kDataSize);
}
FileChunkMessage::~FileChunkMessage() { ReleaseCounted(file_); }

Message* FileChunkMessage::Create() { return NewCounted<FileChunkMessage>(); }
FileChunkMessage* FileChunkMessage::Clone() { return NewCounted<FileChunkMessage>(*this); }
int FileChunkMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void FileChunkMessage::Write(BufferCursor* cursor) {
    PutObject(cursor, file_);
    Put(cursor, index_);
    PutBytes(cursor, data_, kDataSize);
}
void FileChunkMessage::Read(BufferCursor* cursor) {
    TakeObject(cursor, file_);
    Take(cursor, index_);
    TakeBytes(cursor, data_, kDataSize);
}

// AwardMessage.

AwardMessage::AwardMessage() {}
AwardMessage::AwardMessage(const AwardMessage& other) : Message(other), kind_(other.kind_), text_(other.text_) {
    CopyCounted(connection_, other.connection_);
    x87::CopyBits(value_, other.value_);
}
// The original frees the text before it releases the receiver; ours after (the text is a member).
AwardMessage::~AwardMessage() { ReleaseCounted(connection_); }

Message* AwardMessage::Create() { return NewCounted<AwardMessage>(); }
AwardMessage* AwardMessage::Clone() { return NewCounted<AwardMessage>(*this); }
int AwardMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void AwardMessage::Write(BufferCursor* cursor) {
    Put(cursor, kind_);
    PutBytes(cursor, &value_, 4);
    PutString(cursor, text_);
}
void AwardMessage::Read(BufferCursor* cursor) {
    Take(cursor, kind_);
    TakeBytes(cursor, &value_, 4);
    TakeString(cursor, text_);
}

// TradeMessage.

TradeMessage::TradeMessage(const TradeMessage& other)
    : Message(other), done_(other.done_), revision_(other.revision_), partnerRevision_(other.partnerRevision_) {
    CopyCounted(player_, other.player_);
    CopyCounted(partner_, other.partner_);
    CopyCounted(offer_, other.offer_);
    CopyCounted(partnerOffer_, other.partnerOffer_);
}
TradeMessage::~TradeMessage() {
    ReleaseCounted(partnerOffer_);
    ReleaseCounted(offer_);
    ReleaseCounted(partner_);
    ReleaseCounted(player_);
}

Message* TradeMessage::Create() { return NewCounted<TradeMessage>(); }
TradeMessage* TradeMessage::Clone() { return NewCounted<TradeMessage>(*this); }
int TradeMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void TradeMessage::Write(BufferCursor* cursor) {
    PutObject(cursor, offer_);
    PutObject(cursor, partnerOffer_);
    Put(cursor, done_);
    Put(cursor, revision_);
    Put(cursor, partnerRevision_);
}
void TradeMessage::Read(BufferCursor* cursor) {
    TakeObject(cursor, offer_);
    TakeObject(cursor, partnerOffer_);
    Take(cursor, done_);
    Take(cursor, revision_);
    Take(cursor, partnerRevision_);
}

// PartyMemberMessage.

// Original bug kept: the copy's stream is sized like ours, then its (uninitialised) bytes are
// read into ours instead of ours being written into it.
PartyMemberMessage::PartyMemberMessage(const PartyMemberMessage& other) : Message(other) {
    stream_.SetSize(other.stream_.size_);
    CStream* copy = &stream_;
    copy->Read(other.stream_.data_, stream_.size_);
}

Message* PartyMemberMessage::Create() { return NewCounted<PartyMemberMessage>(); }
PartyMemberMessage* PartyMemberMessage::Clone() {
    return NewCounted<PartyMemberMessage>(*this);
}
int PartyMemberMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void PartyMemberMessage::Write(BufferCursor* cursor) {
    CStream* stream = &stream_;
    std::uint32_t size = stream->Size();
    Put(cursor, static_cast<std::uint16_t>(size));
    PutBytes(cursor, stream_.data_, size & 0xFFFF);
}
void PartyMemberMessage::Read(BufferCursor* cursor) {
    std::uint16_t n;
    Take(cursor, n);
    stream_.Resize(n);
    TakeBytes(cursor, stream_.data_, n);
}

// PartyMessage.

PartyMessage::PartyMessage() {}
PartyMessage::PartyMessage(const PartyMessage& other)
    : Message(other), members_(other.members_), money_(other.money_), accepted_(other.accepted_),
      acceptedRevision_(other.acceptedRevision_), revision_(other.revision_), trading_(other.trading_) {
    CopyCounted(player_, other.player_);
    CopyCounted(partner_, other.partner_);
}
PartyMessage::~PartyMessage() {
    ReleaseCounted(partner_);
    ReleaseCounted(player_);
}

Message* PartyMessage::Create() { return NewCounted<PartyMessage>(); }
PartyMessage* PartyMessage::Clone() { return NewCounted<PartyMessage>(*this); }
int PartyMessage::OnShutdown() {
    Rebuild(this);
    return 0;
}
void PartyMessage::Write(BufferCursor* cursor) {
    std::int32_t count = static_cast<std::int32_t>(members_.size());
    Put(cursor, count);
    for (std::int32_t i = 0; i < count; ++i) PutObject(cursor, members_[i]);
    Put(cursor, money_);
    Put(cursor, accepted_);
    Put(cursor, acceptedRevision_);
    Put(cursor, revision_);
    PutObject(cursor, player_);
    PutObject(cursor, partner_);
}
void PartyMessage::Read(BufferCursor* cursor) {
    std::int32_t count;
    Take(cursor, count);
    members_.resize(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) AssignCounted(members_[i], g_currentConnection->ReadObjectRef(cursor));
    Take(cursor, money_);
    Take(cursor, accepted_);
    Take(cursor, acceptedRevision_);
    Take(cursor, revision_);
    TakeObject(cursor, player_);
    TakeObject(cursor, partner_);
}

// ServerEntryBase and ServerEntry (net_classes.h): made by the server list code.

ServerEntryBase::ServerEntryBase(const ServerEntryBase& other) : CRefObject(), address_(other.address_) {
    x87::CopyBits(timeout_, other.timeout_);   // the floats copied bit for bit
    x87::CopyBits(ping_, other.ping_);
    foreign_ = other.foreign_;
    state_ = other.state_;
}
ServerEntryBase::~ServerEntryBase() {}

ServerEntry::ServerEntry() {
    AssignEmptyText(name_);
    kind_ = 4;
    AssignEmptyText(description_);
    players_ = 0;
    maxPlayers_ = 0;
    password_ = 0;
}
ServerEntry::ServerEntry(const ServerEntry& other)
    : ServerEntryBase(other), name_(other.name_), kind_(other.kind_), description_(other.description_),
      players_(other.players_), maxPlayers_(other.maxPlayers_), password_(other.password_) {}

ServerEntry* ServerEntry::Clone() { return NewCounted<ServerEntry>(*this); }
int ServerEntry::OnShutdown() {
    Rebuild(this);
    return 0;
}
void ServerEntry::Write(BufferCursor* cursor) {
    PutString(cursor, name_);
    Put(cursor, kind_);
    PutString(cursor, description_);
    Put(cursor, players_);
    Put(cursor, maxPlayers_);
    Put(cursor, password_);
}
void ServerEntry::Read(BufferCursor* cursor) {
    TakeString(cursor, name_);
    Take(cursor, kind_);
    TakeString(cursor, description_);
    Take(cursor, players_);
    Take(cursor, maxPlayers_);
    Take(cursor, password_);
}

} // namespace net
