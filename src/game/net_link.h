#pragma once

// Message links over streams: the endpoints a player's messages go through, and the pump that keeps a driver
// link per remote player.
// More: docs/notes/network.md, "Message links".

#include "core_refcount.h"
#include "core_streams.h"
#include "net_connection.h"
#include "player_source.h"

#include "platform/platform.h"
#include <cstdint>

#include <cstddef>
#include <deque>
#include <vector>

namespace net {
class SessionBase;
}
class WorldHandle;

namespace netlink {

// A ByteStream over a file.
class FileStream : public ByteStream {
public:
    FileStream();
    ~FileStream() override;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // a short read clears ok
    void Write(const void* buffer, std::uint32_t size) override;
    void Seek(std::uint32_t position) override;                        // SeekFrom
    std::uint32_t Size() override;
    std::uint32_t Position() override;
    void Clear() override;                                             // ok again, back to the start

    // Seeks from the start; returns the file's position.
    long SeekFrom(long position);

    // The direction byte, then the file's Open; ok tells whether it opened.
    bool Open(std::string_view path, platform::FileMode mode, std::uint8_t newReading) {
        reading = newReading;
        ok = file.Open(path, mode);
        return ok != 0;
    }
    void Close() { file.Close(); }

    platform::File file;
};

class NetLink : public RefObject {
public:
    ~NetLink() override;

    virtual bool Connected() = 0;
    virtual bool Alive() = 0;                   // something to read, or connected
    virtual bool HasIncoming() = 0;
    virtual int IncomingCount();                // 1 when HasIncoming
    virtual bool Send(ByteStream* stream);         // dropped: true
    virtual bool Receive(MemoryStream* stream);      // true
    virtual void Pump() {}
    virtual void Disconnect() = 0;

protected:
    NetLink();
};

class LoopbackLink final : public NetLink {
public:
    LoopbackLink();
    ~LoopbackLink() override;

    bool Connected() override;                  // the peer is there, not detached
    bool Alive() override;
    bool HasIncoming() override;
    int IncomingCount() override;
    bool Send(ByteStream* stream) override;        // a copy on the peer's queue
    bool Receive(MemoryStream* stream) override;     // the front of the queue
    void Disconnect() override;

    // Two links joined to each other; the first goes to the player's link, the second is returned.
    static LoopbackLink* CreatePair(WorldHandle* player);

    std::deque<MemoryStream> queue;     // the peer's messages, taken from the front
    Ref<LoopbackLink> peer;
};

// A link that frames its messages in a byte inbox.
class FramedLink : public NetLink {
public:
    ~FramedLink() override;

    bool HasIncoming() override;                // a whole message in the inbox
    int IncomingCount() override;
    bool Send(ByteStream* stream) override;        // framed, then SendBytes
    bool Receive(MemoryStream* stream) override;
    virtual bool SendBytes(const void* data, std::uint32_t size) = 0;

    // `size` bytes appended at the end of the inbox.
    void Append(const void* data, std::uint32_t size);
    // `out` emptied, then given everything in the inbox, which is emptied.
    void TakeAll(ByteStream* out);

    MemoryStream inbox;         // received bytes: {uint32 length, bytes} per message

protected:
    FramedLink();
};

// The per-player driver: a framed link over a network connection's reliable streams.
class DriverLink final : public FramedLink {
public:
    DriverLink();
    ~DriverLink() override;

    bool Connected() override;                  // the player is there, not detached
    bool Alive() override { return true; }
    void Pump() override;                       // the incoming reliable bytes to the inbox
    void Disconnect() override;                 // the unread bytes given back
    bool SendBytes(const void* data, std::uint32_t size) override;

    Ref<net::Connection> player;
};

// The network session (made by World::OpenSession): a player source with two lists of drivers.
class DriverSession final : public PlayerSource {
public:
    DriverSession();
    ~DriverSession() override;

    void OnShutdown() override;          // both lists emptied
    DriverSession* Clone() override;    // drivers shared, holders counted
    bool Query() override { return true; }
    void TakeNew(std::vector<HeldRef<netlink::NetLink>>* joined) override;   // the live holders' links appended

    std::vector<SharedRef<DriverLink>> drivers;     // owning
    std::vector<HeldRef<DriverLink>> holders;    // plain holders (released, not detached)

    // A driver for every new connection of `session` (the game client or server) with something queued;
    // then each driver pumped, and those whose player is gone erased.
    void PumpDrivers(net::SessionBase* session);
    // Both lists emptied, then one driver for `player`, started and drained.
    DriverLink* Start(net::Connection* player);
};

} // namespace netlink
