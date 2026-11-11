// Message links over streams (net_link.h): the file stream, the link base, the loopback link, the framed
// link and the per-player driver. See docs/notes/network.md.
#include "game/net/net_link.h"

#include "engine/core/core_streams.h"
#include "game/net/net_connection.h"
#include "game/net/net_messages.h"
#include "game/world/world.h"   // WorldHandle (CreatePair)

#include "lib/bytes.h"
#include "platform/platform.h"
#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>


namespace netlink {

namespace {

// The largest message Send takes (its buffer holds the length and the bytes).
constexpr unsigned kMaxMessage = 0x7FFC;

} // namespace

// NetLink

NetLink::NetLink() = default;

NetLink::~NetLink() {}

int NetLink::IncomingCount() { return HasIncoming() ? 1 : 0; }

bool NetLink::Send(ByteStream*) { return true; }

bool NetLink::Receive(MemoryStream*) { return true; }

// LoopbackLink

LoopbackLink::LoopbackLink() = default;

// The first goes to the player's pending link; null for a null player. Without memory for the
// second the original crashes; so does this. (The original rereads the pending link for the peers:
// releasing the previous one does not touch it, so it is `first`.)
LoopbackLink* LoopbackLink::CreatePair(WorldHandle* player) {
    if (!player) return nullptr;
    LoopbackLink* first = new LoopbackLink();
    player->pendingLink.Assign(first);
    LoopbackLink* second = new LoopbackLink();
    first->peer.Assign(second);
    second->peer.Assign(first);
    return second;
}

LoopbackLink::~LoopbackLink() { peer.Reset(); }

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

// The front of the queue copied into `stream` (CopyFrom) and dropped. False when nothing is queued.
bool LoopbackLink::Receive(MemoryStream* stream) {
    if (queue.empty()) return false;
    stream->CopyFrom(&queue.front());
    queue.pop_front();
    return true;
}

// Lets go of the peer, then has the peer let go of this one (a reference is held across the peer's call).
void LoopbackLink::Disconnect() {
    HeldRef<LoopbackLink> currentPeer(peer);
    peer.Reset();
    if (currentPeer) currentPeer->Disconnect();
}

// FramedLink

FramedLink::FramedLink() {}

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

// The message framed (its length, then its bytes, read from the start) in a buffer and handed to SendBytes;
// when that fails the link disconnects. False for a message over 0x7FFC bytes or a failed send.
bool FramedLink::Send(ByteStream* stream) {
    std::uint8_t buffer[kMaxMessage + 4];
    if (stream->Size() > kMaxMessage) return false;
    std::uint32_t size = stream->Size();
    lib::Store(buffer, size);
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

// The inbox's unread bytes, with the player's incoming reliable bytes after them, written back over
// the start of that stream (when there were any and the player is alive); then the player let go.
void DriverLink::Disconnect() {
    MemoryStream pending;
    TakeAll(&pending);
    if (pending.size != 0 && IsLive(player.Get())) {
        pending.Seek(pending.size);
        std::uint32_t count = static_cast<std::uint32_t>(player.Get()->reliableIn.Size());
        ByteStream* stream = &pending;
        stream->Write(player.Get()->reliableIn.begin, count);
        player.Get()->reliableIn.Seek(0);
        player.Get()->reliableIn.Write(pending.data.data(), pending.size);
    }
    player.Reset();
}

DriverLink::~DriverLink() {
    DriverLink::Disconnect();
}

bool DriverLink::Connected() { return IsLive(player.Get()); }

// The player's incoming reliable bytes appended to the inbox, and that stream emptied.
void DriverLink::Pump() {
    net::Connection* newPlayer = player.Get();
    if (!IsLive(newPlayer)) return;
    GameBuffer* in = &newPlayer->reliableIn;
    std::uint32_t count = static_cast<std::uint32_t>(in->Size());
    Append(in->begin, count);
    in->Clear();
}

// `size` bytes written at the end of the player's outgoing reliable stream (when the player is
// alive); whether the player is (still) alive.
bool DriverLink::SendBytes(const void* data, std::uint32_t size) {
    if (IsLive(player.Get())) {
        GrowingBuffer& out = player.Get()->reliableOut;
        out.Seek(out.Size());
        out.Write(data, size);
    }
    return IsLive(player.Get());
}

} // namespace netlink
