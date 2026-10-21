// Message links over streams (net_link.h): the file stream 0073C8D8, the link base 0073C8A4, the
// loopback link 0073C8F8, the replay link 0073C92C, the framed link 0073C9C8 and the per-player
// driver 0073C990 (00477E60-0047BC10). The queue's STLport list members are library code, called
// at their original addresses. See docs/notes/network.md and docs/notes/cleanup-network.md.
#include "net_link.h"

#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"
#include "record_lists.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/mfc.h"
#include "originals_library.h"
#include "originals_engine.h"

#include <cstring>
#include <new>

namespace netlink {

namespace {

constexpr std::uint32_t kStreamLimit = 2000000000;

// --- Library (STLport list<CMemoryStream> of the queue) and other callees -----------------------
// The queue as recordlist::StreamQueue's list: its constructors (00478D50 / 00479150), insert of a
// default element (00478FF0), erase (004790F0), clear (00479570) and range insert (004795B0); the
// node pool (00405950 / 00404D70) and the header's deallocation (00479530, n nodes).
using Queue = recordlist::StreamQueue;
Queue::List* AsQueue(QueueNode** queue) { return reinterpret_cast<Queue::List*>(queue); }
Queue::Node* AsNode(QueueNode* node) { return reinterpret_cast<Queue::Node*>(node); }
QueueNode* InsertDefault(QueueNode** queue, QueueNode* pos) {
    Queue::Node* inserted;
    Queue::InsertDefault(AsQueue(queue), nullptr, &inserted, AsNode(pos));
    return reinterpret_cast<QueueNode*>(inserted);
}
void Erase(QueueNode** queue, QueueNode* pos) {
    Queue::Node* next;
    Queue::Erase(AsQueue(queue), nullptr, &next, AsNode(pos));
}
inline void* __fastcall NodeAllocate(std::size_t a0) { return libcrt::NodeAllocate(a0); }
void __fastcall NodeFree(void* a0, std::size_t a1) { libcrt::NodeDeallocate(a0, a1); }
// The game's memmove (006E79A0).

bool IsLive(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }

// The counted pointer release the classes inline: the count dropped, the object deleted at zero.
void ReleaseCounted(CRefObject* object) {
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0) delete object;
}

// A counted pointer assignment as 004782E0 inlines it (the new object counted first).
void AssignPeer(LoopbackLink** slot, LoopbackLink* object) {
    LoopbackLink* previous = *slot;
    *slot = object;
    if (object) ++object->refs_;
    ReleaseCounted(previous);
}

// The largest message Send takes (its stack buffer holds the length and the bytes).
constexpr unsigned kMaxMessage = 0x7FFC;

std::int32_t Addr(const std::uint8_t* p) { return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p)); }

// The number of bytes a reliable stream holds (its high-water mark raised first), as inlined.
unsigned BufferFill(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return static_cast<unsigned>(b->base - Addr(b->begin) + Addr(b->high));
}

// A write the way these functions inline it: in place (marking the buffer written) when it fits
// before `end`, otherwise through slot 4.
void BufferPut(GameBuffer* b, const void* from, unsigned size) {
    if (b->cur + size > b->end) {
        b->WriteMore(from, size);
        return;
    }
    b->flags |= 1;
    std::memcpy(b->cur, from, size);
    b->cur += size;
}

} // namespace

// ================================================================================================
// FileStream (0073C8D8)

FileStream* FileStream::Construct() {   // 00478470
    ei::SetVtable(this, kStreamVtable);
    version_ = kStreamLimit;
    ok_ = 1;
    new (file_) mfc::CFile();
    ei::SetVtable(this, kVtableAddress);
    return this;
}

FileStream* FileStream::CopyConstruct(const FileStream*) { return Construct(); }   // 00479B10

void FileStream::Destruct() {   // 00477FB0
    ei::SetVtable(this, kVtableAddress);
    reinterpret_cast<mfc::CFile*>(file_)->~CFile();
    ei::SetVtable(this, kStreamVtable);
}
EI_REPLACE_METHOD(0x00477FB0, FileStream::Destruct);

FileStream* FileStream::DeletingDestruct(unsigned flags) {   // 00477F90
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00477F90, FileStream::DeletingDestruct);

