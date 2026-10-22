// The network message classes (net_messages.h) and the message base: for each class its factory,
// constructor, copy (Clone's), slots 2-5 (reset, clone, write, read) and destructor; the same for the lobby's
// server entry (0073BB28, net_classes.h; no factory). See docs/notes/network.md and
// docs/notes/cleanup-network.md.
//
// The original inlines several STLport copies of the same string and vector code (copy
// construction from [first, last) into a fresh block of length + 1 bytes, the 8-byte empty string,
// assignment from a C string); they are reproduced here through one helper each, with the same
// allocations in the same order. The reset slots end with the inlined placement new's null test
// of `this`, which cannot fail here and is left out.
#include "net_messages.h"
#include "game_lists.h"
#include "net_connection.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/stlp_vector.h"
#include "lib/stlp_vector_refs.h"

#include <cstring>

namespace net {

namespace {

// 00783D6C: the empty text the server entry's reset assigns.
const char* EmptyText() { return "?"; }

// Counted pointers as the classes inline them.
void CopyCounted(RefPtr& to, const RefPtr& from) {
    to.ptr = from.ptr;
    if (to.ptr) ++to.ptr->refs_;
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
    if (object) ++object->refs_;
    if (previous) {
        --previous->refs_;
        if (previous->refs_ == 0) delete previous;
    }
}

// An empty string with an 8-byte block (the member's own constructor leaves it unset).
void StringEmpty(stlp::string& s) { stlp::string::Reserve(s, 8); }
void AssignEmptyText(stlp::string& s) {
    const char* text = EmptyText();
    s.Assign(text, text + std::strlen(text));
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
void PutString(BufferCursor* cursor, const stlp::string& s) { PutBytes(cursor, s.data(), std::strlen(s.data()) + 1); }
void TakeString(BufferCursor* cursor, stlp::string& s) {
    const char* text = reinterpret_cast<const char*>(cursor->p);
    std::size_t length = std::strlen(text);
    s.Assign(text, text + length);
    cursor->p += length + 1;
}
// Object references through the connection being written or read.
void PutObject(BufferCursor* cursor, const RefPtr& ref) { g_currentConnection->WriteObjectRef(cursor, ref.ptr); }
void TakeObject(BufferCursor* cursor, RefPtr& ref) { AssignCounted(ref, g_currentConnection->ReadObjectRef(cursor)); }

} // namespace

// ================================================================================================
// The message base.

void* __fastcall NewRefObject(std::size_t size) {   // 00413250
    void* object = gamecrt::operator_new(size);
    static_cast<std::uint32_t*>(object)[1] = 0;
    return object;
}
EI_REWRITTEN(0x00413250, NewRefObject);

Message::Message() {}
EI_REWRITTEN_SPECIAL(0x004186E0, Message::Message);

Message::~Message() {}
EI_REWRITTEN_SPECIAL(0x00413430, Message::~Message);
EI_REWRITTEN_SPECIAL(0x00413410, Message::~Message);

// ================================================================================================
// 0073BBF8 LoginMessage.

LoginMessage::LoginMessage() {
    StringEmpty(name_);
    StringEmpty(text_);
}
EI_REWRITTEN_SPECIAL(0x00413350, LoginMessage::LoginMessage);
LoginMessage::LoginMessage(const LoginMessage& other)
    : Message(other), name_(other.name_), text_(other.text_), host_(other.host_), state_(other.state_),
      id_(other.id_) {
    CopyCounted(binding_, other.binding_);
}
LoginMessage::~LoginMessage() { ReleaseCounted(binding_); }
EI_REWRITTEN_SPECIAL(0x00413800, LoginMessage::~LoginMessage);
EI_REWRITTEN_SPECIAL(0x004137E0, LoginMessage::~LoginMessage);

Message* __fastcall LoginMessage::Create() { return NewCounted<LoginMessage>(); }   // 004170A0
LoginMessage* LoginMessage::Clone() { return NewCounted<LoginMessage>(*this); }    // 00413440
int LoginMessage::OnShutdown() {   // 00413570
    Rebuild(this);
    return 0;
}
void LoginMessage::Write(BufferCursor* cursor) {   // 004136B0
    PutString(cursor, name_);
    PutString(cursor, text_);
    Put(cursor, state_);
    Put(cursor, host_);
    Put(cursor, id_);
}
void LoginMessage::Read(BufferCursor* cursor) {   // 00413750
    TakeString(cursor, name_);
    TakeString(cursor, text_);
    Take(cursor, state_);
    Take(cursor, host_);
    Take(cursor, id_);
}

// ================================================================================================
// TextMessage: SayMessage 0073BC28, ZoneMessage 0073BCD0, MapMessage 0073BD00.

TextMessage::TextMessage() { StringEmpty(text_); }
TextMessage::TextMessage(const TextMessage& other) : Message(other), text_(other.text_) {}
TextMessage::~TextMessage() {}
void TextMessage::WriteText(BufferCursor* cursor) { PutString(cursor, text_); }
void TextMessage::ReadText(BufferCursor* cursor) { TakeString(cursor, text_); }

SayMessage::SayMessage() {}
EI_REWRITTEN_SPECIAL(0x004138A0, SayMessage::SayMessage);
EI_REWRITTEN_SPECIAL(0x00413C40, SayMessage::~SayMessage);
EI_REWRITTEN_SPECIAL(0x00413C20, SayMessage::~SayMessage);
Message* __fastcall SayMessage::Create() { return NewCounted<SayMessage>(); }   // 004171A0
SayMessage* SayMessage::Clone() { return NewCounted<SayMessage>(*this); }      // 00413930
int SayMessage::OnShutdown() {   // 00413A50
    Rebuild(this);
    return 0;
}
void SayMessage::Write(BufferCursor* cursor) { WriteText(cursor); }   // 00413B10
void SayMessage::Read(BufferCursor* cursor) { ReadText(cursor); }     // 00413B50

ZoneMessage::ZoneMessage() {}
EI_REWRITTEN_SPECIAL(0x00418C30, ZoneMessage::ZoneMessage);
EI_REWRITTEN_SPECIAL(0x00415930, ZoneMessage::~ZoneMessage);
EI_REWRITTEN_SPECIAL(0x00415910, ZoneMessage::~ZoneMessage);
Message* __fastcall ZoneMessage::Create() { return NewCounted<ZoneMessage>(); }   // 00415570
ZoneMessage* ZoneMessage::Clone() { return NewCounted<ZoneMessage>(*this); }     // 00415620
int ZoneMessage::OnShutdown() {   // 00415740
    Rebuild(this);
    return 0;
}
void ZoneMessage::Write(BufferCursor* cursor) { WriteText(cursor); }   // 00415800
void ZoneMessage::Read(BufferCursor* cursor) { ReadText(cursor); }     // 00415840

EI_REWRITTEN_SPECIAL(0x00416110, MapMessage::~MapMessage);
EI_REWRITTEN_SPECIAL(0x004160F0, MapMessage::~MapMessage);
Message* __fastcall MapMessage::Create() { return NewCounted<MapMessage>(); }   // 00415D50
MapMessage* MapMessage::Clone() { return NewCounted<MapMessage>(*this); }      // 00415E00
int MapMessage::OnShutdown() {   // 00415F20
    Rebuild(this);
    return 0;
}
void MapMessage::Write(BufferCursor* cursor) { WriteText(cursor); }   // 00415FE0
void MapMessage::Read(BufferCursor* cursor) { ReadText(cursor); }     // 00416020

// ================================================================================================
// 0073BC40 CommandMessage.

CommandMessage::CommandMessage() { StringEmpty(text_); }
EI_REWRITTEN_SPECIAL(0x00413C90, CommandMessage::CommandMessage);
CommandMessage::CommandMessage(const CommandMessage& other) : Message(other), kind_(other.kind_), text_(other.text_) {}
EI_REWRITTEN_SPECIAL(0x00414060, CommandMessage::~CommandMessage);
EI_REWRITTEN_SPECIAL(0x00414040, CommandMessage::~CommandMessage);

Message* __fastcall CommandMessage::Create() { return NewCounted<CommandMessage>(); }   // 00417250
CommandMessage* CommandMessage::Clone() { return NewCounted<CommandMessage>(*this); }  // 00413D20
int CommandMessage::OnShutdown() {   // 00413E40
    Rebuild(this);
    return 0;
}
void CommandMessage::Write(BufferCursor* cursor) {   // 00413F00
    Put(cursor, kind_);
    PutString(cursor, text_);
}
void CommandMessage::Read(BufferCursor* cursor) {   // 00413F50
    Take(cursor, kind_);
    TakeString(cursor, text_);
}

// ================================================================================================
// PairMessage: BindingMessage 0073BCB8, FileRequestMessage 0073BC58.

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
EI_REWRITTEN_SPECIAL(0x0041C870, BindingMessage::BindingMessage);
EI_REWRITTEN_SPECIAL(0x004154D0, BindingMessage::~BindingMessage);
EI_REWRITTEN_SPECIAL(0x004154B0, BindingMessage::~BindingMessage);
Message* __fastcall BindingMessage::Create() { return NewCounted<BindingMessage>(); }   // 00415320
BindingMessage* BindingMessage::Clone() { return NewCounted<BindingMessage>(*this); }  // 00415350
int BindingMessage::OnShutdown() {   // 004153B0
    Rebuild(this);
    return 0;
}
void BindingMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }   // 00415450
void BindingMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }     // 00415470

