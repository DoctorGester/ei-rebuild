// Message links over streams (net_link.h): the file stream, the link base, the loopback link, the replay link,
// the framed link and the per-player driver. See docs/notes/network.md and docs/notes/cleanup-network.md.
#include "net_link.h"

#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"
#include "record_lists.h"
#include "world.h"   // CWorldHandle (CreatePair)

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>
#include <new>

namespace netlink {

namespace {

using QueueElement = recordlist::QueueStreamElement;
void __fastcall NodeFree(void* a0, std::size_t a1) { libcrt::NodeDeallocate(a0, a1); }
// The counted pointer release the classes inline: the count dropped, the object deleted at zero.
void ReleaseCounted(CRefObject* object) {
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0) delete object;
}

// A counted pointer assignment as it is inlined (the new object counted first).
void AssignPeer(LoopbackLink** slot, LoopbackLink* object) {
    LoopbackLink* previous = *slot;
    *slot = object;
    if (object) ++object->refs_;
    ReleaseCounted(previous);
}

// The largest message Send takes (its stack buffer holds the length and the bytes).
constexpr unsigned kMaxMessage = 0x7FFC;

std::int32_t Addr(const std::uint8_t* p) { return static_cast<std::int32_t>(reinterpret_cast<std::uintptr_t>(p)); }

unsigned BufferFill(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return static_cast<unsigned>(b->base - Addr(b->begin) + Addr(b->high));
}

// A write the way these functions inline it: in place (marking the buffer written) when it fits
// before `end`, otherwise through WriteMore.
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

// FileStream

FileStream::FileStream() { new (file_.Get()) mfc::CFile(); }
EI_REWRITTEN_SPECIAL(0x00478470, FileStream::FileStream);

FileStream::FileStream(const FileStream&) : FileStream() {}
EI_REWRITTEN_SPECIAL(0x00479B10, FileStream::FileStream);

FileStream::~FileStream() { file_->~CFile(); }
EI_REWRITTEN_SPECIAL(0x00477FB0, FileStream::~FileStream);
EI_REWRITTEN_SPECIAL(0x00477F90, FileStream::~FileStream);

std::uint32_t FileStream::Read(void* buffer, std::uint32_t size) {
    std::uint32_t read = File()->Read(buffer, size);
    ok_ = static_cast<std::uint8_t>(ok_ & (read == size ? 1 : 0));
    return read;
}
EI_REWRITTEN(0x00477F00, FileStream::Read);

void FileStream::Write(const void* buffer, std::uint32_t size) { File()->Write(buffer, size); }
EI_REWRITTEN(0x00477F30, FileStream::Write);

long FileStream::SeekFrom(long position) { return File()->Seek(position, 0u); }
EI_REWRITTEN(0x00477F50, FileStream::SeekFrom);
void FileStream::Seek(std::uint32_t position) { SeekFrom(static_cast<long>(position)); }

std::uint32_t FileStream::Size() { return File()->GetLength(); }
std::uint32_t FileStream::Position() { return File()->GetPosition(); }

long FileStream::Restart() {
    ok_ = 1;
    return File()->Seek(0L, 0u);
}
EI_REWRITTEN(0x00477EE0, FileStream::Restart);
void FileStream::Clear() { Restart(); }

EI_REWRITTEN(0x00477F70, FileStream::Size);
EI_REWRITTEN(0x00477F80, FileStream::Position);

// NetLink

NetLink::NetLink() { recording_ = 0; }
EI_REWRITTEN_SPECIAL(0x00477E60, NetLink::NetLink);

NetLink::NetLink(const NetLink& source)
    : CRefObject(), recording_(source.recording_), skipNext_(source.skipNext_), record_(source.record_) {}

NetLink::~NetLink() {}
EI_REWRITTEN_SPECIAL(0x00478050, NetLink::~NetLink);
EI_REWRITTEN_SPECIAL(0x00478030, NetLink::~NetLink);

int NetLink::IncomingCount() { return HasIncoming() ? 1 : 0; }
EI_REWRITTEN(0x00478000, NetLink::IncomingCount);

bool NetLink::Send(CStream*) { return true; }
EI_REWRITTEN(0x00478100, NetLink::Send);

bool NetLink::Receive(CMemoryStream* stream) {
    Record(stream);
    return true;
}
EI_REWRITTEN(0x00478110, NetLink::Receive);

void NetLink::Record(CStream* stream) {
    if (!recording_) return;
    if (skipNext_) {
        skipNext_ = 0;
        return;
    }
    record_.WriteSized(stream);
}

// modeCreate | modeWrite; the stream's ok tells whether the file opened.
void NetLink::OpenRecording(const char* path) {
    record_.Open(path, FileStream::kOpenWrite, 0);
    skipNext_ = 1;
    recording_ = 1;
}
EI_REWRITTEN(0x004780B0, NetLink::OpenRecording);

void NetLink::CloseRecording() {
    record_.Close();
    recording_ = 0;
}
EI_REWRITTEN(0x004780E0, NetLink::CloseRecording);

// LoopbackLink

LoopbackLink::LoopbackLink() {
    queue_.Construct();
    peer_ = nullptr;
}

LoopbackLink::LoopbackLink(const LoopbackLink& other) : NetLink(other) {
    queue_.head = nullptr;
    auto* header = stlp::allocate_as<QueueNode>();
    queue_.head = header;
    header->next = header;
    queue_.head->prev = queue_.head;
    StreamQueue::InsertRange<QueueElement>(queue_.head->next, other.queue_.First(), other.queue_.End());
    peer_ = other.peer_;
    if (peer_) ++peer_->refs_;
}

// The queue's destructor as it is inlined: every element's deleting destructor (flags 0),
// the node back to the pool; then the header.
void LoopbackLink::DestroyQueue() {
    QueueNode* head = queue_.head;
    for (QueueNode* node = head->next; node != head;) {
        QueueNode* current = node;
        node = node->next;
        CStream* stream = &current->data;
        stream->~CStream();
        if (current) NodeFree(current, sizeof(QueueNode));
    }
    head->next = head;
    head->prev = head;
    if (head) NodeFree(head, sizeof(QueueNode));
}

// The first goes to the player's pending link; null for a null player. Without memory for the
// second the original crashes; so does this. (The original rereads the pending link for the peers:
// releasing the previous one does not touch it, so it is `first`.)
LoopbackLink* __stdcall LoopbackLink::CreatePair(CWorldHandle* player) {
    if (!player) return nullptr;
    LoopbackLink* first = net::NewCounted<LoopbackLink>();
    NetLink* previous = player->pendingLink_;
    player->pendingLink_ = first;
    if (first) ++first->refs_;
    if (previous) previous->Release();
    LoopbackLink* second = net::NewCounted<LoopbackLink>();
    AssignPeer(&first->peer_, second);
    AssignPeer(&second->peer_, first);
    return second;
}
EI_REWRITTEN(0x004782E0, LoopbackLink::CreatePair);

LoopbackLink::~LoopbackLink() {
    ReleaseCounted(peer_);
    DestroyQueue();
}
EI_REWRITTEN_SPECIAL(0x004787A0, LoopbackLink::~LoopbackLink);
EI_REWRITTEN_SPECIAL(0x00478780, LoopbackLink::~LoopbackLink);

// Torn down (the queue cleared rather than destroyed) and built again in place (the reference count
// and +9 kept): no peer, an empty queue, a closed file, not recording.
int LoopbackLink::OnShutdown() {
    ReleaseCounted(peer_);
    queue_.Clear<QueueElement>();
    stlp::deallocate(queue_.head, 48);
    record_.~FileStream();
    new (this) LoopbackLink();
    return 0;
}
EI_REWRITTEN(0x00478610, LoopbackLink::OnShutdown);

// Null when the allocation fails.
LoopbackLink* LoopbackLink::Clone() { return net::NewCounted<LoopbackLink>(*this); }
EI_REWRITTEN(0x004784D0, LoopbackLink::Clone);

bool LoopbackLink::Connected() { return IsLive(peer_); }
EI_REWRITTEN(0x00478710, LoopbackLink::Connected);

bool LoopbackLink::Alive() {
    if (!queue_.Empty()) return true;
    return IsLive(peer_);
}
EI_REWRITTEN(0x00478730, LoopbackLink::Alive);

bool LoopbackLink::HasIncoming() { return !queue_.Empty(); }
EI_REWRITTEN(0x00478750, LoopbackLink::HasIncoming);

int LoopbackLink::IncomingCount() {
    int count = 0;
    for (QueueNode* node = queue_.First(); node != queue_.End(); node = node->next) ++count;
    return count;
}
EI_REWRITTEN(0x00478760, LoopbackLink::IncomingCount);

// A new empty stream at the end of the peer's queue takes a copy of `stream` (its slot 7). False
// without a live peer.
bool LoopbackLink::Send(CStream* stream) {
    LoopbackLink* peer = peer_;
    if (!IsLive(peer)) return false;
    StreamQueue::InsertDefault<QueueElement>(peer->queue_.End());
    CStream* last = &peer_->queue_.head->prev->data;
    last->CopyFrom(stream);
    return true;
}
EI_REWRITTEN(0x00478220, LoopbackLink::Send);

// The front of the queue copied into `stream` (its slot 7) and dropped, then recorded as the base
// does. False when nothing is queued.
bool LoopbackLink::Receive(CMemoryStream* stream) {
    if (queue_.Empty()) return false;
    stream->CopyFrom(&queue_.First()->data);
    StreamQueue::Erase<QueueElement>(queue_.First());
    Record(stream);
    return true;
}
EI_REWRITTEN(0x00478270, LoopbackLink::Receive);

// Lets go of the peer, then has the peer let go of this one (a reference held across its slot 11).
void LoopbackLink::Disconnect() {
    LoopbackLink* peer = peer_;
    if (peer) ++peer->refs_;
    LoopbackLink* previous = peer_;
    peer_ = nullptr;
    ReleaseCounted(previous);
    if (peer) peer->Disconnect();
    ReleaseCounted(peer);
}
EI_REWRITTEN(0x00478140, LoopbackLink::Disconnect);

// ReplayLink

ReplayLink::ReplayLink() {}
ReplayLink::ReplayLink(const ReplayLink& other) : NetLink(other) {}

// modeRead; the stream's ok tells whether it opened. Without memory the original crashes (on the
// reference count's write before the null check); so does this.
ReplayLink* __stdcall ReplayLink::Create(const char* path) {
    auto* self = net::NewCounted<ReplayLink>();
    self->replay_.Open(path, FileStream::kOpenRead, 1);
    return self;
}
EI_REWRITTEN(0x00478960, ReplayLink::Create);

ReplayLink::~ReplayLink() {}
EI_REWRITTEN_SPECIAL(0x00478CC0, ReplayLink::~ReplayLink);
EI_REWRITTEN_SPECIAL(0x00478CA0, ReplayLink::~ReplayLink);

// Destroyed and built again in place (the reference count and +9 kept): two closed files, not recording.
int ReplayLink::OnShutdown() {
    net::Rebuild(this);
    return 0;
}
EI_REWRITTEN(0x00478B10, ReplayLink::OnShutdown);

// Null when the allocation fails.
ReplayLink* ReplayLink::Clone() { return net::NewCounted<ReplayLink>(*this); }
EI_REWRITTEN(0x00478A50, ReplayLink::Clone);

// Whether the recording has more to read (position, then size, through the file stream's slots).
bool ReplayLink::Alive() {
    CStream* replay = &replay_;
    std::uint32_t position = replay->Position();
    std::uint32_t size = replay->Size();
    return position < size;
}
EI_REWRITTEN(0x00478C30, ReplayLink::Alive);

bool ReplayLink::HasIncoming() { return ReplayLink::Alive(); }
EI_REWRITTEN(0x00478C50, ReplayLink::HasIncoming);

// The next recorded message into `stream`; the replay stream's ok.
bool ReplayLink::Receive(CMemoryStream* stream) {
    replay_.ReadSized(stream);
    return replay_.ok_ != 0;
}
EI_REWRITTEN(0x00478C70, ReplayLink::Receive);

void ReplayLink::Disconnect() { replay_.Close(); }
EI_REWRITTEN(0x00478C90, ReplayLink::Disconnect);

// FramedLink

FramedLink::FramedLink() {}

// The original reads the copy's new buffer (never written) into the source's inbox instead of the other way
// round: the copy's bytes are whatever the allocation held and the source's are overwritten with them. Kept.
FramedLink::FramedLink(const FramedLink& other) : NetLink(other) {
    inbox_.SetSize(other.inbox_.size_);
    CStream* stream = &inbox_;
    stream->Read(other.inbox_.data_, inbox_.size_);
}

FramedLink::~FramedLink() {}
EI_REWRITTEN_SPECIAL(0x00479B90, FramedLink::~FramedLink);
EI_REWRITTEN_SPECIAL(0x00479B70, FramedLink::~FramedLink);

void FramedLink::Append(const void* data, std::uint32_t size) {
    CStream* inbox = &inbox_;
    std::uint32_t end = inbox->Size();
    inbox->Seek(end);
    inbox->Write(data, size);
}
EI_REWRITTEN(0x0047B980, FramedLink::Append);

// The inbox is emptied only when it held anything.
void FramedLink::TakeAll(CStream* out) {
    out->Clear();
    CStream* inbox = &inbox_;
    if (inbox->Size() == 0) return;
    const void* data = inbox_.data_;
    std::uint32_t size = inbox->Size();
    out->Write(data, size);
    inbox->Clear();
}
EI_REWRITTEN(0x0047B9B0, FramedLink::TakeAll);

// Whether the inbox holds a whole message (a length and that many bytes).
bool FramedLink::HasIncoming() {
    CStream* inbox = &inbox_;
    if (inbox->Size() < 4) return false;
    inbox->Seek(0);
    std::uint32_t length = 0;   // the original's slot holds the pushed ECX; the read always fills it
    inbox->Read(&length, 4);
    std::uint32_t size = inbox->Size();
    return size >= length + 4;
}
EI_REWRITTEN(0x0047B9F0, FramedLink::HasIncoming);

// The number of whole messages in the inbox, counted from the start; stops at a length that runs
// past the end or is not positive.
int FramedLink::IncomingCount() {
    CStream* inbox = &inbox_;
    int count = 0;
    inbox->Seek(0);
    for (;;) {
        std::uint32_t at = inbox->Position() + 4;
        if (at > inbox->Size()) break;
        std::int32_t length = 0;
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
EI_REWRITTEN(0x0047BA40, FramedLink::IncomingCount);

// The message framed (its length, then its bytes, read from the start) in a stack buffer and handed to slot
// 13; when that fails the link disconnects. False for a message over 0x7FFC bytes or a failed send.
bool FramedLink::Send(CStream* stream) {
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
EI_REWRITTEN(0x0047BAD0, FramedLink::Send);

// The first message's bytes into `stream` (a memory stream, resized to them), the rest of the inbox
// moved to its start. False when no whole message is there.
bool FramedLink::Receive(CMemoryStream* stream) {
    CStream* inbox = &inbox_;
    std::uint32_t length = 0;
    inbox->Seek(0);
    inbox->Read(&length, 4);
    if (inbox->Size() < length + 4) return false;
    stream->Resize(length);
    inbox->Read(stream->data_, length);
    std::uint32_t rest = inbox->Size() - 4 - length;
    std::uint8_t* data = inbox_.data_;
    std::memmove(data, data + length + 4, rest);
    inbox_.Resize(rest);
    return true;
}
EI_REWRITTEN(0x0047BB70, FramedLink::Receive);

// DriverLink

DriverLink::DriverLink() { player_.ptr = nullptr; }
EI_REWRITTEN_SPECIAL(0x0047A030, DriverLink::DriverLink);

DriverLink::DriverLink(const DriverLink& other) : FramedLink(other) {
    player_.ptr = other.player_.ptr;
    if (player_.ptr) ++player_.ptr->refs_;
}

// The inbox's unread bytes, with the player's incoming reliable bytes after them, written back over
// the start of that stream (when there were any and the player is alive); then the player let go.
void DriverLink::Disconnect() {
    CMemoryStream pending;
    TakeAll(&pending);
    if (pending.size_ != 0 && IsLive(player_.Get())) {
        pending.Seek(pending.size_);
        std::uint32_t count = BufferFill(&player_.Get()->reliableIn_);
        CStream* stream = &pending;
        stream->Write(player_.Get()->reliableIn_.begin, count);
        player_.Get()->reliableIn_.Seek(0);
        BufferPut(&player_.Get()->reliableIn_, pending.data_, pending.size_);
    }
    net::Connection* player = player_.Get();
    player_.ptr = nullptr;
    ReleaseCounted(player);
}
EI_REWRITTEN(0x00479810, DriverLink::Disconnect);

DriverLink::~DriverLink() {
    DriverLink::Disconnect();
    ReleaseCounted(player_.ptr);
}
EI_REWRITTEN_SPECIAL(0x00479D40, DriverLink::~DriverLink);
EI_REWRITTEN_SPECIAL(0x00479D20, DriverLink::~DriverLink);

// Destroyed (disconnecting) and built again in place with the constructor's code: the
// reference count and +9 kept, no player, an empty inbox, a closed file.
int DriverLink::OnShutdown() {
    net::Rebuild(this);
    return 0;
}
EI_REWRITTEN(0x00479C10, DriverLink::OnShutdown);

// Null when the allocation fails.
DriverLink* DriverLink::Clone() { return net::NewCounted<DriverLink>(*this); }
EI_REWRITTEN(0x00479A30, DriverLink::Clone);

bool DriverLink::Connected() { return IsLive(player_.Get()); }
EI_REWRITTEN(0x00479780, DriverLink::Connected);

// The player's incoming reliable bytes appended to the inbox, and that stream emptied (flags 6).
void DriverLink::Pump() {
    net::Connection* player = player_.Get();
    if (!IsLive(player)) return;
    GameBuffer* in = &player->reliableIn_;
    std::uint32_t count = BufferFill(in);
    Append(in->begin, count);
    in->flags = 6;
    in->high = in->begin;
    in->cur = in->begin;
}
EI_REWRITTEN(0x004797A0, DriverLink::Pump);

// `size` bytes written at the end of the player's outgoing reliable stream (when the player is
// alive); whether the player is (still) alive.
bool DriverLink::SendBytes(const void* data, std::uint32_t size) {
    if (IsLive(player_.Get())) {
        std::uint32_t fill = BufferFill(&player_.Get()->reliableOut_);
        player_.Get()->reliableOut_.Seek(static_cast<std::int32_t>(fill));
        BufferPut(&player_.Get()->reliableOut_, data, size);
    }
    return IsLive(player_.Get());
}
EI_REWRITTEN(0x004796B0, DriverLink::SendBytes);

} // namespace netlink