std::uint32_t FileStream::ReadImpl(void* buffer, std::uint32_t size) {   // 00477F00
    std::uint32_t read = File()->Read(buffer, size);
    ok_ = static_cast<std::uint8_t>(ok_ & (read == size ? 1 : 0));
    return read;
}

void FileStream::WriteImpl(const void* buffer, std::uint32_t size) { File()->Write(buffer, size); }   // 00477F30

long FileStream::SeekFrom(long position) { return File()->Seek(position, 0u); }   // 00477F50
EI_REPLACE_METHOD(0x00477F50, FileStream::SeekFrom);
void FileStream::Seek(std::uint32_t position) { SeekFrom(static_cast<long>(position)); }

std::uint32_t FileStream::Size() { return File()->GetLength(); }       // 00477F70
std::uint32_t FileStream::Position() { return File()->GetPosition(); } // 00477F80

long FileStream::Restart() {   // 00477EE0
    ok_ = 1;
    return File()->Seek(0L, 0u);
}
EI_REPLACE_METHOD(0x00477EE0, FileStream::Restart);
void FileStream::Clear() { Restart(); }

namespace {
std::uint32_t __fastcall FileStreamSizeHook(FileStream* self) { return self->FileStream::Size(); }
std::uint32_t __fastcall FileStreamPositionHook(FileStream* self) { return self->FileStream::Position(); }
} // namespace
EI_REPLACE_METHOD(0x00477F00, FileStream::ReadImpl);
EI_REPLACE_METHOD(0x00477F30, FileStream::WriteImpl);
EI_REPLACE(0x00477F70, FileStreamSizeHook);
EI_REPLACE(0x00477F80, FileStreamPositionHook);

// ================================================================================================
// NetLink (0073C8A4)

NetLink* NetLink::Construct() {   // 00477E60
    ei::SetVtable(this, CRefObject::kVtableAddress);
    record_.Construct();
    ei::SetVtable(this, kVtableAddress);
    recording_ = 0;
    return this;
}

void NetLink::ConstructCopyOf(const NetLink* source) {
    ei::SetVtable(this, CRefObject::kVtableAddress);
    recording_ = source->recording_;
    skipNext_ = source->skipNext_;
    record_.CopyConstruct(&source->record_);
    ei::SetVtable(this, kVtableAddress);
}

void NetLink::Destruct() {   // 00478050
    record_.Destruct();
    ei::SetVtable(this, CRefCounted::kVtableAddress);
}
EI_REPLACE_METHOD(0x00478050, NetLink::Destruct);

NetLink* NetLink::DeletingDestruct(unsigned flags) {   // 00478030
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00478030, NetLink::DeletingDestruct);

int NetLink::IncomingCount() { return HasIncoming() ? 1 : 0; }   // 00478000
bool NetLink::SendImpl(CStream*) { return true; }                    // 00478100

bool NetLink::ReceiveImpl(CStream* stream) {   // 00478110
    Record(stream);
    return true;
}

void NetLink::Record(CStream* stream) {
    if (!recording_) return;
    if (skipNext_) {
        skipNext_ = 0;
        return;
    }
    record_.WriteSized(stream);
}

// modeCreate | modeWrite; the stream's ok tells whether the file opened.
void NetLink::OpenRecording(const char* path) {   // 004780B0
    record_.Open(path, FileStream::kOpenWrite, 0);
    skipNext_ = 1;
    recording_ = 1;
}

void NetLink::CloseRecording() {   // 004780E0
    record_.Close();
    recording_ = 0;
}

namespace {
int __fastcall LinkIncomingCountHook(NetLink* self) { return self->NetLink::IncomingCount(); }
} // namespace
EI_REPLACE(0x00478000, LinkIncomingCountHook);
EI_REPLACE_METHOD(0x00478100, NetLink::SendImpl);
EI_REPLACE_METHOD(0x00478110, NetLink::ReceiveImpl);

// ================================================================================================
// LoopbackLink (0073C8F8)