FileRequestMessage::FileRequestMessage() {}
EI_REWRITTEN_SPECIAL(0x004140B0, FileRequestMessage::FileRequestMessage);
EI_REWRITTEN_SPECIAL(0x00414270, FileRequestMessage::~FileRequestMessage);
EI_REWRITTEN_SPECIAL(0x00414250, FileRequestMessage::~FileRequestMessage);
Message* __fastcall FileRequestMessage::Create() { return NewCounted<FileRequestMessage>(); }   // 004179E0
FileRequestMessage* FileRequestMessage::Clone() { return NewCounted<FileRequestMessage>(*this); }  // 004140D0
int FileRequestMessage::OnShutdown() {   // 00414130
    Rebuild(this);
    return 0;
}
void FileRequestMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }   // 004141F0
void FileRequestMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }     // 00414210

// ================================================================================================
// 0073BC70 PlayerMessage.

PlayerMessage::PlayerMessage() {
    StringEmpty(name_);
    StringEmpty(text_);
}
EI_REWRITTEN_SPECIAL(0x0041C680, PlayerMessage::PlayerMessage);
PlayerMessage::PlayerMessage(const PlayerMessage& other)
    : Message(other), name_(other.name_), text_(other.text_), state_(other.state_), host_(other.host_),
      id_(other.id_), serial_(other.serial_), number_(other.number_), firstLine_(other.firstLine_) {}
