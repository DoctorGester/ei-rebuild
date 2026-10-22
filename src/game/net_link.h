#pragma once

// Message links over streams (00477E60-0047BC10): the endpoints a player's messages go through,
// and the pump that keeps a driver link per remote player (00479E30 / 0047A080). Names are
// provisional; see docs/notes/network.md and
// docs/notes/cleanup-network.md.
//
// Class map (vtables):
//   0073C8D8  FileStream (0x1C): a CStream over an MFC CFile at +0xC. Save files go through it
//             (world_game.cpp, ui_saveload.cpp), and the links record their messages with it.
//   0073C8A4  NetLink (0x28, abstract, on 0073BB58): the recording flags and the recording file.
//   0073C8F8  LoopbackLink (0x30): two of them joined to each other (004782E0), the single-player
//             link between the local client and the server. Send puts a copy of the stream on the
//             peer's queue, Receive takes the front of its own.
//   0073C92C  ReplayLink (0x44): plays back a recording (00478960 opens it) message by message.
//   0073C9C8  FramedLink (0x50, abstract): the received bytes in a memory stream, each message a
//             4-byte length and its bytes; Send frames the message and hands it to slot 13.
//   0073C990  DriverLink (0x54): a FramedLink over a network connection's reliable streams.
//   0073CA10 is another FramedLink whose constructors (0047CAA0, 0047CFA0) are dead code; its own
//             slots are listed in config/dead_code.csv.
//
// The links are real C++ classes (C28k), created with net::NewCounted.

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
class SessionBase;   // net_classes.h
}
class CWorldHandle;   // world.h

namespace netlink {

// A CStream over a file (game.exe's vtable 0073C8D8). The CFile is raw storage (MFC, C28n): the
// constructor and destructor build and tear it down explicitly.
class FileStream : public CStream {
public:
    // 00478470 (also inlined): a closed file.
    FileStream();
    // 00479B10: a fresh stream; `other` is not read.
    FileStream(const FileStream& other);
    // 00477FB0 (also inlined; deleting: 00477F90): the CFile destroyed (closing an open file).
    ~FileStream() override;
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // 1: 00477F00 (a short read clears ok_)
    void Write(const void* buffer, std::uint32_t size) override;       // 2: 00477F30
    void Seek(std::uint32_t position) override;                        // 3: 00477F50 (SeekFrom)
    std::uint32_t Size() override;                                     // 4: 00477F70 (the file's length)
    std::uint32_t Position() override;                                 // 5: 00477F80
    void Clear() override;                                             // 6: 00477EE0 (Restart)
    // 7: CStream::CopyFrom (004A4D40)

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
    // The CFile's virtual Close (slot 19).
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

// The link base (game.exe's vtable 0073C8A4, 0x28 bytes).
class NetLink : public CRefObject {
public:
    // 00478050 (deleting: 00478030): the record stream destroyed.
    ~NetLink() override;

    // 1: Serialize (00412580, empty); 2 / 3 pure.
    virtual bool Connected() = 0;               // 4
    virtual bool Alive() = 0;                   // 5: something to read, or connected
    virtual bool HasIncoming() = 0;             // 6
    virtual int IncomingCount();                // 7: 00478000 (1 when HasIncoming)
    virtual bool Send(CStream* stream);         // 8: 00478100 (dropped: true)
    virtual bool Receive(CMemoryStream* stream);      // 9: 00478110 (records the stream: true)
    virtual void Pump() {}                      // 10: 00478010 (empty)
    virtual void Disconnect() = 0;              // 11
    virtual int Unknown12() { return 0; }       // 12: 00478020

    // 004780B0. Starts recording into a new file `path`; the first message is dropped.
    void OpenRecording(const char* path);
    // 004780E0. Closes the file and stops recording.
    void CloseRecording();
    // What slot 9 does with a message it took in while recording: the first after OpenRecording is
    // dropped, the others written to the file with their size.
    void Record(CStream* stream);

    std::uint8_t recording_;     // +0x08
    std::uint8_t skipNext_;      // +0x09 the next recorded message is dropped (set by OpenRecording)
    std::uint8_t unused_0A[2];
    FileStream record_;          // +0x0C

protected:
    // 00477E60 (also inlined): the record stream, not recording (the count and +9 are the creator's).
    NetLink();
    // The base of a copy as the clones inline it: `source`'s recording flags, a fresh record stream (the
    // file is not shared).
    NetLink(const NetLink& source);
};

// The loopback link (game.exe's vtable 0073C8F8, 0x30 bytes).
class LoopbackLink final : public NetLink {
public:
    // As 004782E0 inlines it: the base, an empty queue, no peer.
    LoopbackLink();
    // Clone's copy: the recording flags, a closed file of its own, a copy of every queued stream and the same
    // peer (counted).
    LoopbackLink(const LoopbackLink& other);
    // 004787A0 (deleting: 00478780): the peer released, the queue destroyed, then the base.
    ~LoopbackLink() override;

    // 10, 12: NetLink's.
    int OnShutdown() override;                  // 2: 00478610 (torn down and built again in place)
    LoopbackLink* Clone() override;             // 3: 004784D0
    bool Connected() override;                  // 4: 00478710 (the peer is there, not detached)
    bool Alive() override;                      // 5: 00478730
    bool HasIncoming() override;                // 6: 00478750
    int IncomingCount() override;               // 7: 00478760
    bool Send(CStream* stream) override;        // 8: 00478220 (a copy on the peer's queue)
    bool Receive(CMemoryStream* stream) override;     // 9: 00478270 (the front of the queue)
    void Disconnect() override;                 // 11: 00478140