// A new loopback link (its count cleared before the null check, as the original); built as
// 004782E0 inlines it. Null when the allocation fails.
LoopbackLink* LoopbackLink::New() {
    auto* self = net::NewMessageMemory<LoopbackLink>();
    if (!self) return nullptr;
    self->NetLink::Construct();
    Queue::Construct(AsQueue(&self->queue_), nullptr, nullptr);
    self->peer_ = nullptr;   // 00478FB0, the null counted pointer
    ei::SetVtable(self, kVtableAddress);
    return self;
}

// The queue's destructor as 004787A0 inlines it: every element's deleting destructor (flags 0),
// the node back to the pool; then the header.
void LoopbackLink::DestroyQueue() {
    for (QueueNode* node = queue_->next; node != queue_;) {
        QueueNode* current = node;
        node = node->next;
        CStream* stream = &current->stream;
        stream->~CStream();
        if (current) NodeFree(current, sizeof(QueueNode));
    }
    queue_->next = queue_;
    queue_->prev = queue_;
    if (queue_) NodeFree(queue_, sizeof(QueueNode));
}

// The first goes to the player's link (+8); null for a null player. Without memory for the second
// the original crashes; so does this.
LoopbackLink* __fastcall LoopbackLink::CreatePair(void*, void*, std::uint8_t* player) {   // 004782E0
    if (!player) return nullptr;
    auto*& held = *reinterpret_cast<CRefObject**>(player + 8);
    LoopbackLink* first = New();
    CRefObject* previous = held;
    held = first;
    if (first) ++first->refs_;
    if (previous) previous->Release();
    LoopbackLink* second = New();
    AssignPeer(&static_cast<LoopbackLink*>(held)->peer_, second);
    AssignPeer(&second->peer_, static_cast<LoopbackLink*>(held));
    return second;
}
EI_REPLACE(0x004782E0, LoopbackLink::CreatePair);

void LoopbackLink::Destruct() {   // 004787A0
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(peer_);
    DestroyQueue();
    record_.Destruct();
    ei::SetVtable(this, CRefCounted::kVtableAddress);
}
EI_REPLACE_METHOD(0x004787A0, LoopbackLink::Destruct);

LoopbackLink* LoopbackLink::DeletingDestruct(unsigned flags) {   // 00478780
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00478780, LoopbackLink::DeletingDestruct);

// Destroyed and built again in place (the reference count and +9 kept): no peer, an empty queue, a
// closed file, not recording.
int LoopbackLink::OnShutdown() {   // 00478610
    ei::SetVtable(this, kVtableAddress);
    ReleaseCounted(peer_);
    Queue::Clear(AsQueue(&queue_));
    stlp::deallocate(queue_, 48);   // 00479530
    record_.Destruct();
    ei::SetVtable(this, CRefCounted::kVtableAddress);
    NetLink::Construct();
    Queue::Construct(AsQueue(&queue_), nullptr, nullptr);
    peer_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    return 0;
}

// A copy with the recording flags, a closed file of its own, a copy of every queued stream and the
// same peer (counted). Null when the allocation fails.
LoopbackLink* LoopbackLink::Clone() {   // 004784D0
    auto* copy = net::NewMessageMemory<LoopbackLink>();
    if (!copy) return nullptr;
    copy->ConstructCopyOf(this);
    copy->queue_ = nullptr;
    auto* header = static_cast<QueueNode*>(NodeAllocate(sizeof(QueueNode)));
    copy->queue_ = header;
    header->next = header;
    copy->queue_->prev = copy->queue_;
    Queue::InsertRange(AsQueue(&copy->queue_), nullptr, AsNode(copy->queue_->next), AsNode(queue_->next), AsNode(queue_));
    copy->peer_ = peer_;
    if (copy->peer_) ++copy->peer_->refs_;
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}

bool LoopbackLink::Connected() { return IsLive(peer_); }   // 00478710

bool LoopbackLink::Alive() {   // 00478730
    if (queue_->next != queue_) return true;
    return IsLive(peer_);
}

bool LoopbackLink::HasIncoming() { return queue_->next != queue_; }   // 00478750

int LoopbackLink::IncomingCount() {   // 00478760
    int count = 0;
    for (QueueNode* node = queue_->next; node != queue_; node = node->next) ++count;
    return count;
}

