#pragma once

// Message links over streams: the endpoints a player's messages go through, and the pump that keeps a driver
// link per remote player.
// More: docs/notes/comments-c22i.md, "net_link.h".

#include "core_refcount.h"
#include "core_streams.h"
#include "net_connection.h"
#include "player_source.h"

#include "lib/mfc.h"
#include "lib/static_object.h"
#include "lib/stlp_list.h"

#include <cstddef>
#include <cstdint>

namespace net {
class SessionBase;
}
class CWorldHandle;

namespace netlink {

// A CStream over a file. The CFile is raw storage (MFC): the constructor and destructor build and tear it
// down explicitly.
class FileStream : public CStream {
public:
    FileStream();
    // A fresh stream; `other` is not read.
    FileStream(const FileStream& other);
    ~FileStream() override;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // a short read clears ok_
    void Write(const void* buffer, std::uint32_t size) override;
    void Seek(std::uint32_t position) override;                        // SeekFrom
    std::uint32_t Size() override;
    std::uint32_t Position() override;
    void Clear() override;                                             // Restart

    // Seeks from the start; returns the CFile's result.
    long SeekFrom(long position);
    // ok_ again, back to the start; returns the seek's result.
    long Restart();

    // The direction byte, then the CFile's Open; ok_ tells whether it opened.
    bool Open(std::string_view path, unsigned flags, std::uint8_t reading) {
        reading_ = reading;
        ok_ = File()->Open(std::string(path).c_str(), flags, nullptr) != 0;
        return ok_ != 0;
    }
    void Close() { File()->Close(); }

    // The CFile called through its own vtable (MFC 4.2's).
    mfc::CFileVirtuals* File() { return file_->Virtuals(); }

    static constexpr unsigned kOpenRead = 0;         // CFile::modeRead
    static constexpr unsigned kOpenWrite = 0x1001;   // CFile::modeCreate | modeWrite

    lib::InPlace<mfc::CFile> file_;
};

// The loopback link's queue.
using StreamQueue = stlp::list<CMemoryStream>;
using QueueNode = StreamQueue::Node;

class NetLink : public CRefObject {
public:
    ~NetLink() override;

    virtual bool Connected() = 0;
    virtual bool Alive() = 0;                   // something to read, or connected
    virtual bool HasIncoming() = 0;
    virtual int IncomingCount();                // 1 when HasIncoming
    virtual bool Send(CStream* stream);         // dropped: true
    virtual bool Receive(CMemoryStream* stream);      // records the stream: true
    virtual void Pump() {}
    virtual void Disconnect() = 0;
    virtual int Unknown12() { return 0; }

    // Starts recording into a new file `path`; the first message is dropped.
    void OpenRecording(const char* path);
    void CloseRecording();
    // What Receive does with a message while recording: the first after OpenRecording is dropped, the others
    // are written to the file with their size.
    void Record(CStream* stream);

    std::uint8_t recording_;
    std::uint8_t skipNext_;      // the next recorded message is dropped (set by OpenRecording)
    std::uint8_t unused_0A[2];
    FileStream record_;

protected:
    // The record stream, not recording (the count and skipNext_ are the creator's).
    NetLink();
    // `source`'s recording flags, a fresh record stream (the file is not shared).
    NetLink(const NetLink& source);
};

class LoopbackLink final : public NetLink {
public:
    LoopbackLink();
    // A closed file of its own, a copy of every queued stream and the same peer (counted).
    LoopbackLink(const LoopbackLink& other);
    ~LoopbackLink() override;

    int OnShutdown() override;                  // torn down and built again in place
    LoopbackLink* Clone() override;
    bool Connected() override;                  // the peer is there, not detached
    bool Alive() override;
    bool HasIncoming() override;
    int IncomingCount() override;
    bool Send(CStream* stream) override;        // a copy on the peer's queue
    bool Receive(CMemoryStream* stream) override;     // the front of the queue
    void Disconnect() override;

    // Two links joined to each other; the first goes to the player's link, the second is returned.
    static LoopbackLink* CreatePair(CWorldHandle* player);

    StreamQueue queue_;
    LoopbackLink* peer_;         // counted

private:
    void DestroyQueue();
};

class ReplayLink final : public NetLink {
public:

    bool Connected() override { return false; }

    FileStream replay_;          // the recording played back
};

// A link that frames its messages in a byte inbox.
class FramedLink : public NetLink {
public:
    ~FramedLink() override;

    bool HasIncoming() override;                // a whole message in the inbox
    int IncomingCount() override;
    bool Send(CStream* stream) override;        // framed, then SendBytes
    bool Receive(CMemoryStream* stream) override;
    virtual bool SendBytes(const void* data, std::uint32_t size) = 0;

    // `size` bytes appended at the end of the inbox.
    void Append(const void* data, std::uint32_t size);
    // `out` emptied, then given everything in the inbox, which is emptied.
    void TakeAll(CStream* out);

    CMemoryStream inbox_;        // received bytes: {uint32 length, bytes} per message

protected:
    FramedLink();
    // An inbox of the source's size.
    FramedLink(const FramedLink& other);
};

// The per-player driver: a framed link over a network connection's reliable streams.
class DriverLink final : public FramedLink {
public:
    DriverLink();
    // The same player (counted).
    DriverLink(const DriverLink& other);
    ~DriverLink() override;

    int OnShutdown() override;                  // destroyed and built again in place
    DriverLink* Clone() override;
    bool Connected() override;                  // the player is there, not detached
    bool Alive() override { return true; }
    void Pump() override;                       // the incoming reliable bytes to the inbox
    void Disconnect() override;                 // the unread bytes given back
    bool SendBytes(const void* data, std::uint32_t size) override;

    RefPtrTo<net::Connection> player_;
};

// A node of the session's driver lists (STLport list<RefPtr>).
struct DriverNode {
    DriverNode* next;
    DriverNode* prev;
    RefPtrTo<DriverLink> value;

    DriverLink* Driver() const { return value.Get(); }
};

// The network session (made by CWorld::OpenSession): a player source with two lists of drivers.
class DriverSession final : public CPlayerSource {
public:
    DriverSession();
    ~DriverSession() override;

    int OnShutdown() override;          // both lists emptied, new sentinels
    DriverSession* Clone() override;    // drivers shared, holders counted
    bool Query() override { return true; }
    void TakeNew(JoinedLinks* joined) override;   // the live holders' links appended

    DriverNode* drivers_;        // list<RefPtr> (owning)
    DriverNode* holders_;        // list of plain holders (released, not detached)

    // A driver for every new connection of `session` (the game client or server) with something queued;
    // then each driver pumped, and those whose player is gone erased.
    void PumpDrivers(net::SessionBase* session);
    // Both lists emptied, then one driver for `player`, started and drained.
    DriverLink* Start(net::Connection* player);
};

} // namespace netlink