EI_REWRITTEN_SPECIAL(0x004148E0, PlayerMessage::~PlayerMessage);
EI_REWRITTEN_SPECIAL(0x004148C0, PlayerMessage::~PlayerMessage);

Message* __fastcall PlayerMessage::Create() { return NewCounted<PlayerMessage>(); }   // 00414420
PlayerMessage* PlayerMessage::Clone() { return NewCounted<PlayerMessage>(*this); }   // 00414500
int PlayerMessage::OnShutdown() {   // 00414650
    Rebuild(this);
    return 0;
}
void PlayerMessage::Write(BufferCursor* cursor) {   // 00414770
    PutString(cursor, name_);
    PutString(cursor, text_);
    Put(cursor, state_);
    Put(cursor, host_);
    Put(cursor, id_);
    Put(cursor, serial_);
}
void PlayerMessage::Read(BufferCursor* cursor) {   // 00414820
    TakeString(cursor, name_);
    TakeString(cursor, text_);
    Take(cursor, state_);
    Take(cursor, host_);
    Take(cursor, id_);
    Take(cursor, serial_);
}

// ================================================================================================
// 0073BC88 LineMessage.

LineMessage::LineMessage() { StringEmpty(text_); }
EI_REWRITTEN_SPECIAL(0x0041C740, LineMessage::LineMessage);
LineMessage::LineMessage(const LineMessage& other) : Message(other), number_(other.number_), text_(other.text_) {
    CopyCounted(connection_, other.connection_);
}
LineMessage::~LineMessage() { ReleaseCounted(connection_); }
EI_REWRITTEN_SPECIAL(0x00414D10, LineMessage::~LineMessage);
EI_REWRITTEN_SPECIAL(0x00414CF0, LineMessage::~LineMessage);

Message* __fastcall LineMessage::Create() { return NewCounted<LineMessage>(); }   // 00414960
LineMessage* LineMessage::Clone() { return NewCounted<LineMessage>(*this); }     // 00414A00
int LineMessage::OnShutdown() {   // 00414AC0
    Rebuild(this);
    return 0;
}
void LineMessage::Write(BufferCursor* cursor) {   // 00414BB0
    Put(cursor, number_);
    PutString(cursor, text_);
}
void LineMessage::Read(BufferCursor* cursor) {   // 00414C00
    Take(cursor, number_);
    TakeString(cursor, text_);
}

