#pragma once

// Message links over streams: the endpoints a player's messages go through,
// and the pump that keeps a driver link per remote player. Names are
// provisional; see docs/notes/network.md and
// docs/notes/cleanup-network.md.
// Class map (vtables):
//   FileStream (0x1C): a CStream over an MFC CFile. Save files go through it
//             (world_game.cpp, ui_saveload.cpp), and the links record their messages with it.
//   NetLink (0x28, abstract): the recording flags and the recording file.
//   LoopbackLink (0x30): two of them joined to each other, the single-player
//             link between the local client and the server. Send puts a copy of the stream on the
//             peer's queue, Receive takes the front of its own.
//   ReplayLink (0x44): plays back a recording message by message.
//   FramedLink (0x50, abstract): the received bytes in a memory stream, each message a
//             4-byte length and its bytes; Send frames the message and hands it to slot 13.
//   DriverLink (0x54): a FramedLink over a network connection's reliable streams.
//   Is another FramedLink whose constructors are dead code; its own
//             slots are listed in config/dead_code.csv.
// The links are real C++ classes, created with net::NewCounted.

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

// A CStream over a file. The CFile is raw storage (MFC): the
// constructor and destructor build and tear it down explicitly.
class FileStream : public CStream {
public:
    // A closed file.
    FileStream();
    // A fresh stream; `other` is not read.
    FileStream(const FileStream& other);
    // The destructor: the CFile destroyed (closing an open file).
    ~FileStream() override;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // Virtual 1 (a short read clears ok_)
    void Write(const void* buffer, std::uint32_t size) override;
    void Seek(std::uint32_t position) override;                        // Virtual 3 (SeekFrom)
    std::uint32_t Size() override;                                     // Virtual 4 (the file's length)
    std::uint32_t Position() override;
    void Clear() override;                                             // Virtual 6 (Restart)
    // 7: CStream::CopyFrom

    // The body of slot 3: seeks from the start; returns the CFile's result.
    long SeekFrom(long position);
    // The body of slot 6: ok_ again, back to the start; returns the seek's result.
    long Restart();

    // The direction byte, then the CFile's virtual Open (its own vtable, slot 8); ok_ tells whether
    // it opened (inlined by its users).
    bool Open(const char* path, unsigned flags, std::uint8_t reading) {
        reading_ = reading;
        ok_ = File()->Open(path, flags, nullptr) != 0;
        return ok_ != 0;
    }
    // The CFile's virtual Close.
    void Close() { File()->Close(); }

    // The CFile called through its own vtable (MFC 4.2 slots: 3 GetPosition, 8 Open, 10 Seek,
    // 12 GetLength, 13 Read, 14 Write, 19 Close).
    mfc::CFileVirtuals* File() { return file_->Virtuals(); }

    static constexpr unsigned kOpenRead = 0;         // CFile::modeRead
    static constexpr unsigned kOpenWrite = 0x1001;   // CFile::modeCreate | modeWrite

    lib::InPlace<mfc::CFile> file_;   // constructed and destroyed by the stream's constructor and destructor
};

// The loopback link's queue: an STLport list of memory streams (0x30-byte nodes from the node
// pool: next, prev, the stream).
using StreamQueue = stlp::list<CMemoryStream>;
using QueueNode = StreamQueue::Node;

// The link base.
class NetLink : public CRefObject {
public:
    // The deleting destructor: the record stream destroyed.
    ~NetLink() override;

    // 1: Serialize (empty); 2 / 3 pure.
    virtual bool Connected() = 0;
    virtual bool Alive() = 0;                   // 5: something to read, or connected
    virtual bool HasIncoming() = 0;
    virtual int IncomingCount();                // Virtual 7 (1 when HasIncoming)
    virtual bool Send(CStream* stream);         // Virtual 8 (dropped: true)
    virtual bool Receive(CMemoryStream* stream);      // Virtual 9 (records the stream: true)
    virtual void Pump() {}                      // Virtual 10 (empty)
    virtual void Disconnect() = 0;
    virtual int Unknown12() { return 0; }

    // Starts recording into a new file `path`; the first message is dropped.
    void OpenRecording(const char* path);
    // Closes the file and stops recording.
    void CloseRecording();
    // What slot 9 does with a message it took in while recording: the first after OpenRecording is
    // dropped, the others written to the file with their size.
    void Record(CStream* stream);

    std::uint8_t recording_;
    std::uint8_t skipNext_;      // the next recorded message is dropped (set by OpenRecording)
    std::uint8_t unused_0A[2];
    FileStream record_;

protected:
    // The record stream, not recording (the count and +9 are the creator's).
    NetLink();
    // The base of a copy as the clones inline it: `source`'s recording flags, a fresh record stream (the
    // file is not shared).
    NetLink(const NetLink& source);
};

// The loopback link.
class LoopbackLink final : public NetLink {
public:
    // The base, an empty queue, no peer.
    LoopbackLink();
    // Clone's copy: the recording flags, a closed file of its own, a copy of every queued stream and the same
    // peer (counted).
    LoopbackLink(const LoopbackLink& other);
    // The deleting destructor: the peer released, the queue destroyed, then the base.
    ~LoopbackLink() override;

