// The network message classes (net_messages.h) and the message base helpers: for each class its
// factory, constructor body, slots 2-5 (reset, clone, write, read) and destructor body; the same for
// the lobby's server entry (0073BB28, net_classes.h; no factory). The scalar deleting destructors
// are in deleting_destructors_400.cpp. See docs/notes/unmapped_410_470.md ("Third round",
// "Eighth round") and docs/notes/cleanup-network.md.
//
// The original inlines several STLport copies of the same string and vector code (copy
// construction from [first, last) into a fresh block of length + 1 bytes, the 8-byte empty string,
// assignment from a C string); they are reproduced here through one helper each, with the same
// allocations in the same order. The reset slots end with the inlined placement new's null test
// of `this`, which cannot fail here and is left out.
#include "net_messages.h"
#include "net_connection.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "originals_core.h"
#include "originals_library.h"

#include <cstring>

namespace net {

namespace {

// --- STLport (library) ---------------------------------------------------------------------------
__declspec(naked) void __fastcall StringAllocateBlock(StringRep* /*s*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x00424E40)
void __fastcall StringAssignRange(StringRep* a0, void*, const char* a2, const char* a3) { StringAssignRangeOriginal((void*)a0, nullptr, a2, a3); }
// vector<RefPtr> of the file message: copy of the elements sharing them, deallocate.
__declspec(naked) RefPtr* __fastcall SharedRefsCopy(const RefPtr* /*first*/, const RefPtr* /*last*/, RefPtr* /*result*/, std::uint32_t /*tag*/) EI_ORIGINAL(0x0042AE90)
__declspec(naked) void __fastcall FileRefsDeallocate(RefPtr** /*eos proxy*/, void*, RefPtr* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x004251B0)
// vector<RefPtr> of the party message: base constructor, copy, deallocate, resize.
__declspec(naked) void* __fastcall RefsGetAllocator(const RefVector* /*vector*/, void*, void* /*out*/) EI_ORIGINAL(0x00421EB0)
__declspec(naked) void __fastcall RefsBaseConstruct(RefVector* /*vector*/, void*, std::size_t /*n*/, const void* /*alloc*/) EI_ORIGINAL(0x00421F60)
__declspec(naked) void __fastcall RefsDeallocate(RefPtr** /*eos proxy*/, void*, RefPtr* /*p*/, std::size_t /*n*/) EI_ORIGINAL(0x00425210)
__declspec(naked) void __fastcall RefsResize(RefVector* /*vector*/, void*, std::size_t /*n*/) EI_ORIGINAL(0x004200C0)
// Counted pointer element destructors (deleting destructors called with 0).
__declspec(naked) void* __fastcall FileRefDestroy(RefPtr* /*p*/, void*, unsigned /*flags*/) EI_ORIGINAL(0x0042B880)

// 00783D6C: the empty text the server entry's reset assigns.
const char* EmptyText() { return reinterpret_cast<const char*>(0x00783D6C); }

// The header of a clone (the base constructors inlined) and of a fresh or rebuilt message.
void CloneHeader(Message* copy, const Message* source) {
    ei::SetVtable(copy, kRefValueVtable);
    copy->version_ = source->version_;
    ei::SetVtable(copy, kMessageVtable);
}
void FreshHeader(Message* self) {
    ei::SetVtable(self, kMessageVtable);
    self->version_ = 1;
}

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

// Strings.
void StringCopy(StringRep& to, const StringRep& from) {
    to.start = nullptr;
    to.finish = nullptr;
    to.eos = nullptr;
    StringAssignSpan(&to, nullptr, from.start, from.finish);
}
// An empty string with an 8-byte block from _M_allocate_block.
void StringEmpty(StringRep& s) {
    s.start = nullptr;
    s.finish = nullptr;
    s.eos = nullptr;
    StringAllocateBlock(&s, nullptr, 8);
    *s.finish = 0;
}
// The same with the node allocator called directly (the constructors that inline it).
void StringEmptyInline(StringRep& s) {
    s.start = nullptr;
    s.finish = nullptr;
    s.eos = nullptr;
    char* block = static_cast<char*>(stlp::allocate(8));
    s.start = block;
    s.finish = block;
    s.eos = block + 8;
    *block = 0;
}
void StringFree(StringRep& s) { stlp::deallocate(s.start, static_cast<std::size_t>(s.eos - s.start)); }
void AssignEmptyText(StringRep& s) {
    const char* text = EmptyText();
    StringAssignRange(&s, nullptr, text, text + std::strlen(text));
}

// An empty vector of counted pointers.
void ClearRefs(RefVector& v) {
    v.start = nullptr;
    v.finish = nullptr;
    v.eos = nullptr;
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
void PutString(BufferCursor* cursor, const StringRep& s) { PutBytes(cursor, s.start, std::strlen(s.start) + 1); }
void TakeString(BufferCursor* cursor, StringRep& s) {
    const char* text = reinterpret_cast<const char*>(cursor->p);
    std::size_t length = std::strlen(text);
    StringAssignRange(&s, nullptr, text, text + length);
    cursor->p += length + 1;
}
// Object references through the connection being written or read.
void PutObject(BufferCursor* cursor, const RefPtr& ref) { CurrentConnection()->WriteObjectRef(cursor, ref.ptr); }
void TakeObject(BufferCursor* cursor, RefPtr& ref) { AssignCounted(ref, CurrentConnection()->ReadObjectRef(cursor)); }

} // namespace

// ================================================================================================
// The message base.

void* __fastcall NewRefObject(std::size_t size) {   // 00413250
    void* object = gamecrt::operator_new(size);
    static_cast<std::uint32_t*>(object)[1] = 0;
    return object;
}
EI_REPLACE(0x00413250, NewRefObject);

Message* __fastcall MessageConstructBase(Message* self) {   // 004186E0
    ei::SetVtable(self, kMessageVtable);
    self->version_ = 1;
    return self;
}
EI_REPLACE(0x004186E0, MessageConstructBase);

// ================================================================================================
// 0073BBF8 LoginMessage.

namespace {
// The fields after the base header (factory, reset).
void InitLogin(LoginMessage* m) {
    StringEmpty(m->name_);
    StringEmpty(m->text_);
    m->binding_.ptr = nullptr;
    ei::SetVtable(m, LoginMessage::kVtableAddress);
    m->host_ = 0;
    m->state_ = 4;
    m->id_ = -1;
}
void FreeLogin(LoginMessage* m) {
    ReleaseCounted(m->binding_);
    StringFree(m->text_);
    StringFree(m->name_);
}
} // namespace

Message* __fastcall LoginMessage::Create() {   // 004170A0
    auto* p = NewMessageMemory<LoginMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    InitLogin(p);
    return p;
}
LoginMessage* LoginMessage::Construct() {   // 00413350
    MessageConstructBase(this);
    StringEmpty(name_);
    StringEmpty(text_);
    binding_.ptr = nullptr;
    ei::SetVtable(this, kVtableAddress);
    host_ = 0;
    state_ = 4;
    id_ = -1;
    return this;
}
LoginMessage* LoginMessage::Clone() {   // 00413440
    auto* p = NewMessageMemory<LoginMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    StringCopy(p->name_, name_);
    StringCopy(p->text_, text_);
    p->host_ = host_;
    p->state_ = state_;
    p->id_ = id_;
    CopyCounted(p->binding_, binding_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int LoginMessage::OnShutdown() {   // 00413570
    ei::SetVtable(this, kVtableAddress);
    FreeLogin(this);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    InitLogin(this);
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
void LoginMessage::Destruct() {   // 00413800
    ei::SetVtable(this, kVtableAddress);
    FreeLogin(this);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// TextMessage: SayMessage 0073BC28, ZoneMessage 0073BCD0, MapMessage 0073BD00.

Message* TextMessage::CreateAs(std::uintptr_t vtable) {
    auto* p = NewMessageMemory<TextMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(p->text_);
    ei::SetVtable(p, vtable);
    return p;
}
TextMessage* TextMessage::CloneAs(std::uintptr_t vtable) {
    auto* p = NewMessageMemory<TextMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    StringCopy(p->text_, text_);
    ei::SetVtable(p, vtable);
    return p;
}
void TextMessage::ResetAs(std::uintptr_t vtable) {
    ei::SetVtable(this, vtable);
    StringFree(text_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    StringEmpty(text_);
    ei::SetVtable(this, vtable);
}
void TextMessage::WriteText(BufferCursor* cursor) { PutString(cursor, text_); }
void TextMessage::ReadText(BufferCursor* cursor) { TakeString(cursor, text_); }
void TextMessage::DestructAs(std::uintptr_t vtable) {
    ei::SetVtable(this, vtable);
    StringFree(text_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

Message* __fastcall SayMessage::Create() { return CreateAs(kVtableAddress); }   // 004171A0
SayMessage* SayMessage::Construct() {   // 004138A0
    MessageConstructBase(this);
    StringEmptyInline(text_);
    ei::SetVtable(this, kVtableAddress);
    return this;
}
SayMessage* SayMessage::Clone() { return static_cast<SayMessage*>(CloneAs(kVtableAddress)); }   // 00413930
int SayMessage::OnShutdown() {   // 00413A50
    ResetAs(kVtableAddress);
    return 0;
}
void SayMessage::Write(BufferCursor* cursor) { WriteText(cursor); }   // 00413B10
void SayMessage::Read(BufferCursor* cursor) { ReadText(cursor); }     // 00413B50
void SayMessage::Destruct() { DestructAs(kVtableAddress); }           // 00413C40

Message* __fastcall ZoneMessage::Create() { return CreateAs(kVtableAddress); }   // 00415570
ZoneMessage* ZoneMessage::Construct() {   // 00418C30
    MessageConstructBase(this);
    StringEmptyInline(text_);
    ei::SetVtable(this, kVtableAddress);
    return this;
}
ZoneMessage* ZoneMessage::Clone() { return static_cast<ZoneMessage*>(CloneAs(kVtableAddress)); }   // 00415620
int ZoneMessage::OnShutdown() {   // 00415740
    ResetAs(kVtableAddress);
    return 0;
}
void ZoneMessage::Write(BufferCursor* cursor) { WriteText(cursor); }   // 00415800
void ZoneMessage::Read(BufferCursor* cursor) { ReadText(cursor); }     // 00415840
void ZoneMessage::Destruct() { DestructAs(kVtableAddress); }           // 00415930

Message* __fastcall MapMessage::Create() { return CreateAs(kVtableAddress); }   // 00415D50
MapMessage* MapMessage::Clone() { return static_cast<MapMessage*>(CloneAs(kVtableAddress)); }   // 00415E00
int MapMessage::OnShutdown() {   // 00415F20
    ResetAs(kVtableAddress);
    return 0;
}
void MapMessage::Write(BufferCursor* cursor) { WriteText(cursor); }   // 00415FE0
void MapMessage::Read(BufferCursor* cursor) { ReadText(cursor); }     // 00416020
void MapMessage::Destruct() { DestructAs(kVtableAddress); }           // 00416110

// ================================================================================================
// 0073BC40 CommandMessage.

Message* __fastcall CommandMessage::Create() {   // 00417250
    auto* p = NewMessageMemory<CommandMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(p->text_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
CommandMessage* CommandMessage::Construct() {   // 00413C90
    MessageConstructBase(this);
    StringEmptyInline(text_);
    ei::SetVtable(this, kVtableAddress);
    return this;
}
CommandMessage* CommandMessage::Clone() {   // 00413D20
    auto* p = NewMessageMemory<CommandMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    p->kind_ = kind_;
    StringCopy(p->text_, text_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int CommandMessage::OnShutdown() {   // 00413E40
    ei::SetVtable(this, kVtableAddress);
    StringFree(text_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    StringEmpty(text_);
    ei::SetVtable(this, kVtableAddress);
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
void CommandMessage::Destruct() {   // 00414060
    ei::SetVtable(this, kVtableAddress);
    StringFree(text_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// PairMessage: BindingMessage 0073BCB8, FileRequestMessage 0073BC58.

Message* PairMessage::CreateAs(std::uintptr_t vtable) {
    auto* p = NewMessageMemory<PairMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    p->first_.ptr = nullptr;
    p->second_.ptr = nullptr;
    ei::SetVtable(p, vtable);
    return p;
}
void PairMessage::ConstructAs(std::uintptr_t vtable) {
    version_ = 1;
    first_.ptr = nullptr;
    second_.ptr = nullptr;
    ei::SetVtable(this, vtable);
}
PairMessage* PairMessage::CloneAs(std::uintptr_t vtable) {
    auto* p = NewMessageMemory<PairMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    CopyCounted(p->first_, first_);
    CopyCounted(p->second_, second_);
    ei::SetVtable(p, vtable);
    return p;
}
void PairMessage::ResetAs(std::uintptr_t vtable) {
    ei::SetVtable(this, vtable);
    ReleaseCounted(second_);
    ReleaseCounted(first_);
    ei::SetVtable(this, kRefObjectRootVtable);
    version_ = 1;
    first_.ptr = nullptr;
    second_.ptr = nullptr;
    ei::SetVtable(this, vtable);
}
void PairMessage::WriteFirst(BufferCursor* cursor) { PutObject(cursor, first_); }
void PairMessage::ReadFirst(BufferCursor* cursor) { TakeObject(cursor, first_); }
void PairMessage::DestructAs(std::uintptr_t vtable) {
    ei::SetVtable(this, vtable);
    ReleaseCounted(second_);
    ReleaseCounted(first_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

Message* __fastcall BindingMessage::Create() { return CreateAs(kVtableAddress); }   // 00415320
BindingMessage* BindingMessage::Construct() {   // 0041C870
    ConstructAs(kVtableAddress);
    return this;
}
BindingMessage* BindingMessage::Clone() { return static_cast<BindingMessage*>(CloneAs(kVtableAddress)); }   // 00415350
int BindingMessage::OnShutdown() {   // 004153B0
    ResetAs(kVtableAddress);
    return 0;
}
void BindingMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }   // 00415450
void BindingMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }     // 00415470
void BindingMessage::Destruct() { DestructAs(kVtableAddress); }            // 004154D0

Message* __fastcall FileRequestMessage::Create() { return CreateAs(kVtableAddress); }   // 004179E0
FileRequestMessage* FileRequestMessage::Construct() {   // 004140B0
    ConstructAs(kVtableAddress);
    return this;
}
FileRequestMessage* FileRequestMessage::Clone() { return static_cast<FileRequestMessage*>(CloneAs(kVtableAddress)); }   // 004140D0
int FileRequestMessage::OnShutdown() {   // 00414130
    ResetAs(kVtableAddress);
    return 0;
}
void FileRequestMessage::Write(BufferCursor* cursor) { WriteFirst(cursor); }   // 004141F0
void FileRequestMessage::Read(BufferCursor* cursor) { ReadFirst(cursor); }     // 00414210
void FileRequestMessage::Destruct() { DestructAs(kVtableAddress); }            // 00414270

// ================================================================================================
// 0073BC70 PlayerMessage.

namespace {
void InitPlayer(PlayerMessage* m) {
    StringEmpty(m->name_);
    StringEmpty(m->text_);
    m->id_ = -1;
    m->number_ = -1;
    m->serial_ = -1;
    ei::SetVtable(m, PlayerMessage::kVtableAddress);
    m->host_ = 0;
    m->state_ = 1;
}
} // namespace

Message* __fastcall PlayerMessage::Create() {   // 00414420
    auto* p = NewMessageMemory<PlayerMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    InitPlayer(p);
    return p;
}
PlayerMessage* PlayerMessage::Construct() {   // 0041C680
    MessageConstructBase(this);
    StringEmpty(name_);
    StringEmptyInline(text_);
    ei::SetVtable(this, kVtableAddress);
    id_ = -1;
    number_ = -1;
    serial_ = -1;
    host_ = 0;
    state_ = 1;
    return this;
}
PlayerMessage* PlayerMessage::Clone() {   // 00414500
    auto* p = NewMessageMemory<PlayerMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    StringCopy(p->name_, name_);
    StringCopy(p->text_, text_);
    p->state_ = state_;
    p->host_ = host_;
    p->id_ = id_;
    p->serial_ = serial_;
    p->number_ = number_;
    p->firstLine_ = firstLine_;
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int PlayerMessage::OnShutdown() {   // 00414650
    ei::SetVtable(this, kVtableAddress);
    StringFree(text_);
    StringFree(name_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    InitPlayer(this);
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
void PlayerMessage::Destruct() {   // 004148E0
    ei::SetVtable(this, kVtableAddress);
    StringFree(text_);
    StringFree(name_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BC88 LineMessage.

Message* __fastcall LineMessage::Create() {   // 00414960
    auto* p = NewMessageMemory<LineMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(p->text_);
    p->connection_.ptr = nullptr;
    ei::SetVtable(p, kVtableAddress);
    return p;
}
LineMessage* LineMessage::Construct() {   // 0041C740
    MessageConstructBase(this);
    StringEmpty(text_);
    connection_.ptr = nullptr;
    ei::SetVtable(this, kVtableAddress);
    return this;
}
LineMessage* LineMessage::Clone() {   // 00414A00
    auto* p = NewMessageMemory<LineMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    p->number_ = number_;
    StringCopy(p->text_, text_);
    CopyCounted(p->connection_, connection_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int LineMessage::OnShutdown() {   // 00414AC0
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(connection_);
    StringFree(text_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    StringEmpty(text_);
    connection_.ptr = nullptr;
    ei::SetVtable(this, kVtableAddress);
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
void LineMessage::Destruct() {   // 00414D10
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(connection_);
    StringFree(text_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BCA0 HostMessage.

Message* __fastcall HostMessage::Create() {   // 00414DB0
    auto* p = NewMessageMemory<HostMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(p->address_);
    StringEmpty(p->zone_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
HostMessage* HostMessage::Construct() {   // 0041C7C0
    MessageConstructBase(this);
    StringEmpty(address_);
    StringEmptyInline(zone_);
    ei::SetVtable(this, kVtableAddress);
    return this;
}
HostMessage* HostMessage::Clone() {   // 00414E80
    auto* p = NewMessageMemory<HostMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    StringCopy(p->address_, address_);
    StringCopy(p->zone_, zone_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int HostMessage::OnShutdown() {   // 00414FA0
    ei::SetVtable(this, kVtableAddress);
    StringFree(zone_);
    StringFree(address_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    StringEmpty(address_);
    StringEmpty(zone_);
    ei::SetVtable(this, kVtableAddress);
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
void HostMessage::Destruct() {   // 004152A0
    ei::SetVtable(this, kVtableAddress);
    StringFree(zone_);
    StringFree(address_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BCE8 VariableMessage.

Message* __fastcall VariableMessage::Create() {   // 00415980
    auto* p = NewMessageMemory<VariableMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(p->name_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
VariableMessage* VariableMessage::Clone() {   // 00415A30
    auto* p = NewMessageMemory<VariableMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    StringCopy(p->name_, name_);
    std::memcpy(&p->value_, &value_, 4);
    p->serial_ = serial_;
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int VariableMessage::OnShutdown() {   // 00415B60
    ei::SetVtable(this, kVtableAddress);
    StringFree(name_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    StringEmpty(name_);
    ei::SetVtable(this, kVtableAddress);
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
void VariableMessage::Destruct() {   // 00415D00
    ei::SetVtable(this, kVtableAddress);
    StringFree(name_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BD18 FileMessage.

Message* __fastcall FileMessage::Create() {   // 00416160
    auto* p = NewMessageMemory<FileMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    StringEmpty(p->name_);
    ClearRefs(p->chunks_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
FileMessage* FileMessage::Clone() {   // 00416220
    auto* p = NewMessageMemory<FileMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    p->chunkCount_ = chunkCount_;
    p->size_ = size_;
    StringCopy(p->name_, name_);
    const RefVector& from = chunks_;
    RefVector& to = p->chunks_;
    std::size_t n = static_cast<std::size_t>(from.finish - from.start);
    ClearRefs(to);
    auto* block = static_cast<RefPtr*>(stlp::allocate(n * sizeof(RefPtr)));
    to.start = block;
    to.finish = block;
    to.eos = block + n;
    to.finish = SharedRefsCopy(from.start, from.finish, block, 0);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int FileMessage::OnShutdown() {   // 00416390
    ei::SetVtable(this, kVtableAddress);
    for (RefPtr *p = chunks_.start, *end = chunks_.finish; p != end; ++p) FileRefDestroy(p, nullptr, 0);
    FileRefsDeallocate(&chunks_.eos, nullptr, chunks_.start, static_cast<std::size_t>(chunks_.eos - chunks_.start));
    StringFree(name_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    StringEmpty(name_);
    ClearRefs(chunks_);
    ei::SetVtable(this, kVtableAddress);
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
void FileMessage::Destruct() {   // 00416640
    ei::SetVtable(this, kVtableAddress);
    for (RefPtr *p = chunks_.start, *end = chunks_.finish; p != end; ++p) p->Destroy();
    stlp::deallocate(chunks_.start, static_cast<std::size_t>(chunks_.eos - chunks_.start) * sizeof(RefPtr));
    StringFree(name_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BD30 FileChunkMessage.

Message* __fastcall FileChunkMessage::Create() {   // 004166F0
    auto* p = NewMessageMemory<FileChunkMessage>();
    if (!p) return nullptr;
    p->version_ = 1;
    p->file_.ptr = nullptr;
    ei::SetVtable(p, kVtableAddress);
    return p;
}
FileChunkMessage* FileChunkMessage::Clone() {   // 00416720
    auto* p = NewMessageMemory<FileChunkMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    CopyCounted(p->file_, file_);
    p->index_ = index_;
    std::memcpy(p->data_, data_, kDataSize);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int FileChunkMessage::OnShutdown() {   // 00416780
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(file_);
    ei::SetVtable(this, kRefObjectRootVtable);
    version_ = 1;
    file_.ptr = nullptr;
    ei::SetVtable(this, kVtableAddress);
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
void FileChunkMessage::Destruct() {   // 004168E0
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(file_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BD48 AwardMessage.

Message* __fastcall AwardMessage::Create() {   // 00416940
    auto* p = NewMessageMemory<AwardMessage>();
    if (!p) return nullptr;
    FreshHeader(p);
    p->connection_.ptr = nullptr;
    StringEmpty(p->text_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
AwardMessage* AwardMessage::Clone() {   // 00416A10
    auto* p = NewMessageMemory<AwardMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    CopyCounted(p->connection_, connection_);
    p->kind_ = kind_;
    std::memcpy(&p->value_, &value_, 4);
    StringCopy(p->text_, text_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int AwardMessage::OnShutdown() {   // 00416B30
    ei::SetVtable(this, kVtableAddress);
    StringFree(text_);
    ReleaseCounted(connection_);
    ei::SetVtable(this, kRefObjectRootVtable);
    FreshHeader(this);
    connection_.ptr = nullptr;
    StringEmpty(text_);
    ei::SetVtable(this, kVtableAddress);
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
void AwardMessage::Destruct() {   // 00416D70
    ei::SetVtable(this, kVtableAddress);
    StringFree(text_);
    ReleaseCounted(connection_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BD60 TradeMessage.

namespace {
void InitTrade(TradeMessage* m) {
    m->player_.ptr = nullptr;
    m->partner_.ptr = nullptr;
    m->offer_.ptr = nullptr;
    m->partnerOffer_.ptr = nullptr;
    ei::SetVtable(m, TradeMessage::kVtableAddress);
    m->done_ = 0;
    m->revision_ = 0;
    m->partnerRevision_ = 0;
}
void FreeTrade(TradeMessage* m) {
    ReleaseCounted(m->partnerOffer_);
    ReleaseCounted(m->offer_);
    ReleaseCounted(m->partner_);
    ReleaseCounted(m->player_);
}
} // namespace

Message* __fastcall TradeMessage::Create() {   // 00416DF0
    auto* p = NewMessageMemory<TradeMessage>();
    if (!p) return nullptr;
    p->version_ = 1;
    InitTrade(p);
    return p;
}
TradeMessage* TradeMessage::Clone() {   // 00416E30
    auto* p = NewMessageMemory<TradeMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    CopyCounted(p->player_, player_);
    CopyCounted(p->partner_, partner_);
    CopyCounted(p->offer_, offer_);
    CopyCounted(p->partnerOffer_, partnerOffer_);
    p->done_ = done_;
    p->revision_ = revision_;
    p->partnerRevision_ = partnerRevision_;
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int TradeMessage::OnShutdown() {   // 00416EB0
    ei::SetVtable(this, kVtableAddress);
    FreeTrade(this);
    ei::SetVtable(this, kRefObjectRootVtable);
    version_ = 1;
    InitTrade(this);
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
void TradeMessage::Destruct() {   // 00416FC0
    ei::SetVtable(this, kVtableAddress);
    FreeTrade(this);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BD78 PartyMemberMessage.

namespace {
// The memory stream's constructor as the factory and the reset inline it.
void InitMemberStream(CMemoryStream& s) {
    s.version_ = 2000000000;
    ei::SetVtable(&s, kMemoryStreamVtable);
    s.data_ = nullptr;
    s.capacity_ = 0;
    s.position_ = 0;
    s.size_ = 0;
    s.ok_ = 1;
    s.bitMask_ = 0x80;
    s.bitsIndex_ = -1;
}
void FreeMemberStream(CMemoryStream& s) {
    std::uint8_t* data = s.data_;
    ei::SetVtable(&s, kMemoryStreamVtable);
    if (data) gamecrt::operator_delete(data);
    ei::SetVtable(&s, kStreamVtable);
}
} // namespace

Message* __fastcall PartyMemberMessage::Create() {   // 00417300
    auto* p = NewMessageMemory<PartyMemberMessage>();
    if (!p) return nullptr;
    p->version_ = 1;
    InitMemberStream(p->stream_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
// Original bug kept: the copy's stream is sized like ours, then its (uninitialised) bytes are
// read into ours (slot 1, Read) instead of ours being written into it.
PartyMemberMessage* PartyMemberMessage::Clone() {   // 00417420
    auto* p = NewMessageMemory<PartyMemberMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    CMemoryStream& stream = p->stream_;
    InitMemberStream(stream);
    stream.SetSize(stream_.size_);
    CStream* copy = &stream;
    copy->Read(stream_.data_, stream.size_);
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int PartyMemberMessage::OnShutdown() {   // 004174F0
    ei::SetVtable(this, kVtableAddress);
    FreeMemberStream(stream_);
    ei::SetVtable(this, kRefObjectRootVtable);
    version_ = 1;
    InitMemberStream(stream_);
    ei::SetVtable(this, kVtableAddress);
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
void PartyMemberMessage::Destruct() {   // 00417620
    ei::SetVtable(this, kVtableAddress);
    FreeMemberStream(stream_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BDD0 PartyMessage.

namespace {
void InitParty(PartyMessage* m) {
    ClearRefs(m->members_);
    m->player_.ptr = nullptr;
    m->partner_.ptr = nullptr;
    ei::SetVtable(m, PartyMessage::kVtableAddress);
    m->acceptedRevision_ = 0;
    m->revision_ = 1;
    m->accepted_ = 0;
    m->partyValue_ = 0;
    m->trading_ = 0;
}
} // namespace

Message* __fastcall PartyMessage::Create() {   // 00417650
    auto* p = NewMessageMemory<PartyMessage>();
    if (!p) return nullptr;
    p->version_ = 1;
    InitParty(p);
    return p;
}
PartyMessage* PartyMessage::Clone() {   // 004176A0
    auto* p = NewMessageMemory<PartyMessage>();
    if (!p) return nullptr;
    CloneHeader(p, this);
    std::uint8_t allocator = 0;
    const void* copy = RefsGetAllocator(&members_, nullptr, &allocator);
    RefsBaseConstruct(&p->members_, nullptr, static_cast<std::size_t>(members_.finish - members_.start), copy);
    p->members_.finish = UninitializedCopy(members_.start, members_.finish, p->members_.start, 0);
    p->partyValue_ = partyValue_;
    p->accepted_ = accepted_;
    p->acceptedRevision_ = acceptedRevision_;
    p->revision_ = revision_;
    CopyCounted(p->player_, player_);
    CopyCounted(p->partner_, partner_);
    ei::SetVtable(p, kVtableAddress);
    p->trading_ = trading_;
    return p;
}
int PartyMessage::OnShutdown() {   // 004177A0
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(partner_);
    ReleaseCounted(player_);
    for (RefPtr *p = members_.start, *end = members_.finish; p != end; ++p) RefPtrDestroyAt(p, nullptr, 0);
    RefsDeallocate(&members_.eos, nullptr, members_.start, static_cast<std::size_t>(members_.eos - members_.start));
    ei::SetVtable(this, kRefObjectRootVtable);
    version_ = 1;
    InitParty(this);
    return 0;
}
void PartyMessage::Write(BufferCursor* cursor) {   // 00412AC0
    std::int32_t count = static_cast<std::int32_t>(members_.finish - members_.start);
    Put(cursor, count);
    for (std::int32_t i = 0; i < count; ++i) PutObject(cursor, members_.start[i]);
    Put(cursor, partyValue_);
    Put(cursor, accepted_);
    Put(cursor, acceptedRevision_);
    Put(cursor, revision_);
    PutObject(cursor, player_);
    PutObject(cursor, partner_);
}
void PartyMessage::Read(BufferCursor* cursor) {   // 00412B70
    std::int32_t count;
    Take(cursor, count);
    RefsResize(&members_, nullptr, static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        RefPtr& slot = members_.start[i];
        AssignCounted(slot, CurrentConnection()->ReadObjectRef(cursor));
    }
    Take(cursor, partyValue_);
    Take(cursor, accepted_);
    Take(cursor, acceptedRevision_);
    Take(cursor, revision_);
    TakeObject(cursor, player_);
    TakeObject(cursor, partner_);
}
void PartyMessage::Destruct() {   // 004178F0
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(partner_);
    ReleaseCounted(player_);
    for (RefPtr *p = members_.start, *end = members_.finish; p != end; ++p)
        if (p->ptr) p->ptr->Release();
    stlp::deallocate(members_.start, static_cast<std::size_t>(members_.eos - members_.start) * sizeof(RefPtr));
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// 0073BB28 ServerEntry (net_classes.h): made by 0041A7E0 and 0041D420, read from the server list.
// Its deleting destructor is 004128C0; slot 1 is the empty 00412580.

ServerEntry* ServerEntry::Clone() {   // 00412590
    auto* p = NewMessageMemory<ServerEntry>();
    if (!p) return nullptr;
    ei::SetVtable(p, kRefValueVtable);
    p->address_ = address_;
    std::memcpy(&p->timeout_, &timeout_, 4);   // the floats copied as dwords
    std::memcpy(&p->ping_, &ping_, 4);
    p->foreign_ = foreign_;
    p->state_ = state_;
    ei::SetVtable(p, ServerEntryBase::kVtableAddress);
    StringCopy(p->name_, name_);
    p->kind_ = kind_;
    StringCopy(p->description_, description_);
    p->players_ = players_;
    p->maxPlayers_ = maxPlayers_;
    p->password_ = password_;
    ei::SetVtable(p, kVtableAddress);
    return p;
}
int ServerEntry::OnShutdown() {   // 00412750
    ei::SetVtable(this, kVtableAddress);
    StringBufferFree(&description_);
    StringBufferFree(&name_);
    ei::SetVtable(this, kRefObjectRootVtable);
    ConstructBase();
    StringEmpty(name_);
    StringEmpty(description_);
    ei::SetVtable(this, kVtableAddress);
    AssignEmptyText(name_);
    kind_ = 4;
    AssignEmptyText(description_);
    players_ = 0;
    maxPlayers_ = 0;
    password_ = 0;
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
void ServerEntry::Destruct() {   // 004128E0
    ei::SetVtable(this, kVtableAddress);
    StringFree(description_);
    StringFree(name_);
    ei::SetVtable(this, kRefObjectRootVtable);
}

// ================================================================================================
// The registrations: factories and constructor bodies directly; the slots through shims, which
// call the qualified member (rule 4).

namespace {

#define EI_MESSAGE_SLOT_SHIMS(Cls)                                                                      \
    int __fastcall Cls##ResetHook(Cls* self) { return self->Cls::OnShutdown(); }                        \
    Cls* __fastcall Cls##CloneHook(Cls* self) { return self->Cls::Clone(); }                            \
    void __fastcall Cls##WriteHook(Cls* self, void*, BufferCursor* cursor) { self->Cls::Write(cursor); } \
    void __fastcall Cls##ReadHook(Cls* self, void*, BufferCursor* cursor) { self->Cls::Read(cursor); }

EI_MESSAGE_SLOT_SHIMS(LoginMessage)
EI_MESSAGE_SLOT_SHIMS(SayMessage)
EI_MESSAGE_SLOT_SHIMS(CommandMessage)
EI_MESSAGE_SLOT_SHIMS(FileRequestMessage)
EI_MESSAGE_SLOT_SHIMS(PartyMessage)
EI_MESSAGE_SLOT_SHIMS(PartyMemberMessage)
EI_MESSAGE_SLOT_SHIMS(PlayerMessage)
EI_MESSAGE_SLOT_SHIMS(LineMessage)
EI_MESSAGE_SLOT_SHIMS(HostMessage)
EI_MESSAGE_SLOT_SHIMS(BindingMessage)
EI_MESSAGE_SLOT_SHIMS(ZoneMessage)
EI_MESSAGE_SLOT_SHIMS(MapMessage)
EI_MESSAGE_SLOT_SHIMS(VariableMessage)
EI_MESSAGE_SLOT_SHIMS(FileMessage)
EI_MESSAGE_SLOT_SHIMS(FileChunkMessage)
EI_MESSAGE_SLOT_SHIMS(AwardMessage)
EI_MESSAGE_SLOT_SHIMS(TradeMessage)
EI_MESSAGE_SLOT_SHIMS(ServerEntry)

#undef EI_MESSAGE_SLOT_SHIMS

} // namespace

// Factory, reset, clone, write, read and destructor body per class.
EI_REPLACE(0x004170A0, LoginMessage::Create);
EI_REPLACE(0x00413570, LoginMessageResetHook);
EI_REPLACE(0x00413440, LoginMessageCloneHook);
EI_REPLACE(0x004136B0, LoginMessageWriteHook);
EI_REPLACE(0x00413750, LoginMessageReadHook);
EI_REPLACE_METHOD(0x00413800, LoginMessage::Destruct);
EI_REPLACE(0x004171A0, SayMessage::Create);
EI_REPLACE(0x00413A50, SayMessageResetHook);
EI_REPLACE(0x00413930, SayMessageCloneHook);
EI_REPLACE(0x00413B10, SayMessageWriteHook);
EI_REPLACE(0x00413B50, SayMessageReadHook);
EI_REPLACE_METHOD(0x00413C40, SayMessage::Destruct);
EI_REPLACE(0x00417250, CommandMessage::Create);
EI_REPLACE(0x00413E40, CommandMessageResetHook);
EI_REPLACE(0x00413D20, CommandMessageCloneHook);
EI_REPLACE(0x00413F00, CommandMessageWriteHook);
EI_REPLACE(0x00413F50, CommandMessageReadHook);
EI_REPLACE_METHOD(0x00414060, CommandMessage::Destruct);
EI_REPLACE(0x004179E0, FileRequestMessage::Create);
EI_REPLACE(0x00414130, FileRequestMessageResetHook);
EI_REPLACE(0x004140D0, FileRequestMessageCloneHook);
EI_REPLACE(0x004141F0, FileRequestMessageWriteHook);
EI_REPLACE(0x00414210, FileRequestMessageReadHook);
EI_REPLACE_METHOD(0x00414270, FileRequestMessage::Destruct);
EI_REPLACE(0x00417650, PartyMessage::Create);
EI_REPLACE(0x004177A0, PartyMessageResetHook);
EI_REPLACE(0x004176A0, PartyMessageCloneHook);
EI_REPLACE(0x00412AC0, PartyMessageWriteHook);
EI_REPLACE(0x00412B70, PartyMessageReadHook);
EI_REPLACE_METHOD(0x004178F0, PartyMessage::Destruct);
EI_REPLACE(0x00417300, PartyMemberMessage::Create);
EI_REPLACE(0x004174F0, PartyMemberMessageResetHook);
EI_REPLACE(0x00417420, PartyMemberMessageCloneHook);
EI_REPLACE(0x00417560, PartyMemberMessageWriteHook);
EI_REPLACE(0x004175B0, PartyMemberMessageReadHook);
EI_REPLACE_METHOD(0x00417620, PartyMemberMessage::Destruct);
EI_REPLACE(0x00414420, PlayerMessage::Create);
EI_REPLACE(0x00414650, PlayerMessageResetHook);
EI_REPLACE(0x00414500, PlayerMessageCloneHook);
EI_REPLACE(0x00414770, PlayerMessageWriteHook);
EI_REPLACE(0x00414820, PlayerMessageReadHook);
EI_REPLACE_METHOD(0x004148E0, PlayerMessage::Destruct);
EI_REPLACE(0x00414960, LineMessage::Create);
EI_REPLACE(0x00414AC0, LineMessageResetHook);
EI_REPLACE(0x00414A00, LineMessageCloneHook);
EI_REPLACE(0x00414BB0, LineMessageWriteHook);
EI_REPLACE(0x00414C00, LineMessageReadHook);
EI_REPLACE_METHOD(0x00414D10, LineMessage::Destruct);
EI_REPLACE(0x00414DB0, HostMessage::Create);
EI_REPLACE(0x00414FA0, HostMessageResetHook);
EI_REPLACE(0x00414E80, HostMessageCloneHook);
EI_REPLACE(0x004150B0, HostMessageWriteHook);
EI_REPLACE(0x00415120, HostMessageReadHook);
EI_REPLACE_METHOD(0x004152A0, HostMessage::Destruct);
EI_REPLACE(0x00415320, BindingMessage::Create);
EI_REPLACE(0x004153B0, BindingMessageResetHook);
EI_REPLACE(0x00415350, BindingMessageCloneHook);
EI_REPLACE(0x00415450, BindingMessageWriteHook);
EI_REPLACE(0x00415470, BindingMessageReadHook);
EI_REPLACE_METHOD(0x004154D0, BindingMessage::Destruct);
EI_REPLACE(0x00415570, ZoneMessage::Create);
EI_REPLACE(0x00415740, ZoneMessageResetHook);
EI_REPLACE(0x00415620, ZoneMessageCloneHook);
EI_REPLACE(0x00415800, ZoneMessageWriteHook);
EI_REPLACE(0x00415840, ZoneMessageReadHook);
EI_REPLACE_METHOD(0x00415930, ZoneMessage::Destruct);
EI_REPLACE(0x00415D50, MapMessage::Create);
EI_REPLACE(0x00415F20, MapMessageResetHook);
EI_REPLACE(0x00415E00, MapMessageCloneHook);
EI_REPLACE(0x00415FE0, MapMessageWriteHook);
EI_REPLACE(0x00416020, MapMessageReadHook);
EI_REPLACE_METHOD(0x00416110, MapMessage::Destruct);
EI_REPLACE(0x00415980, VariableMessage::Create);
EI_REPLACE(0x00415B60, VariableMessageResetHook);
EI_REPLACE(0x00415A30, VariableMessageCloneHook);
EI_REPLACE(0x00415C20, VariableMessageWriteHook);
EI_REPLACE(0x00415C80, VariableMessageReadHook);
EI_REPLACE_METHOD(0x00415D00, VariableMessage::Destruct);
EI_REPLACE(0x00416160, FileMessage::Create);
EI_REPLACE(0x00416390, FileMessageResetHook);
EI_REPLACE(0x00416220, FileMessageCloneHook);
EI_REPLACE(0x004164D0, FileMessageWriteHook);
EI_REPLACE(0x00416530, FileMessageReadHook);
EI_REPLACE_METHOD(0x00416640, FileMessage::Destruct);
EI_REPLACE(0x004166F0, FileChunkMessage::Create);
EI_REPLACE(0x00416780, FileChunkMessageResetHook);
EI_REPLACE(0x00416720, FileChunkMessageCloneHook);
EI_REPLACE(0x00416800, FileChunkMessageWriteHook);
EI_REPLACE(0x00416850, FileChunkMessageReadHook);
EI_REPLACE_METHOD(0x004168E0, FileChunkMessage::Destruct);
EI_REPLACE(0x00416940, AwardMessage::Create);
EI_REPLACE(0x00416B30, AwardMessageResetHook);
EI_REPLACE(0x00416A10, AwardMessageCloneHook);
EI_REPLACE(0x00416C00, AwardMessageWriteHook);
EI_REPLACE(0x00416C60, AwardMessageReadHook);
EI_REPLACE_METHOD(0x00416D70, AwardMessage::Destruct);
EI_REPLACE(0x00416DF0, TradeMessage::Create);
EI_REPLACE(0x00416EB0, TradeMessageResetHook);
EI_REPLACE(0x00416E30, TradeMessageCloneHook);
EI_REPLACE(0x00412C80, TradeMessageWriteHook);
EI_REPLACE(0x00412CE0, TradeMessageReadHook);
EI_REPLACE_METHOD(0x00416FC0, TradeMessage::Destruct);

EI_REPLACE(0x00412750, ServerEntryResetHook);
EI_REPLACE(0x00412590, ServerEntryCloneHook);
EI_REPLACE(0x00412990, ServerEntryWriteHook);
EI_REPLACE(0x00412A30, ServerEntryReadHook);
EI_REPLACE_METHOD(0x004128E0, ServerEntry::Destruct);

EI_REPLACE_METHOD(0x00413350, LoginMessage::Construct);
EI_REPLACE_METHOD(0x004138A0, SayMessage::Construct);
EI_REPLACE_METHOD(0x00413C90, CommandMessage::Construct);
EI_REPLACE_METHOD(0x004140B0, FileRequestMessage::Construct);
EI_REPLACE_METHOD(0x0041C680, PlayerMessage::Construct);
EI_REPLACE_METHOD(0x0041C740, LineMessage::Construct);
EI_REPLACE_METHOD(0x0041C7C0, HostMessage::Construct);
EI_REPLACE_METHOD(0x0041C870, BindingMessage::Construct);
EI_REPLACE_METHOD(0x00418C30, ZoneMessage::Construct);

} // namespace net