// A new empty stream at the end of the peer's queue takes a copy of `stream` (its slot 7). False
// without a live peer.
bool LoopbackLink::SendImpl(CStream* stream) {   // 00478220
    LoopbackLink* peer = peer_;
    if (!IsLive(peer)) return false;
    InsertDefault(&peer->queue_, peer->queue_);
    CStream* last = &peer_->queue_->prev->stream;
    last->CopyFrom(stream);
    return true;
}

// The front of the queue copied into `stream` (its slot 7) and dropped, then recorded as the base
// does. False when nothing is queued.
bool LoopbackLink::ReceiveImpl(CStream* stream) {   // 00478270
    if (queue_->next == queue_) return false;
    stream->CopyFrom(&queue_->next->stream);
    Erase(&queue_, queue_->next);
    Record(stream);
    return true;
}

// Lets go of the peer, then has the peer let go of this one (a reference held across its slot 11).
void LoopbackLink::Disconnect() {   // 00478140
    LoopbackLink* peer = peer_;
    if (peer) ++peer->refs_;
    LoopbackLink* previous = peer_;
    peer_ = nullptr;
    ReleaseCounted(previous);
    if (peer) peer->Disconnect();
    ReleaseCounted(peer);
}

namespace {
int __fastcall LoopbackResetHook(LoopbackLink* self) { return self->LoopbackLink::OnShutdown(); }
LoopbackLink* __fastcall LoopbackCloneHook(LoopbackLink* self) { return self->LoopbackLink::Clone(); }
int __fastcall LoopbackConnectedHook(LoopbackLink* self) { return self->LoopbackLink::Connected() ? 1 : 0; }
int __fastcall LoopbackAliveHook(LoopbackLink* self) { return self->LoopbackLink::Alive() ? 1 : 0; }
bool __fastcall LoopbackHasIncomingHook(LoopbackLink* self) { return self->LoopbackLink::HasIncoming(); }
int __fastcall LoopbackIncomingCountHook(LoopbackLink* self) { return self->LoopbackLink::IncomingCount(); }
void __fastcall LoopbackDisconnectHook(LoopbackLink* self) { self->LoopbackLink::Disconnect(); }
} // namespace
EI_REPLACE(0x00478610, LoopbackResetHook);
EI_REPLACE(0x004784D0, LoopbackCloneHook);
EI_REPLACE(0x00478710, LoopbackConnectedHook);
EI_REPLACE(0x00478730, LoopbackAliveHook);
EI_REPLACE(0x00478750, LoopbackHasIncomingHook);
EI_REPLACE(0x00478760, LoopbackIncomingCountHook);
EI_REPLACE_METHOD(0x00478220, LoopbackLink::SendImpl);
EI_REPLACE_METHOD(0x00478270, LoopbackLink::ReceiveImpl);
EI_REPLACE(0x00478140, LoopbackDisconnectHook);

// ================================================================================================
// ReplayLink (0073C92C)

// A new replay link in place as 00478960 / 00478B10 inline it: the base (not recording), then the
// second file stream.
void ReplayLink::ConstructInPlace() {
    NetLink::Construct();
    replay_.Construct();
    ei::SetVtable(this, kVtableAddress);
}

// modeRead; the stream's ok tells whether it opened. Without memory the original crashes (on the
// reference count's write before the null check); so does this.
ReplayLink* __fastcall ReplayLink::Create(void*, void*, const char* path) {   // 00478960
    auto* self = net::NewMessageMemory<ReplayLink>();
    if (self) self->ConstructInPlace();
    self->replay_.Open(path, FileStream::kOpenRead, 1);
    return self;
}
EI_REPLACE(0x00478960, ReplayLink::Create);

void ReplayLink::Destruct() {   // 00478CC0
    ei::SetVtable(this, kVtableAddress);
    replay_.Destruct();
    record_.Destruct();
    ei::SetVtable(this, CRefCounted::kVtableAddress);
}
EI_REPLACE_METHOD(0x00478CC0, ReplayLink::Destruct);

ReplayLink* ReplayLink::DeletingDestruct(unsigned flags) {   // 00478CA0
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00478CA0, ReplayLink::DeletingDestruct);

// Destroyed and built again in place (the reference count and +9 kept): two closed files, not
// recording.
int ReplayLink::OnShutdown() {   // 00478B10
    Destruct();
    ConstructInPlace();
    return 0;
}

