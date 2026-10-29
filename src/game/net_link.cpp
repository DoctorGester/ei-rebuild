// Message links over streams (net_link.h): the file stream, the link base, the loopback link, the replay link,
// the framed link and the per-player driver. See docs/notes/network.md and docs/notes/cleanup-network.md.
#include "net_link.h"

#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"
#include "world.h"   // CWorldHandle (CreatePair)

#include "lib/bytes.h"
#include "lib/file_io.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>


namespace netlink {

namespace {

// The largest message Send takes (its stack buffer holds the length and the bytes).
constexpr unsigned kMaxMessage = 0x7FFC;

unsigned BufferFill(GameBuffer* b) {
    if (b->cur > b->high) b->high = b->cur;
    return static_cast<unsigned>(b->PositionOf(b->high));
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

FileStream::FileStream() = default;

FileStream::FileStream(const FileStream&) : FileStream() {}

FileStream::~FileStream() = default;

std::uint32_t FileStream::Read(void* buffer, std::uint32_t size) {
    std::uint32_t read = file_.Read(buffer, size);
    ok_ = static_cast<std::uint8_t>(ok_ & (read == size ? 1 : 0));
    return read;
}

void FileStream::Write(const void* buffer, std::uint32_t size) { file_.Write(buffer, size); }

long FileStream::SeekFrom(long position) { return file_.Seek(position, FILE_BEGIN); }
void FileStream::Seek(std::uint32_t position) { SeekFrom(static_cast<long>(position)); }

std::uint32_t FileStream::Size() { return file_.GetLength(); }
std::uint32_t FileStream::Position() { return file_.GetPosition(); }

long FileStream::Restart() {
    ok_ = 1;
    return file_.Seek(0L, FILE_BEGIN);
}
void FileStream::Clear() { Restart(); }

// NetLink

NetLink::NetLink() { recording_ = 0; }

NetLink::NetLink(const NetLink& source)
    : CRefObject(), recording_(source.recording_), skipNext_(source.skipNext_), record_(source.record_) {}

NetLink::~NetLink() {}

int NetLink::IncomingCount() { return HasIncoming() ? 1 : 0; }

bool NetLink::Send(CStream*) { return true; }

bool NetLink::Receive(CMemoryStream* stream) {
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
void NetLink::OpenRecording(const char* path) {
    record_.Open(path, FileStream::kOpenWrite, 0);
    skipNext_ = 1;
    recording_ = 1;
}

void NetLink::CloseRecording() {
    record_.Close();
    recording_ = 0;
}

// LoopbackLink

LoopbackLink::LoopbackLink() = default;

// The game's queue copy is kept: each copy is a new stream of the source's size whose Read takes the source's
// buffer, so the new stream's fresh bytes (zeros here) are read into the source (both then hold them).
LoopbackLink::LoopbackLink(const LoopbackLink& other) : NetLink(other) {
    for (const CMemoryStream& source : other.queue_) {
        CMemoryStream& copy = queue_.emplace_back();
        copy.SetSize(source.size_);
        copy.Read(const_cast<std::uint8_t*>(source.data_.data()), copy.size_);
    }
    peer_.Assign(other.peer_);
}

// The first goes to the player's pending link; null for a null player. Without memory for the
// second the original crashes; so does this. (The original rereads the pending link for the peers:
// releasing the previous one does not touch it, so it is `first`.)
LoopbackLink* LoopbackLink::CreatePair(CWorldHandle* player) {
    if (!player) return nullptr;
    LoopbackLink* first = net::NewCounted<LoopbackLink>();
    player->pendingLink_.Assign(first);
    LoopbackLink* second = net::NewCounted<LoopbackLink>();
    first->peer_.Assign(second);
    second->peer_.Assign(first);
    return second;
}

LoopbackLink::~LoopbackLink() { peer_.Reset(); }

// Torn down (the queue cleared rather than destroyed) and built again in place (the reference count
// and +9 kept): no peer, an empty queue, a closed file, not recording.
void LoopbackLink::OnShutdown() {
    peer_.Reset();
    std::destroy_at(&queue_);
    record_.~FileStream();
    new (this) LoopbackLink();
}

// Null when the allocation fails.
LoopbackLink* LoopbackLink::Clone() { return net::NewCounted<LoopbackLink>(*this); }

bool LoopbackLink::Connected() { return IsLive(peer_); }

bool LoopbackLink::Alive() {
    if (!queue_.empty()) return true;
    return IsLive(peer_);
}

bool LoopbackLink::HasIncoming() { return !queue_.empty(); }

int LoopbackLink::IncomingCount() { return static_cast<int>(queue_.size()); }

// A new empty stream at the end of the peer's queue takes a copy of `stream` (CopyFrom). False
// without a live peer.
bool LoopbackLink::Send(CStream* stream) {
    LoopbackLink* peer = peer_;
    if (!IsLive(peer)) return false;
    peer->queue_.emplace_back().CopyFrom(stream);
    return true;
}

// The front of the queue copied into `stream` (CopyFrom) and dropped, then recorded as the base
// does. False when nothing is queued.
bool LoopbackLink::Receive(CMemoryStream* stream) {
    if (queue_.empty()) return false;
    stream->CopyFrom(&queue_.front());
    queue_.pop_front();
    Record(stream);
    return true;
}

// Lets go of the peer, then has the peer let go of this one (a reference is held across the peer's call).
void LoopbackLink::Disconnect() {
    HeldRef<LoopbackLink> peer(peer_);
    peer_.Reset();
    if (peer) peer->Disconnect();
}

// ReplayLink

// FramedLink

FramedLink::FramedLink() {}

// The original reads the copy's new buffer (never written) into the source's inbox instead of the other way
// round: the copy's bytes are whatever the allocation held (zeros here) and the source's are overwritten with them. Kept.
FramedLink::FramedLink(const FramedLink& other) : NetLink(other) {
    inbox_.SetSize(other.inbox_.size_);
    CStream* stream = &inbox_;
    stream->Read(const_cast<std::uint8_t*>(other.inbox_.data_.data()), inbox_.size_);
}

FramedLink::~FramedLink() {}

void FramedLink::Append(const void* data, std::uint32_t size) {
    CStream* inbox = &inbox_;
    std::uint32_t end = inbox->Size();
    inbox->Seek(end);
    inbox->Write(data, size);
}

// The inbox is emptied only when it held anything.
void FramedLink::TakeAll(CStream* out) {
    out->Clear();
    CStream* inbox = &inbox_;
    if (inbox->Size() == 0) return;
    const void* data = inbox_.data_.data();
    std::uint32_t size = inbox->Size();
    out->Write(data, size);
    inbox->Clear();
}

// Whether the inbox holds a whole message (a length and that many bytes).
bool FramedLink::HasIncoming() {
    CStream* inbox = &inbox_;
    if (inbox->Size() < 4) return false;
    inbox->Seek(0);
    std::uint32_t length = 0;   // the read always fills it
    inbox->ReadValue(length);
    std::uint32_t size = inbox->Size();
    return size >= length + 4;
}

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
        inbox->ReadValue(length);
        std::uint32_t end = inbox->Position() + static_cast<std::uint32_t>(length);
        if (end > inbox->Size()) break;
        if (length <= 0) break;
        std::uint32_t next = inbox->Position() + static_cast<std::uint32_t>(length);
        inbox->Seek(next);
        ++count;
    }
    return count;
}

// The message framed (its length, then its bytes, read from the start) in a stack buffer and handed to slot
// 13; when that fails the link disconnects. False for a message over 0x7FFC bytes or a failed send.
bool FramedLink::Send(CStream* stream) {
    std::uint8_t buffer[kMaxMessage + 4];
    if (stream->Size() > kMaxMessage) return false;
    std::uint32_t size = stream->Size();
    lib::StoreBytes(buffer, size);
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
bool FramedLink::Receive(CMemoryStream* stream) {
    CStream* inbox = &inbox_;
    std::uint32_t length = 0;
    inbox->Seek(0);
    inbox->ReadValue(length);
    if (inbox->Size() < length + 4) return false;
    stream->Resize(length);
    inbox->Read(stream->data_.data(), length);
    std::uint32_t rest = inbox->Size() - 4 - length;
    std::uint8_t* data = inbox_.data_.data();
    std::memmove(data, data + length + 4, rest);
    inbox_.Resize(rest);
    return true;
}

// DriverLink

DriverLink::DriverLink() {}

DriverLink::DriverLink(const DriverLink& other) : FramedLink(other) {
    player_.Assign(other.player_);
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
        BufferPut(&player_.Get()->reliableIn_, pending.data_.data(), pending.size_);
    }
    player_.Reset();
}

DriverLink::~DriverLink() {
    DriverLink::Disconnect();
}

// Destroyed (disconnecting) and built again in place with the constructor's code: the
// reference count and +9 kept, no player, an empty inbox, a closed file.
void DriverLink::OnShutdown() {
    net::Rebuild(this);
}

// Null when the allocation fails.
DriverLink* DriverLink::Clone() { return net::NewCounted<DriverLink>(*this); }

bool DriverLink::Connected() { return IsLive(player_.Get()); }

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

} // namespace netlink
