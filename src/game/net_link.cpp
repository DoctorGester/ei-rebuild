// Message links over streams (net_link.h): the file stream, the link base, the loopback link, the replay link,
// the framed link and the per-player driver. See docs/notes/network.md and docs/notes/cleanup-network.md.
#include "net_link.h"

#include "core_streams.h"
#include "net_connection.h"
#include "net_messages.h"
#include "world.h"   // WorldHandle (CreatePair)

#include "lib/bytes.h"
#include "platform/file.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>


namespace netlink {

namespace {

// The largest message Send takes (its stack buffer holds the length and the bytes).
constexpr unsigned kMaxMessage = 0x7FFC;

unsigned BufferFill(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    return static_cast<unsigned>(buffer->PositionOf(buffer->high));
}

// A write the way these functions inline it: in place (marking the buffer written) when it fits
// before `end`, otherwise through WriteMore.
void BufferPut(GameBuffer* buffer, const void* from, unsigned size) {
    if (buffer->cur + size > buffer->end) {
        buffer->WriteMore(from, size);
        return;
    }
    buffer->flags |= 1;
    std::memcpy(buffer->cur, from, size);
    buffer->cur += size;
}

} // namespace

// FileStream

FileStream::FileStream() = default;

FileStream::FileStream(const FileStream&) : FileStream() {}

FileStream::~FileStream() = default;

std::uint32_t FileStream::Read(void* buffer, std::uint32_t size) {
    std::uint32_t read = file.Read(buffer, size);
    ok = static_cast<std::uint8_t>(ok & (read == size ? 1 : 0));
    return read;
}

void FileStream::Write(const void* buffer, std::uint32_t size) { file.Write(buffer, size); }

long FileStream::SeekFrom(long position) { return file.Seek(position, platform::File::begin); }
void FileStream::Seek(std::uint32_t position) { SeekFrom(static_cast<long>(position)); }

std::uint32_t FileStream::Size() { return file.GetLength(); }
std::uint32_t FileStream::Position() { return file.GetPosition(); }

long FileStream::Restart() {
    ok = 1;
    return file.Seek(0L, platform::File::begin);
}
void FileStream::Clear() { Restart(); }

// NetLink

NetLink::NetLink() { recording = 0; }

NetLink::NetLink(const NetLink& source)
    : RefObject(), recording(source.recording), skipNext(source.skipNext), record(source.record) {}

NetLink::~NetLink() {}

int NetLink::IncomingCount() { return HasIncoming() ? 1 : 0; }

bool NetLink::Send(ByteStream*) { return true; }

bool NetLink::Receive(MemoryStream* stream) {
    Record(stream);
    return true;
}

void NetLink::Record(ByteStream* stream) {
    if (!recording) return;
    if (skipNext) {
        skipNext = 0;
        return;
    }
    record.WriteSized(stream);
}

// modeCreate | modeWrite; the stream's ok tells whether the file opened.
void NetLink::OpenRecording(const char* path) {
    record.Open(path, FileStream::kOpenWrite, 0);
    skipNext = 1;
    recording = 1;
}

void NetLink::CloseRecording() {
    record.Close();
    recording = 0;
}

// LoopbackLink

LoopbackLink::LoopbackLink() = default;

// The game's queue copy is kept: each copy is a new stream of the source's size whose Read takes the source's
// buffer, so the new stream's fresh bytes (zeros here) are read into the source (both then hold them).
LoopbackLink::LoopbackLink(const LoopbackLink& other) : NetLink(other) {
    for (const MemoryStream& source : other.queue) {
        MemoryStream& copy = queue.emplace_back();
        copy.SetSize(source.size);
        copy.Read(const_cast<std::uint8_t*>(source.data.data()), copy.size);
    }
    peer.Assign(other.peer);
}

// The first goes to the player's pending link; null for a null player. Without memory for the
// second the original crashes; so does this. (The original rereads the pending link for the peers:
// releasing the previous one does not touch it, so it is `first`.)
LoopbackLink* LoopbackLink::CreatePair(WorldHandle* player) {
    if (!player) return nullptr;
    LoopbackLink* first = net::NewCounted<LoopbackLink>();
    player->pendingLink.Assign(first);
    LoopbackLink* second = net::NewCounted<LoopbackLink>();
    first->peer.Assign(second);
    second->peer.Assign(first);
    return second;
}

LoopbackLink::~LoopbackLink() { peer.Reset(); }

// Torn down (the queue cleared rather than destroyed) and built again in place (the reference count
// and +9 kept): no peer, an empty queue, a closed file, not recording.
void LoopbackLink::OnShutdown() {
    peer.Reset();
    std::destroy_at(&queue);
    record.~FileStream();
    new (this) LoopbackLink();
}

// Null when the allocation fails.
LoopbackLink* LoopbackLink::Clone() { return net::NewCounted<LoopbackLink>(*this); }

bool LoopbackLink::Connected() { return IsLive(peer); }

bool LoopbackLink::Alive() {
    if (!queue.empty()) return true;
    return IsLive(peer);
}

bool LoopbackLink::HasIncoming() { return !queue.empty(); }

int LoopbackLink::IncomingCount() { return static_cast<int>(queue.size()); }

// A new empty stream at the end of the peer's queue takes a copy of `stream` (CopyFrom). False
// without a live peer.
bool LoopbackLink::Send(ByteStream* stream) {
    LoopbackLink* currentPeer = peer;
    if (!IsLive(currentPeer)) return false;
    currentPeer->queue.emplace_back().CopyFrom(stream);
    return true;
}

// The front of the queue copied into `stream` (CopyFrom) and dropped, then recorded as the base
// does. False when nothing is queued.
bool LoopbackLink::Receive(MemoryStream* stream) {
    if (queue.empty()) return false;
    stream->CopyFrom(&queue.front());
    queue.pop_front();
    Record(stream);
    return true;
}

// Lets go of the peer, then has the peer let go of this one (a reference is held across the peer's call).
void LoopbackLink::Disconnect() {
    HeldRef<LoopbackLink> currentPeer(peer);
    peer.Reset();
    if (currentPeer) currentPeer->Disconnect();
}

// ReplayLink

// FramedLink

FramedLink::FramedLink() {}

// The original reads the copy's new buffer (never written) into the source's inbox instead of the other way
// round: the copy's bytes are whatever the allocation held (zeros here) and the source's are overwritten with them. Kept.
FramedLink::FramedLink(const FramedLink& other) : NetLink(other) {
    inbox.SetSize(other.inbox.size);
    ByteStream* stream = &inbox;
    stream->Read(const_cast<std::uint8_t*>(other.inbox.data.data()), inbox.size);
}

FramedLink::~FramedLink() {}

void FramedLink::Append(const void* data, std::uint32_t size) {
    ByteStream* currentInbox = &inbox;
    std::uint32_t end = currentInbox->Size();
    currentInbox->Seek(end);
    currentInbox->Write(data, size);
}

// The inbox is emptied only when it held anything.
void FramedLink::TakeAll(ByteStream* out) {
    out->Clear();
    ByteStream* currentInbox = &inbox;
    if (currentInbox->Size() == 0) return;
    const void* data = inbox.data.data();
    std::uint32_t size = currentInbox->Size();
    out->Write(data, size);
    currentInbox->Clear();
}

// Whether the inbox holds a whole message (a length and that many bytes).
bool FramedLink::HasIncoming() {
    ByteStream* currentInbox = &inbox;
    if (currentInbox->Size() < 4) return false;
    currentInbox->Seek(0);
    std::uint32_t length = 0;   // the read always fills it
    currentInbox->ReadValue(length);
    std::uint32_t size = currentInbox->Size();
    return size >= length + 4;
}

// The number of whole messages in the inbox, counted from the start; stops at a length that runs
// past the end or is not positive.
int FramedLink::IncomingCount() {
    ByteStream* currentInbox = &inbox;
    int count = 0;
    currentInbox->Seek(0);
    for (;;) {
        std::uint32_t at = currentInbox->Position() + 4;
        if (at > currentInbox->Size()) break;
        std::int32_t length = 0;
        currentInbox->ReadValue(length);
        std::uint32_t end = currentInbox->Position() + static_cast<std::uint32_t>(length);
        if (end > currentInbox->Size()) break;
        if (length <= 0) break;
        std::uint32_t next = currentInbox->Position() + static_cast<std::uint32_t>(length);
        currentInbox->Seek(next);
        ++count;
    }
    return count;
}

// The message framed (its length, then its bytes, read from the start) in a stack buffer and handed to slot
// 13; when that fails the link disconnects. False for a message over 0x7FFC bytes or a failed send.
bool FramedLink::Send(ByteStream* stream) {
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
bool FramedLink::Receive(MemoryStream* stream) {
    ByteStream* currentInbox = &inbox;
    std::uint32_t length = 0;
    currentInbox->Seek(0);
    currentInbox->ReadValue(length);
    if (currentInbox->Size() < length + 4) return false;
    stream->Resize(length);
    currentInbox->Read(stream->data.data(), length);
    std::uint32_t rest = currentInbox->Size() - 4 - length;
    std::uint8_t* data = inbox.data.data();
    std::memmove(data, data + length + 4, rest);
    inbox.Resize(rest);
    return true;
}

// DriverLink

DriverLink::DriverLink() {}

DriverLink::DriverLink(const DriverLink& other) : FramedLink(other) {
    player.Assign(other.player);
}

// The inbox's unread bytes, with the player's incoming reliable bytes after them, written back over
// the start of that stream (when there were any and the player is alive); then the player let go.
void DriverLink::Disconnect() {
    MemoryStream pending;
    TakeAll(&pending);
    if (pending.size != 0 && IsLive(player.Get())) {
        pending.Seek(pending.size);
        std::uint32_t count = BufferFill(&player.Get()->reliableIn);
        ByteStream* stream = &pending;
        stream->Write(player.Get()->reliableIn.begin, count);
        player.Get()->reliableIn.Seek(0);
        BufferPut(&player.Get()->reliableIn, pending.data.data(), pending.size);
    }
    player.Reset();
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

bool DriverLink::Connected() { return IsLive(player.Get()); }

// The player's incoming reliable bytes appended to the inbox, and that stream emptied (flags 6).
void DriverLink::Pump() {
    net::Connection* newPlayer = player.Get();
    if (!IsLive(newPlayer)) return;
    GameBuffer* in = &newPlayer->reliableIn;
    std::uint32_t count = BufferFill(in);
    Append(in->begin, count);
    in->flags = 6;
    in->high = in->begin;
    in->cur = in->begin;
}

// `size` bytes written at the end of the player's outgoing reliable stream (when the player is
// alive); whether the player is (still) alive.
bool DriverLink::SendBytes(const void* data, std::uint32_t size) {
    if (IsLive(player.Get())) {
        std::uint32_t fill = BufferFill(&player.Get()->reliableOut);
        player.Get()->reliableOut.Seek(static_cast<std::int32_t>(fill));
        BufferPut(&player.Get()->reliableOut, data, size);
    }
    return IsLive(player.Get());
}

} // namespace netlink