// ================================================================================================
// 0073BCA0 HostMessage.

HostMessage::HostMessage() {
    StringEmpty(address_);
    StringEmpty(zone_);
}
EI_REWRITTEN_SPECIAL(0x0041C7C0, HostMessage::HostMessage);
HostMessage::HostMessage(const HostMessage& other) : Message(other), address_(other.address_), zone_(other.zone_) {}
EI_REWRITTEN_SPECIAL(0x004152A0, HostMessage::~HostMessage);
EI_REWRITTEN_SPECIAL(0x00415280, HostMessage::~HostMessage);

Message* __fastcall HostMessage::Create() { return NewCounted<HostMessage>(); }   // 00414DB0
HostMessage* HostMessage::Clone() { return NewCounted<HostMessage>(*this); }     // 00414E80
int HostMessage::OnShutdown() {   // 00414FA0
    Rebuild(this);
    return 0;
}
void HostMessage::Write(BufferCursor* cursor) {   // 004150B0
    PutString(cursor, address_);
    PutString(cursor, zone_);
}
void HostMessage::Read(BufferCursor* cursor) {   // 00415120
    TakeString(cursor, address_);
    TakeString(cursor, zone_);
}

// ================================================================================================
// 0073BCE8 VariableMessage.

VariableMessage::VariableMessage() { StringEmpty(name_); }
VariableMessage::VariableMessage(const VariableMessage& other) : Message(other), name_(other.name_) {
    std::memcpy(&value_, &other.value_, 4);
    serial_ = other.serial_;
}
EI_REWRITTEN_SPECIAL(0x00415D00, VariableMessage::~VariableMessage);
EI_REWRITTEN_SPECIAL(0x00415CE0, VariableMessage::~VariableMessage);

Message* __fastcall VariableMessage::Create() { return NewCounted<VariableMessage>(); }   // 00415980
VariableMessage* VariableMessage::Clone() { return NewCounted<VariableMessage>(*this); }  // 00415A30
int VariableMessage::OnShutdown() {   // 00415B60
    Rebuild(this);
    return 0;
}
void VariableMessage::Write(BufferCursor* cursor) {   // 00415C20
    PutString(cursor, name_);
    PutBytes(cursor, &value_, 4);
    Put(cursor, serial_);
}
void VariableMessage::Read(BufferCursor* cursor) {   // 00415C80
    TakeString(cursor, name_);
    TakeBytes(cursor, &value_, 4);
    Take(cursor, serial_);
}

// ================================================================================================
// 0073BD18 FileMessage.

FileMessage::FileMessage() {
    StringEmpty(name_);
    chunks_.ConstructEmpty();
}
FileMessage::FileMessage(const FileMessage& other)
    : Message(other), chunkCount_(other.chunkCount_), size_(other.size_), name_(other.name_) {
    const RefVector& from = other.chunks_;
    std::size_t n = static_cast<std::size_t>(from.size());
    chunks_.ConstructEmpty();
    auto* block = stlp::allocate_as<RefPtr>(n);
    chunks_.Start() = block;
    chunks_.Finish() = block;
    chunks_.EndOfStorage() = block + n;
    chunks_.Finish() = stlp::SharedRefVector::UninitializedCopy(from.begin(), from.end(), block, nullptr);   // 0042AE90
}
FileMessage::~FileMessage() {
    for (RefPtr *p = chunks_.begin(), *end = chunks_.end(); p != end; ++p) p->Destroy();   // 0042B880 (flags 0)
    stlp::deallocate(chunks_.begin(), static_cast<std::size_t>(chunks_.EndOfStorage() - chunks_.begin()) * sizeof(RefPtr));
}
EI_REWRITTEN_SPECIAL(0x00416640, FileMessage::~FileMessage);
EI_REWRITTEN_SPECIAL(0x00416620, FileMessage::~FileMessage);