// A copy with the recording flags and two closed files of its own. Null when the allocation fails.
ReplayLink* ReplayLink::Clone() {   // 00478A50
    auto* copy = net::NewMessageMemory<ReplayLink>();
    if (!copy) return nullptr;
    copy->ConstructCopyOf(this);
    copy->replay_.Construct();
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}

// Whether the recording has more to read (position, then size, through the file stream's slots).
bool ReplayLink::Alive() {   // 00478C30
    CStream* replay = &replay_;
    std::uint32_t position = replay->Position();
    std::uint32_t size = replay->Size();
    return position < size;
}

bool ReplayLink::HasIncoming() { return ReplayLink::Alive(); }   // 00478C50

// The next recorded message into `stream`; the replay stream's ok.
bool ReplayLink::ReceiveImpl(CStream* stream) {   // 00478C70
    replay_.ReadSized(stream);
    return replay_.ok_ != 0;
}

void ReplayLink::Disconnect() { replay_.Close(); }   // 00478C90

namespace {
int __fastcall ReplayResetHook(ReplayLink* self) { return self->ReplayLink::OnShutdown(); }
ReplayLink* __fastcall ReplayCloneHook(ReplayLink* self) { return self->ReplayLink::Clone(); }
int __fastcall ReplayAliveHook(ReplayLink* self) { return self->ReplayLink::Alive() ? 1 : 0; }
int __fastcall ReplayHasIncomingHook(ReplayLink* self) { return self->ReplayLink::HasIncoming() ? 1 : 0; }
void __fastcall ReplayDisconnectHook(ReplayLink* self) { self->ReplayLink::Disconnect(); }
} // namespace
EI_REPLACE(0x00478B10, ReplayResetHook);
EI_REPLACE(0x00478A50, ReplayCloneHook);
EI_REPLACE(0x00478C30, ReplayAliveHook);
EI_REPLACE(0x00478C50, ReplayHasIncomingHook);
EI_REPLACE_METHOD(0x00478C70, ReplayLink::ReceiveImpl);
EI_REPLACE(0x00478C90, ReplayDisconnectHook);

// ================================================================================================
// FramedLink (0073C9C8)

void FramedLink::Destruct() {   // 00479B90
    ei::SetVtable(&inbox_, kMemoryStreamVtable);
    if (inbox_.data_) gamecrt::operator_delete(inbox_.data_);
    ei::SetVtable(&inbox_, kStreamVtable);
    record_.Destruct();
    ei::SetVtable(this, CRefCounted::kVtableAddress);
}
EI_REPLACE_METHOD(0x00479B90, FramedLink::Destruct);

FramedLink* FramedLink::DeletingDestruct(unsigned flags) {   // 00479B70
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00479B70, FramedLink::DeletingDestruct);

void FramedLink::Append(const void* data, std::uint32_t size) {   // 0047B980
    CStream* inbox = &inbox_;
    std::uint32_t end = inbox->Size();
    inbox->Seek(end);
    inbox->Write(data, size);
}

// The inbox is emptied only when it held anything.
void FramedLink::TakeAll(CStream* out) {   // 0047B9B0
    out->Clear();
    CStream* inbox = &inbox_;
    if (inbox->Size() == 0) return;
    const void* data = inbox_.data_;
    std::uint32_t size = inbox->Size();
    out->Write(data, size);
    inbox->Clear();
}

// Whether the inbox holds a whole message (a length and that many bytes).
bool FramedLink::HasIncoming() {   // 0047B9F0
    CStream* inbox = &inbox_;
    if (inbox->Size() < 4) return false;
    inbox->Seek(0);
    std::uint32_t length = 0;   // the original's slot holds the pushed ECX; the read always fills it
    inbox->Read(&length, 4);
    std::uint32_t size = inbox->Size();
    return size >= length + 4;
}