    // 004782E0 (thiscall with an unused this, RET 4). Two links joined to each other; the first goes
    // to the player's link (+8), the second is returned.
    static LoopbackLink* __stdcall CreatePair(CWorldHandle* player);

    StreamQueue queue_;          // +0x28
    LoopbackLink* peer_;         // +0x2C counted

private:
    void DestroyQueue();
};

// The replay link (game.exe's vtable 0073C92C, 0x44 bytes).
class ReplayLink final : public NetLink {
public:
    // As 00478960 / 00478B10 inline it: the base (not recording), then the second file stream.
    ReplayLink();
    // Clone's copy: the recording flags and two closed files of its own.
    ReplayLink(const ReplayLink& other);
    // 00478CC0 (deleting: 00478CA0): both file streams destroyed (the replay's first).
    ~ReplayLink() override;

    // 7, 8, 10, 12: NetLink's.
    int OnShutdown() override;                  // 2: 00478B10 (destroyed and built again in place)
    ReplayLink* Clone() override;               // 3: 00478A50
    bool Connected() override { return false; } // 4: 00478C20
    bool Alive() override;                      // 5: 00478C30 (the recording has more to read)
    bool HasIncoming() override;                // 6: 00478C50 (the same code)
    bool Receive(CMemoryStream* stream) override;     // 9: 00478C70
    void Disconnect() override;                 // 11: 00478C90 (closes the recording)

    // 00478960 (thiscall with an unused this, RET 4). A replay link reading the recording `path`.
    static ReplayLink* __stdcall Create(const char* path);

    FileStream replay_;          // +0x28 the recording played back (reading)
};

// The framed link (game.exe's vtable 0073C9C8, abstract, 0x50 bytes).
class FramedLink : public NetLink {
public:
    // 00479B90 (also inlined; deleting: 00479B70): the inbox's buffer freed, then the base.
    ~FramedLink() override;

    // 2-5, 11 pure; 10, 12 NetLink's.
    bool HasIncoming() override;                // 6: 0047B9F0 (a whole message in the inbox)
    int IncomingCount() override;               // 7: 0047BA40
    bool Send(CStream* stream) override;        // 8: 0047BAD0 (framed, then SendBytes)
    bool Receive(CMemoryStream* stream) override;     // 9: 0047BB70 (a memory stream)
    virtual bool SendBytes(const void* data, std::uint32_t size) = 0;   // 13

    // 0047B980. `size` bytes appended at the end of the inbox.
    void Append(const void* data, std::uint32_t size);
    // 0047B9B0. `out` emptied, then given everything in the inbox, which is emptied.
    void TakeAll(CStream* out);

    CMemoryStream inbox_;        // +0x28 received bytes: {uint32 length, bytes} per message

protected:
    // The base and an empty inbox.
    FramedLink();
    // A copy's (as 00479A30 inlines it): the base's copy, an inbox of the source's size.
    FramedLink(const FramedLink& other);
};

// The per-player driver (game.exe's vtable 0073C990, 0x54 bytes): a framed link over a network
// connection's reliable streams.
class DriverLink final : public FramedLink {
public:
    // 0047A030: the base, an empty inbox, no player.
    DriverLink();
    // Clone's copy: the framed link's, then the same player (counted).
    DriverLink(const DriverLink& other);
    // 00479D40 (deleting: 00479D20): disconnected (called directly), then the framed link's destructor.
    ~DriverLink() override;

    // 6-9 FramedLink's; 12 NetLink's.
    int OnShutdown() override;                  // 2: 00479C10 (destroyed and built again in place)
    DriverLink* Clone() override;               // 3: 00479A30
    bool Connected() override;                  // 4: 00479780 (the player is there, not detached)
    bool Alive() override { return true; }      // 5: 00479D10
    void Pump() override;                       // 10: 004797A0 (the incoming reliable bytes to the inbox)
    void Disconnect() override;                 // 11: 00479810 (the unread bytes given back)
    bool SendBytes(const void* data, std::uint32_t size) override;   // 13: 004796B0

    RefPtrTo<net::Connection> player_;   // +0x50 counted
};

// A node of the session's driver lists (STLport list<RefPtr>, 12 bytes from the node pool).
struct DriverNode {
    DriverNode* next;
    DriverNode* prev;
    RefPtr value;

    DriverLink* Driver() const { return static_cast<DriverLink*>(value.ptr); }
};

// The network session (0x10 bytes; CWorld + 0x60, made by CWorld::OpenSession 0055F410; a real
// class since C28g, game.exe's 0073F3A8 retired): a player source (player_source.h) with two lists
// of drivers. Its lifetime is in world_sessions.cpp.
class DriverSession final : public CPlayerSource {
public:
    // The constructor as 0055F410 inlines it: both lists empty.
    DriverSession();
    // 0055F7B0 (deleting: 0055F790). Both lists destroyed (holders released, drivers destroyed).
    ~DriverSession() override;

    int OnShutdown() override;          // 2: 0055F6B0 (both lists emptied, new sentinels)
    DriverSession* Clone() override;    // 3: 0055F550 (drivers shared, holders counted)
    bool Query() override { return true; }   // 4: 0047A220
    void TakeNew(JoinedLinks* joined) override;   // 6: 0047A230 (the live holders' links appended; player_sources.cpp)

    DriverNode* drivers_;        // +0x08 list<RefPtr> (owning)
    DriverNode* holders_;        // +0x0C list of plain holders (released, not detached)

    // 00479E30 (RET 4). A driver for every new connection of `session` (the game client or server)
    // with something queued; then each driver pumped, and those whose player is gone erased.
    void PumpDrivers(net::SessionBase* session);
    // 0047A080 (RET 4). Both lists emptied, then one driver for `player`, started and drained.
    DriverLink* Start(net::Connection* player);
};

} // namespace netlink