Message* __fastcall FileMessage::Create() { return NewCounted<FileMessage>(); }   // 00416160
FileMessage* FileMessage::Clone() { return NewCounted<FileMessage>(*this); }     // 00416220
int FileMessage::OnShutdown() {   // 00416390
    Rebuild(this);
    return 0;
}
void FileMessage::Write(BufferCursor* cursor) {   // 004164D0
    Put(cursor, chunkCount_);
    Put(cursor, size_);
    PutString(cursor, name_);
}
void FileMessage::Read(BufferCursor* cursor) {   // 00416530
    Take(cursor, chunkCount_);
    Take(cursor, size_);
    TakeString(cursor, name_);
}

// ================================================================================================
// 0073BD30 FileChunkMessage.

FileChunkMessage::FileChunkMessage(const FileChunkMessage& other) : Message(other) {
    CopyCounted(file_, other.file_);
    index_ = other.index_;
    std::memcpy(data_, other.data_, kDataSize);
}
FileChunkMessage::~FileChunkMessage() { ReleaseCounted(file_); }
EI_REWRITTEN_SPECIAL(0x004168E0, FileChunkMessage::~FileChunkMessage);
EI_REWRITTEN_SPECIAL(0x004168C0, FileChunkMessage::~FileChunkMessage);

Message* __fastcall FileChunkMessage::Create() { return NewCounted<FileChunkMessage>(); }   // 004166F0
FileChunkMessage* FileChunkMessage::Clone() { return NewCounted<FileChunkMessage>(*this); }  // 00416720
int FileChunkMessage::OnShutdown() {   // 00416780
    Rebuild(this);
    return 0;
}
void FileChunkMessage::Write(BufferCursor* cursor) {   // 00416800
    PutObject(cursor, file_);
    Put(cursor, index_);
    PutBytes(cursor, data_, kDataSize);
}
void FileChunkMessage::Read(BufferCursor* cursor) {   // 00416850
    TakeObject(cursor, file_);
    Take(cursor, index_);
    TakeBytes(cursor, data_, kDataSize);
}

// ================================================================================================
// 0073BD48 AwardMessage.

AwardMessage::AwardMessage() { StringEmpty(text_); }
AwardMessage::AwardMessage(const AwardMessage& other) : Message(other), kind_(other.kind_), text_(other.text_) {
    CopyCounted(connection_, other.connection_);
    std::memcpy(&value_, &other.value_, 4);
}
// The original frees the text before it releases the receiver; ours after (the text is a member).
AwardMessage::~AwardMessage() { ReleaseCounted(connection_); }
EI_REWRITTEN_SPECIAL(0x00416D70, AwardMessage::~AwardMessage);
EI_REWRITTEN_SPECIAL(0x00416D50, AwardMessage::~AwardMessage);

Message* __fastcall AwardMessage::Create() { return NewCounted<AwardMessage>(); }   // 00416940
AwardMessage* AwardMessage::Clone() { return NewCounted<AwardMessage>(*this); }    // 00416A10
int AwardMessage::OnShutdown() {   // 00416B30
    Rebuild(this);
    return 0;
}
void AwardMessage::Write(BufferCursor* cursor) {   // 00416C00
    Put(cursor, kind_);
    PutBytes(cursor, &value_, 4);
    PutString(cursor, text_);
}
void AwardMessage::Read(BufferCursor* cursor) {   // 00416C60
    Take(cursor, kind_);
    TakeBytes(cursor, &value_, 4);
    TakeString(cursor, text_);
}

// ================================================================================================
// 0073BD60 TradeMessage.

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
EI_REWRITTEN_SPECIAL(0x00416FC0, TradeMessage::~TradeMessage);
EI_REWRITTEN_SPECIAL(0x00416FA0, TradeMessage::~TradeMessage);

Message* __fastcall TradeMessage::Create() { return NewCounted<TradeMessage>(); }   // 00416DF0
TradeMessage* TradeMessage::Clone() { return NewCounted<TradeMessage>(*this); }    // 00416E30
int TradeMessage::OnShutdown() {   // 00416EB0
    Rebuild(this);
    return 0;
}
void TradeMessage::Write(BufferCursor* cursor) {   // 00412C80
    PutObject(cursor, offer_);
    PutObject(cursor, partnerOffer_);
    Put(cursor, done_);
    Put(cursor, revision_);
    Put(cursor, partnerRevision_);
}
void TradeMessage::Read(BufferCursor* cursor) {   // 00412CE0
    TakeObject(cursor, offer_);
    TakeObject(cursor, partnerOffer_);
    Take(cursor, done_);
    Take(cursor, revision_);
    Take(cursor, partnerRevision_);
}