// The number of whole messages in the inbox, counted from the start; stops at a length that runs
// past the end or is not positive.
int FramedLink::IncomingCount() {   // 0047BA40
    CStream* inbox = &inbox_;
    int count = 0;
    inbox->Seek(0);
    for (;;) {
        std::uint32_t at = inbox->Position() + 4;
        if (at > inbox->Size()) break;
        std::int32_t length = 0;   // as in 0047B9F0: always filled
        inbox->Read(&length, 4);
        std::uint32_t end = inbox->Position() + static_cast<std::uint32_t>(length);
        if (end > inbox->Size()) break;
        if (length <= 0) break;
        std::uint32_t next = inbox->Position() + static_cast<std::uint32_t>(length);
        inbox->Seek(next);
        ++count;
    }
    return count;
}

// The message framed (its length, then its bytes, read from the start) in a stack buffer and
// handed to slot 13; when that fails the link disconnects (slot 11). False for a message over
// 0x7FFC bytes or a failed send.
bool FramedLink::SendImpl(CStream* stream) {   // 0047BAD0
    std::uint8_t buffer[kMaxMessage + 4];
    if (stream->Size() > kMaxMessage) return false;
    std::uint32_t size = stream->Size();
    std::memcpy(buffer, &size, sizeof(size));
    stream->Seek(0);
    std::uint32_t count = stream->Size();
    stream->Read(buffer + 4, count);
    std::uint32_t total = stream->Size() + 4;
    if (SendBytes(buffer, total)) return true;
    Disconnect();
    return false;
}

// The first message's bytes into `stream` (a memory stream, resized to them), the rest of the inbox
// moved to its start. False when no whole message is there.
bool FramedLink::ReceiveImpl(CStream* to) {   // 0047BB70
    auto* stream = static_cast<CMemoryStream*>(to);
    CStream* inbox = &inbox_;
    std::uint32_t length = 0;
    inbox->Seek(0);
    inbox->Read(&length, 4);
    if (inbox->Size() < length + 4) return false;
    stream->Resize(length);
    inbox->Read(stream->data_, length);
    std::uint32_t rest = inbox->Size() - 4 - length;
    std::uint8_t* data = inbox_.data_;
    libcrt::Memmove(data, data + length + 4, rest);
    inbox_.Resize(rest);
    return true;
}

namespace {
int __fastcall FramedHasIncomingHook(FramedLink* self) { return self->FramedLink::HasIncoming() ? 1 : 0; }
int __fastcall FramedIncomingCountHook(FramedLink* self) { return self->FramedLink::IncomingCount(); }
} // namespace
EI_REPLACE(0x0047B9F0, FramedHasIncomingHook);
EI_REPLACE(0x0047BA40, FramedIncomingCountHook);
EI_REPLACE_METHOD(0x0047BAD0, FramedLink::SendImpl);
EI_REPLACE_METHOD(0x0047BB70, FramedLink::ReceiveImpl);

// ================================================================================================
// DriverLink (0073C990)

DriverLink* DriverLink::Construct() {   // 0047A030
    NetLink::Construct();
    inbox_.version_ = kStreamLimit;
    ei::SetVtable(&inbox_, kMemoryStreamVtable);
    inbox_.data_ = nullptr;
    inbox_.capacity_ = 0;
    inbox_.position_ = 0;
    inbox_.size_ = 0;
    inbox_.ok_ = 1;
    inbox_.bitMask_ = 0x80;
    inbox_.bitsIndex_ = -1;
    player_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    return this;
}

// The inbox's unread bytes, with the player's incoming reliable bytes after them, written back over
// the start of that stream (when there were any and the player is alive); then the player let go.
void DriverLink::Disconnect() {   // 00479810
    CMemoryStream pending;   // the original's stack stream: +4, +6, +7 and +0xC never written
    pending.version_ = kStreamLimit;
    pending.ok_ = 1;
    ei::SetVtable(&pending, kMemoryStreamVtable);
    pending.data_ = nullptr;
    pending.Reset();
    TakeAll(&pending);
    if (pending.size_ != 0 && IsLive(player_)) {
        pending.SeekImpl(pending.size_);
        std::uint32_t count = BufferFill(&player_->reliableIn_);
        CStream* stream = &pending;
        stream->Write(player_->reliableIn_.begin, count);
        player_->reliableIn_.Seek(0);
        BufferPut(&player_->reliableIn_, pending.data_, pending.size_);
    }
    net::Connection* player = player_;
    player_ = nullptr;
    ReleaseCounted(player);
    ei::SetVtable(&pending, kMemoryStreamVtable);
    if (pending.data_) gamecrt::operator_delete(pending.data_);
}