    // 10, 12: NetLink's.
    int OnShutdown() override;                  // Virtual 2 (torn down and built again in place)
    LoopbackLink* Clone() override;
    bool Connected() override;                  // Virtual 4 (the peer is there, not detached)
    bool Alive() override;
    bool HasIncoming() override;
    int IncomingCount() override;
    bool Send(CStream* stream) override;        // Virtual 8 (a copy on the peer's queue)
    bool Receive(CMemoryStream* stream) override;     // Virtual 9 (the front of the queue)
    void Disconnect() override;

    // Two links joined to each other; the first goes
    // to the player's link (+8), the second is returned.
    static LoopbackLink* __stdcall CreatePair(CWorldHandle* player);

    StreamQueue queue_;
    LoopbackLink* peer_;         // counted

private:
    void DestroyQueue();
};

// The replay link.
class ReplayLink final : public NetLink {
public:
    // The base (not recording), then the second file stream.
    ReplayLink();
    // Clone's copy: the recording flags and two closed files of its own.
    ReplayLink(const ReplayLink& other);
    // The deleting destructor: both file streams destroyed (the replay's first).
    ~ReplayLink() override;

    // 7, 8, 10, 12: NetLink's.
    int OnShutdown() override;                  // Virtual 2 (destroyed and built again in place)
    ReplayLink* Clone() override;
    bool Connected() override { return false; }
    bool Alive() override;                      // Virtual 5 (the recording has more to read)
    bool HasIncoming() override;                // Virtual 6 (the same code)
    bool Receive(CMemoryStream* stream) override;
    void Disconnect() override;                 // Virtual 11 (closes the recording)

    // A replay link reading the recording `path`.
    static ReplayLink* __stdcall Create(const char* path);

    FileStream replay_;          // the recording played back (reading)
};

// The framed link (abstract, 0x50 bytes).
class FramedLink : public NetLink {
public:
    // The destructor: the inbox's buffer freed, then the base.
    ~FramedLink() override;

    // 2-5, 11 pure; 10, 12 NetLink's.
    bool HasIncoming() override;                // Virtual 6 (a whole message in the inbox)
    int IncomingCount() override;
    bool Send(CStream* stream) override;        // Virtual 8 (framed, then SendBytes)
    bool Receive(CMemoryStream* stream) override;     // Virtual 9 (a memory stream)
    virtual bool SendBytes(const void* data, std::uint32_t size) = 0;

    // `size` bytes appended at the end of the inbox.
    void Append(const void* data, std::uint32_t size);
    // `out` emptied, then given everything in the inbox, which is emptied.
    void TakeAll(CStream* out);

    CMemoryStream inbox_;        // received bytes: {uint32 length, bytes} per message

protected:
    // The base and an empty inbox.
    FramedLink();
    // A copy's: the base's copy, an inbox of the source's size.
    FramedLink(const FramedLink& other);
};

// The per-player driver: a framed link over a network
// connection's reliable streams.
class DriverLink final : public FramedLink {
public:
    // The base, an empty inbox, no player.
    DriverLink();
    // Clone's copy: the framed link's, then the same player (counted).
    DriverLink(const DriverLink& other);
    // The deleting destructor: disconnected (called directly), then the framed link's destructor.
    ~DriverLink() override;

    // 6-9 FramedLink's; 12 NetLink's.
    int OnShutdown() override;                  // Virtual 2 (destroyed and built again in place)
    DriverLink* Clone() override;
    bool Connected() override;                  // Virtual 4 (the player is there, not detached)
    bool Alive() override { return true; }
    void Pump() override;                       // Virtual 10 (the incoming reliable bytes to the inbox)
    void Disconnect() override;                 // Virtual 11 (the unread bytes given back)
    bool SendBytes(const void* data, std::uint32_t size) override;

    RefPtrTo<net::Connection> player_;   // counted
};

// A node of the session's driver lists (STLport list<RefPtr>, 12 bytes from the node pool).
struct DriverNode {
    DriverNode* next;
    DriverNode* prev;
    RefPtr value;

    DriverLink* Driver() const { return static_cast<DriverLink*>(value.ptr); }
};

// The network session (0x10 bytes; CWorld + 0x60, made by CWorld::OpenSession; a real
// class): a player source (player_source.h) with two lists
// of drivers. Its lifetime is in world_sessions.cpp.
class DriverSession final : public CPlayerSource {
public:
    // Both lists empty.
    DriverSession();
    // The deleting destructor. Both lists destroyed (holders released, drivers destroyed).
    ~DriverSession() override;

    int OnShutdown() override;          // Virtual 2 (both lists emptied, new sentinels)
    DriverSession* Clone() override;    // Virtual 3 (drivers shared, holders counted)
    bool Query() override { return true; }
    void TakeNew(JoinedLinks* joined) override;   // The live holders' links appended (player_sources.cpp)

    DriverNode* drivers_;        // list<RefPtr> (owning)
    DriverNode* holders_;        // list of plain holders (released, not detached)

    // A driver for every new connection of `session` (the game client or server)
    // with something queued; then each driver pumped, and those whose player is gone erased.
    void PumpDrivers(net::SessionBase* session);
    // Both lists emptied, then one driver for `player`, started and drained.
    DriverLink* Start(net::Connection* player);
};

} // namespace netlink