// ================================================================================================
// 0073BD78 PartyMemberMessage.

// Original bug kept: the copy's stream is sized like ours, then its (uninitialised) bytes are
// read into ours (slot 1, Read) instead of ours being written into it.
PartyMemberMessage::PartyMemberMessage(const PartyMemberMessage& other) : Message(other) {
    stream_.SetSize(other.stream_.size_);
    CStream* copy = &stream_;
    copy->Read(other.stream_.data_, stream_.size_);
}
EI_REWRITTEN_SPECIAL(0x00417620, PartyMemberMessage::~PartyMemberMessage);
EI_REWRITTEN_SPECIAL(0x00417600, PartyMemberMessage::~PartyMemberMessage);

Message* __fastcall PartyMemberMessage::Create() { return NewCounted<PartyMemberMessage>(); }   // 00417300
PartyMemberMessage* PartyMemberMessage::Clone() {   // 00417420
    return NewCounted<PartyMemberMessage>(*this);
}
int PartyMemberMessage::OnShutdown() {   // 004174F0
    Rebuild(this);
    return 0;
}
void PartyMemberMessage::Write(BufferCursor* cursor) {   // 00417560
    CStream* stream = &stream_;
    std::uint32_t size = stream->Size();
    Put(cursor, static_cast<std::uint16_t>(size));
    PutBytes(cursor, stream_.data_, size & 0xFFFF);
}
void PartyMemberMessage::Read(BufferCursor* cursor) {   // 004175B0
    std::uint16_t n;
    Take(cursor, n);
    stream_.Resize(n);
    TakeBytes(cursor, stream_.data_, n);
}

// ================================================================================================
// 0073BDD0 PartyMessage.

PartyMessage::PartyMessage() { members_.ConstructEmpty(); }
PartyMessage::PartyMessage(const PartyMessage& other)
    : Message(other), money_(other.money_), accepted_(other.accepted_), acceptedRevision_(other.acceptedRevision_),
      revision_(other.revision_), trading_(other.trading_) {
    members_.CopyConstruct<stlp::CountedRefElement>(other.members_);
    CopyCounted(player_, other.player_);
    CopyCounted(partner_, other.partner_);
}
PartyMessage::~PartyMessage() {
    ReleaseCounted(partner_);
    ReleaseCounted(player_);
    for (RefPtr *p = members_.begin(), *end = members_.end(); p != end; ++p)
        if (p->ptr) p->ptr->Release();
    stlp::deallocate(members_.begin(), static_cast<std::size_t>(members_.EndOfStorage() - members_.begin()) * sizeof(RefPtr));
}
EI_REWRITTEN_SPECIAL(0x004178F0, PartyMessage::~PartyMessage);
EI_REWRITTEN_SPECIAL(0x004178D0, PartyMessage::~PartyMessage);

Message* __fastcall PartyMessage::Create() { return NewCounted<PartyMessage>(); }   // 00417650
PartyMessage* PartyMessage::Clone() { return NewCounted<PartyMessage>(*this); }    // 004176A0
int PartyMessage::OnShutdown() {   // 004177A0
    Rebuild(this);
    return 0;
}
void PartyMessage::Write(BufferCursor* cursor) {   // 00412AC0
    std::int32_t count = static_cast<std::int32_t>(members_.size());
    Put(cursor, count);
    for (std::int32_t i = 0; i < count; ++i) PutObject(cursor, members_.begin()[i]);
    Put(cursor, money_);
    Put(cursor, accepted_);
    Put(cursor, acceptedRevision_);
    Put(cursor, revision_);
    PutObject(cursor, player_);
    PutObject(cursor, partner_);
}
void PartyMessage::Read(BufferCursor* cursor) {   // 00412B70
    std::int32_t count;
    Take(cursor, count);
    static_cast<stlp::VectorAbi<RefPtr, stlp::CountedRefElement>*>(&members_)->ResizeDefault(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        RefPtr& slot = members_.begin()[i];
        AssignCounted(slot, g_currentConnection->ReadObjectRef(cursor));
    }
    Take(cursor, money_);
    Take(cursor, accepted_);
    Take(cursor, acceptedRevision_);
    Take(cursor, revision_);
    TakeObject(cursor, player_);
    TakeObject(cursor, partner_);
}