void DriverLink::Destruct() {   // 00479D40
    ei::SetVtable(this, kVtableAddress);
    DriverLink::Disconnect();
    ReleaseCounted(player_);
    FramedLink::Destruct();
}
EI_REPLACE_METHOD(0x00479D40, DriverLink::Destruct);

DriverLink* DriverLink::DeletingDestruct(unsigned flags) {   // 00479D20
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00479D20, DriverLink::DeletingDestruct);

// Destroyed (disconnecting) and built again in place with the constructor's code (0047A030): the
// reference count and +9 kept, no player, an empty inbox, a closed file.
int DriverLink::OnShutdown() {   // 00479C10
    Destruct();
    Construct();
    return 0;
}

// A copy with the recording flags, a closed file of its own, an inbox of the same size and the same
// player (counted). Null when the allocation fails. The original reads the copy's new buffer
// (never written) into the source's inbox instead of the other way round: the copy's bytes are
// whatever the allocation held and the source's are overwritten with them. Kept.
DriverLink* DriverLink::Clone() {   // 00479A30
    auto* copy = net::NewMessageMemory<DriverLink>();
    if (!copy) return nullptr;
    copy->ConstructCopyOf(this);
    CMemoryStream* inbox = &copy->inbox_;
    inbox->version_ = kStreamLimit;
    inbox->ok_ = 1;
    ei::SetVtable(inbox, kMemoryStreamVtable);
    inbox->data_ = nullptr;
    inbox->Reset();
    inbox->SetSize(inbox_.size_);
    CStream* stream = inbox;
    stream->Read(inbox_.data_, inbox->size_);
    ei::SetVtable(copy, FramedLink::kVtableAddress);
    copy->player_ = player_;
    if (copy->player_) ++copy->player_->refs_;
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}

bool DriverLink::Connected() { return IsLive(player_); }   // 00479780

// The player's incoming reliable bytes appended to the inbox (0047B980), and that stream emptied
// (flags 6).
void DriverLink::Pump() {   // 004797A0
    net::Connection* player = player_;
    if (!IsLive(player)) return;
    GameBuffer* in = &player->reliableIn_;
    std::uint32_t count = BufferFill(in);
    Append(in->begin, count);
    in->flags = 6;
    in->high = in->begin;
    in->cur = in->begin;
}

// `size` bytes written at the end of the player's outgoing reliable stream (when the player is
// alive); whether the player is (still) alive.
bool DriverLink::SendBytes(const void* data, std::uint32_t size) {   // 004796B0
    if (IsLive(player_)) {
        std::uint32_t fill = BufferFill(&player_->reliableOut_);
        player_->reliableOut_.Seek(static_cast<std::int32_t>(fill));
        BufferPut(&player_->reliableOut_, data, size);
    }
    return IsLive(player_);
}

namespace {
int __fastcall DriverResetHook(DriverLink* self) { return self->DriverLink::OnShutdown(); }
DriverLink* __fastcall DriverCloneHook(DriverLink* self) { return self->DriverLink::Clone(); }
int __fastcall DriverConnectedHook(DriverLink* self) { return self->DriverLink::Connected() ? 1 : 0; }
void __fastcall DriverPumpHook(DriverLink* self) { self->DriverLink::Pump(); }
void __fastcall DriverDisconnectHook(DriverLink* self) { self->DriverLink::Disconnect(); }
int __fastcall DriverSendBytesHook(DriverLink* self, void*, const void* data, std::uint32_t size) {
    return self->DriverLink::SendBytes(data, size) ? 1 : 0;
}
} // namespace
EI_REPLACE(0x00479C10, DriverResetHook);
EI_REPLACE(0x00479A30, DriverCloneHook);
EI_REPLACE(0x00479780, DriverConnectedHook);
EI_REPLACE(0x004797A0, DriverPumpHook);
EI_REPLACE(0x00479810, DriverDisconnectHook);
EI_REPLACE(0x004796B0, DriverSendBytesHook);

} // namespace netlink