// ================================================================================================
// 0073BB40 ServerEntryBase and 0073BB28 ServerEntry (net_classes.h): made by 0041A7E0, 0041D420 and 0041D600 (the
// server list). Slot 1 is the empty 00412580.

ServerEntryBase::ServerEntryBase(const ServerEntryBase& other) : CRefObject(), address_(other.address_) {
    std::memcpy(&timeout_, &other.timeout_, 4);   // the floats copied as dwords
    std::memcpy(&ping_, &other.ping_, 4);
    foreign_ = other.foreign_;
    state_ = other.state_;
}
ServerEntryBase::~ServerEntryBase() {}
EI_REWRITTEN_SPECIAL(0x00412960, ServerEntryBase::~ServerEntryBase);
EI_REWRITTEN_SPECIAL(0x00412970, ServerEntryBase::~ServerEntryBase);

ServerEntry::ServerEntry() {
    StringEmpty(name_);
    StringEmpty(description_);
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
EI_REWRITTEN_SPECIAL(0x004128E0, ServerEntry::~ServerEntry);
EI_REWRITTEN_SPECIAL(0x004128C0, ServerEntry::~ServerEntry);

ServerEntry* ServerEntry::Clone() { return NewCounted<ServerEntry>(*this); }   // 00412590
int ServerEntry::OnShutdown() {   // 00412750
    Rebuild(this);
    return 0;
}
void ServerEntry::Write(BufferCursor* cursor) {   // 00412990
    PutString(cursor, name_);
    Put(cursor, kind_);
    PutString(cursor, description_);
    Put(cursor, players_);
    Put(cursor, maxPlayers_);
    Put(cursor, password_);
}
void ServerEntry::Read(BufferCursor* cursor) {   // 00412A30
    TakeString(cursor, name_);
    Take(cursor, kind_);
    TakeString(cursor, description_);
    Take(cursor, players_);
    Take(cursor, maxPlayers_);
    Take(cursor, password_);
}

// ================================================================================================
// The registrations: the factories, then each class's slots 2-5.

EI_REWRITTEN(0x004170A0, LoginMessage::Create);
EI_REWRITTEN(0x00413570, LoginMessage::OnShutdown);
EI_REWRITTEN(0x00413440, LoginMessage::Clone);
EI_REWRITTEN(0x004136B0, LoginMessage::Write);
EI_REWRITTEN(0x00413750, LoginMessage::Read);
EI_REWRITTEN(0x004171A0, SayMessage::Create);
EI_REWRITTEN(0x00413A50, SayMessage::OnShutdown);
EI_REWRITTEN(0x00413930, SayMessage::Clone);
EI_REWRITTEN(0x00413B10, SayMessage::Write);
EI_REWRITTEN(0x00413B50, SayMessage::Read);
EI_REWRITTEN(0x00417250, CommandMessage::Create);
EI_REWRITTEN(0x00413E40, CommandMessage::OnShutdown);
EI_REWRITTEN(0x00413D20, CommandMessage::Clone);
EI_REWRITTEN(0x00413F00, CommandMessage::Write);
EI_REWRITTEN(0x00413F50, CommandMessage::Read);
EI_REWRITTEN(0x004179E0, FileRequestMessage::Create);
EI_REWRITTEN(0x00414130, FileRequestMessage::OnShutdown);
EI_REWRITTEN(0x004140D0, FileRequestMessage::Clone);
EI_REWRITTEN(0x004141F0, FileRequestMessage::Write);
EI_REWRITTEN(0x00414210, FileRequestMessage::Read);
EI_REWRITTEN(0x00417650, PartyMessage::Create);
EI_REWRITTEN(0x004177A0, PartyMessage::OnShutdown);
EI_REWRITTEN(0x004176A0, PartyMessage::Clone);
EI_REWRITTEN(0x00412AC0, PartyMessage::Write);
EI_REWRITTEN(0x00412B70, PartyMessage::Read);
EI_REWRITTEN(0x00417300, PartyMemberMessage::Create);
EI_REWRITTEN(0x004174F0, PartyMemberMessage::OnShutdown);
EI_REWRITTEN(0x00417420, PartyMemberMessage::Clone);
EI_REWRITTEN(0x00417560, PartyMemberMessage::Write);
EI_REWRITTEN(0x004175B0, PartyMemberMessage::Read);
EI_REWRITTEN(0x00414420, PlayerMessage::Create);
EI_REWRITTEN(0x00414650, PlayerMessage::OnShutdown);
EI_REWRITTEN(0x00414500, PlayerMessage::Clone);
EI_REWRITTEN(0x00414770, PlayerMessage::Write);
EI_REWRITTEN(0x00414820, PlayerMessage::Read);
EI_REWRITTEN(0x00414960, LineMessage::Create);
EI_REWRITTEN(0x00414AC0, LineMessage::OnShutdown);
EI_REWRITTEN(0x00414A00, LineMessage::Clone);
EI_REWRITTEN(0x00414BB0, LineMessage::Write);
EI_REWRITTEN(0x00414C00, LineMessage::Read);
EI_REWRITTEN(0x00414DB0, HostMessage::Create);
EI_REWRITTEN(0x00414FA0, HostMessage::OnShutdown);
EI_REWRITTEN(0x00414E80, HostMessage::Clone);
EI_REWRITTEN(0x004150B0, HostMessage::Write);
EI_REWRITTEN(0x00415120, HostMessage::Read);
EI_REWRITTEN(0x00415320, BindingMessage::Create);
EI_REWRITTEN(0x004153B0, BindingMessage::OnShutdown);
EI_REWRITTEN(0x00415350, BindingMessage::Clone);
EI_REWRITTEN(0x00415450, BindingMessage::Write);
EI_REWRITTEN(0x00415470, BindingMessage::Read);
EI_REWRITTEN(0x00415570, ZoneMessage::Create);
EI_REWRITTEN(0x00415740, ZoneMessage::OnShutdown);
EI_REWRITTEN(0x00415620, ZoneMessage::Clone);
EI_REWRITTEN(0x00415800, ZoneMessage::Write);
EI_REWRITTEN(0x00415840, ZoneMessage::Read);
EI_REWRITTEN(0x00415D50, MapMessage::Create);
EI_REWRITTEN(0x00415F20, MapMessage::OnShutdown);
EI_REWRITTEN(0x00415E00, MapMessage::Clone);
EI_REWRITTEN(0x00415FE0, MapMessage::Write);
EI_REWRITTEN(0x00416020, MapMessage::Read);
EI_REWRITTEN(0x00415980, VariableMessage::Create);
EI_REWRITTEN(0x00415B60, VariableMessage::OnShutdown);
EI_REWRITTEN(0x00415A30, VariableMessage::Clone);
EI_REWRITTEN(0x00415C20, VariableMessage::Write);
EI_REWRITTEN(0x00415C80, VariableMessage::Read);
EI_REWRITTEN(0x00416160, FileMessage::Create);
EI_REWRITTEN(0x00416390, FileMessage::OnShutdown);
EI_REWRITTEN(0x00416220, FileMessage::Clone);
EI_REWRITTEN(0x004164D0, FileMessage::Write);
EI_REWRITTEN(0x00416530, FileMessage::Read);
EI_REWRITTEN(0x004166F0, FileChunkMessage::Create);
EI_REWRITTEN(0x00416780, FileChunkMessage::OnShutdown);
EI_REWRITTEN(0x00416720, FileChunkMessage::Clone);
EI_REWRITTEN(0x00416800, FileChunkMessage::Write);
EI_REWRITTEN(0x00416850, FileChunkMessage::Read);
EI_REWRITTEN(0x00416940, AwardMessage::Create);
EI_REWRITTEN(0x00416B30, AwardMessage::OnShutdown);
EI_REWRITTEN(0x00416A10, AwardMessage::Clone);
EI_REWRITTEN(0x00416C00, AwardMessage::Write);
EI_REWRITTEN(0x00416C60, AwardMessage::Read);
EI_REWRITTEN(0x00416DF0, TradeMessage::Create);
EI_REWRITTEN(0x00416EB0, TradeMessage::OnShutdown);
EI_REWRITTEN(0x00416E30, TradeMessage::Clone);
EI_REWRITTEN(0x00412C80, TradeMessage::Write);
EI_REWRITTEN(0x00412CE0, TradeMessage::Read);

EI_REWRITTEN(0x00412750, ServerEntry::OnShutdown);
EI_REWRITTEN(0x00412590, ServerEntry::Clone);
EI_REWRITTEN(0x00412990, ServerEntry::Write);
EI_REWRITTEN(0x00412A30, ServerEntry::Read);

} // namespace net
