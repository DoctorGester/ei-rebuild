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
// Like the other reference-counted classes the links are __declspec(novtable) and never built by
// C++: our rewrites of the constructor bodies store a table, through which the virtual calls
// dispatch: the compiled tags' at the end (C19, `X::Vtable()`) in place of game.exe's. The slot
// bodies stay registered through `...Hook` shims (docs/rewrite-guide.md rule 4) until C20. The
// slots that are not rewritten are declared only.

#include "core_refcount.h"
#include "core_streams.h"
#include "net_connection.h"
#include "player_source.h"

#include "ei/vcall.h"
#include "lib/compiled_vtable.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

namespace net {
class SessionBase;   // net_classes.h
}

namespace netlink {

// A CStream over a file (vtable 0073C8D8). The CFile is raw storage: the constructor and destructor
// bodies build and tear it down explicitly (rewrite-guide rule 5).
class __declspec(novtable) FileStream : public CStream {
public:
    static void* Vtable();   // the compiled table (below; game.exe's 0073C8D8)

    ~FileStream() override {}                                           // 0: 00477F90 (DeletingDestruct)
    std::uint32_t Read(void* buffer, std::uint32_t size) override;     // 1: 00477F00 (a short read clears ok_)
    std::uint32_t ReadImpl(void* buffer, std::uint32_t size);   // body of the slot above (registered by member pointer)
    void Write(const void* buffer, std::uint32_t size) override;       // 2: 00477F30
    void WriteImpl(const void* buffer, std::uint32_t size);   // body of the slot above (registered by member pointer)
    void Seek(std::uint32_t position) override;                        // 3: 00477F50 (SeekFrom)
    std::uint32_t Size() override;                                     // 4: 00477F70 (the file's length)
    std::uint32_t Position() override;                                 // 5: 00477F80
    void Clear() override;                                             // 6: 00477EE0 (Restart)
    // 7: CStream::CopyFrom (004A4D40)

    // 00478470 (also inlined). CStream's fields, the CFile constructed, the vtable. Returns this.
    FileStream* Construct();
    // 00479B10. The copy constructor: a fresh stream; `other` is not read. Returns this.
    FileStream* CopyConstruct(const FileStream* other);
    // 00477FB0 (also inlined). The CFile destroyed (closing an open file), CStream's vtable left.
    void Destruct();
    // 00477F90 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    FileStream* DeletingDestruct(unsigned flags);
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
    mfc::CFileVirtuals* File() { return reinterpret_cast<mfc::CFileVirtuals*>(file_); }

    static constexpr unsigned kOpenRead = 0;         // CFile::modeRead
    static constexpr unsigned kOpenWrite = 0x1001;   // CFile::modeCreate | modeWrite

    alignas(4) std::uint8_t file_[0x10];   // +0x0C mfc::CFile (constructed in place by Construct)
};
static_assert(offsetof(FileStream, file_) == 0xC);
static_assert(sizeof(FileStream) == 0x1C);

// A node of the loopback link's queue (0x30 bytes from the node pool): next, prev, the stream.
struct QueueNode {
    QueueNode* next;
    QueueNode* prev;
    CMemoryStream stream;
};
static_assert(sizeof(QueueNode) == 0x30);

// The link base (vtable 0073C8A4, 0x28 bytes).
class __declspec(novtable) NetLink : public CRefObject {
public:
    static void* Vtable();   // the compiled table (below; game.exe's 0073C8A4)

    // 0: 00478030 deleting destructor (DeletingDestruct); 1: Serialize (00412580, empty); 2 / 3 pure.
    virtual bool Connected() = 0;               // 4
    virtual bool Alive() = 0;                   // 5: something to read, or connected
    virtual bool HasIncoming() = 0;             // 6
    virtual int IncomingCount();                // 7: 00478000 (1 when HasIncoming)
    virtual bool Send(CStream* stream);         // 8: 00478100 (dropped: true)
    bool SendImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    virtual bool Receive(CStream* stream);      // 9: 00478110 (records the stream: true)
    bool ReceiveImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    virtual void Pump();                        // 10: 00478010 (empty)
    virtual void Disconnect() = 0;              // 11
    virtual int Unknown12();                    // 12: 00478020 (0)

    // 00477E60 (also inlined). The base: 0073BB58, the record stream, 0073C8A4, not recording (the
    // reference count is left as it is). Returns this.
    NetLink* Construct();
    // The base of a copy as the clones inline it: 0073BB58, `source`'s recording flags, a fresh
    // record stream (CopyConstruct: the file is not shared), 0073C8A4.
    void ConstructCopyOf(const NetLink* source);
    // 00478050. The record stream destroyed, the root vtable left (0073C8A4 is not stored first).
    void Destruct();
    // 00478030 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    NetLink* DeletingDestruct(unsigned flags);
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
};
static_assert(offsetof(NetLink, recording_) == 0x08);
static_assert(offsetof(NetLink, record_) == 0x0C);
static_assert(sizeof(NetLink) == 0x28);

// The loopback link (vtable 0073C8F8, 0x30 bytes).
class __declspec(novtable) LoopbackLink : public NetLink {
public:
    static void* Vtable();   // the compiled table (below; game.exe's 0073C8F8)

    // 0: 00478780 deleting destructor (DeletingDestruct); 10, 12: NetLink's.
    int OnShutdown() override;                  // 2: 00478610 (destroyed and built again in place)
    LoopbackLink* Clone() override;             // 3: 004784D0
    bool Connected() override;                  // 4: 00478710 (the peer is there, not detached)
    bool Alive() override;                      // 5: 00478730
    bool HasIncoming() override;                // 6: 00478750
    int IncomingCount() override;               // 7: 00478760
    bool Send(CStream* stream) override;        // 8: 00478220 (a copy on the peer's queue)
    bool SendImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    bool Receive(CStream* stream) override;     // 9: 00478270 (the front of the queue)
    bool ReceiveImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    void Disconnect() override;                 // 11: 00478140

    // 004782E0 (thiscall with an unused this, RET 4). Two links joined to each other; the first goes
    // to the player's link (+8), the second is returned.
    static LoopbackLink* __fastcall CreatePair(void* unused, void*, std::uint8_t* player);
    // 004787A0. The peer released, the queue destroyed, then the record stream; the root vtable left.
    void Destruct();
    // 00478780 (slot 0).
    LoopbackLink* DeletingDestruct(unsigned flags);

    QueueNode* queue_;           // +0x28 the queue's header node (STLport list<CMemoryStream>)
    LoopbackLink* peer_;         // +0x2C counted

private:
    static LoopbackLink* New();
    void DestroyQueue();
};
static_assert(offsetof(LoopbackLink, queue_) == 0x28);
static_assert(offsetof(LoopbackLink, peer_) == 0x2C);
static_assert(sizeof(LoopbackLink) == 0x30);

// The replay link (vtable 0073C92C, 0x44 bytes).
class __declspec(novtable) ReplayLink : public NetLink {
public:
    static void* Vtable();   // the compiled table (below; game.exe's 0073C92C)

    // 0: 00478CA0 deleting destructor (DeletingDestruct); 7, 8, 10, 12: NetLink's.
    int OnShutdown() override;                  // 2: 00478B10 (destroyed and built again in place)
    ReplayLink* Clone() override;               // 3: 00478A50
    bool Connected() override;                  // 4: 00478C20 (false)
    bool Alive() override;                      // 5: 00478C30 (the recording has more to read)
    bool HasIncoming() override;                // 6: 00478C50 (the same code)
    bool Receive(CStream* stream) override;     // 9: 00478C70
    bool ReceiveImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    void Disconnect() override;                 // 11: 00478C90 (closes the recording)

    // 00478960 (thiscall with an unused this, RET 4). A replay link reading the recording `path`.
    static ReplayLink* __fastcall Create(void* unused, void*, const char* path);
    // 00478CC0. Both file streams destroyed (the replay's first), the root vtable left.
    void Destruct();
    // 00478CA0 (slot 0).
    ReplayLink* DeletingDestruct(unsigned flags);

    FileStream replay_;          // +0x28 the recording played back (reading)

private:
    void ConstructInPlace();
};
static_assert(offsetof(ReplayLink, replay_) == 0x28);
static_assert(sizeof(ReplayLink) == 0x44);

// The framed link (vtable 0073C9C8, abstract, 0x50 bytes).
class __declspec(novtable) FramedLink : public NetLink {
public:
    static void* Vtable();   // the compiled table (below; game.exe's 0073C9C8)

    // 0: 00479B70 deleting destructor (DeletingDestruct); 2-5, 11 pure; 10, 12 NetLink's.
    bool HasIncoming() override;                // 6: 0047B9F0 (a whole message in the inbox)
    int IncomingCount() override;               // 7: 0047BA40
    bool Send(CStream* stream) override;        // 8: 0047BAD0 (framed, then SendBytes)
    bool SendImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    bool Receive(CStream* stream) override;     // 9: 0047BB70 (a memory stream)
    bool ReceiveImpl(CStream* stream);   // body of the slot above (registered by member pointer)
    virtual bool SendBytes(const void* data, std::uint32_t size) = 0;   // 13

    // 00479B90 (also inlined). The inbox's buffer freed, then the record stream; the root vtable
    // left (its own vtable is not stored first).
    void Destruct();
    // 00479B70 (slot 0).
    FramedLink* DeletingDestruct(unsigned flags);
    // 0047B980. `size` bytes appended at the end of the inbox.
    void Append(const void* data, std::uint32_t size);
    // 0047B9B0. `out` emptied, then given everything in the inbox, which is emptied.
    void TakeAll(CStream* out);

    CMemoryStream inbox_;        // +0x28 received bytes: {uint32 length, bytes} per message
};
static_assert(offsetof(FramedLink, inbox_) == 0x28);
static_assert(sizeof(FramedLink) == 0x50);

// The per-player driver (vtable 0073C990, 0x54 bytes): a framed link over a network connection's
// reliable streams.
class __declspec(novtable) DriverLink : public FramedLink {
public:
    static void* Vtable();   // the compiled table (below; game.exe's 0073C990)

    // 0: 00479D20 deleting destructor (DeletingDestruct); 6-9 FramedLink's; 12 NetLink's.
    int OnShutdown() override;                  // 2: 00479C10 (destroyed and built again in place)
    DriverLink* Clone() override;               // 3: 00479A30
    bool Connected() override;                  // 4: 00479780 (the player is there, not detached)
    bool Alive() override;                      // 5: 00479D10 (true)
    void Pump() override;                       // 10: 004797A0 (the incoming reliable bytes to the inbox)
    void Disconnect() override;                 // 11: 00479810 (the unread bytes given back)
    bool SendBytes(const void* data, std::uint32_t size) override;   // 13: 004796B0

    // 0047A030. The base, an empty inbox, no player. Returns this.
    DriverLink* Construct();
    // 00479D40. Disconnected (called directly), then the framed link's destructor body inlined.
    void Destruct();
    // 00479D20 (slot 0).
    DriverLink* DeletingDestruct(unsigned flags);

    net::Connection* player_;    // +0x50 counted
};
static_assert(offsetof(DriverLink, player_) == 0x50);
static_assert(sizeof(DriverLink) == 0x54);

// A node of the session's driver lists (STLport list<RefPtr>, 12 bytes from the node pool).
struct DriverNode {
    DriverNode* next;
    DriverNode* prev;
    RefPtr value;

    DriverLink* Driver() const { return static_cast<DriverLink*>(value.ptr); }
};

// The network session (vtable 0073F3A8, 0x10 bytes; CWorld + 0x60, made by CWorld::OpenSession
// 0055F410): a player source (player_source.h) with two lists of drivers. Its lifetime slots are in
// world_sessions.cpp.
class __declspec(novtable) DriverSession : public CPlayerSource {
public:
    static void* Vtable();   // the compiled table (below; game.exe's 0073F3A8)

    // 0: 0055F790 (DeletingDestruct); 4: 0047A220 (AL 1); 5: the base's; 6: 0047A230 (the live
    // holders' links appended to the list).
    int OnShutdown() override;          // 2: 0055F6B0 (both lists emptied; built again in place)
    DriverSession* Clone() override;    // 3: 0055F550 (drivers shared, holders counted)
    void TakeNew(void* joined) override;   // 6: 0047A230 (player_sources.cpp)
    void TakeNewImpl(void* joined);   // body of the slot above (registered by member pointer)

    // The constructor as 0055F410 inlines it on `memory` (0x10 bytes of operator new, the count
    // already cleared): both lists empty. Returns the session.
    static DriverSession* ConstructAt(void* memory);
    // 0055F7B0. Both lists destroyed (holders released, drivers destroyed), the root vtable left.
    void Destruct();
    // 0055F790 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    DriverSession* DeletingDestruct(unsigned flags);

    DriverNode* drivers_;        // +0x08 list<RefPtr> (owning)
    DriverNode* holders_;        // +0x0C list of plain holders (released, not detached)

    // 00479E30 (RET 4). A driver for every new connection of `session` (the game client or server)
    // with something queued; then each driver pumped, and those whose player is gone erased.
    void PumpDrivers(net::SessionBase* session);
    // 0047A080 (RET 4). Both lists emptied, then one driver for `player`, started and drained.
    DriverLink* Start(net::Connection* player);
};
static_assert(offsetof(DriverSession, drivers_) == 0x08);
static_assert(offsetof(DriverSession, holders_) == 0x0C);

// ---- The compiled tables --------------------------------------------------------------------------

// The file stream's, the links' and the driver session's compiled tables (C19, lib/compiled_vtable.h) in place of
// game.exe's: a tag per class whose slots call the bodies game.exe's table reached (the class's members called
// qualified, the `...Impl` bodies; the short ones written out, the pure ones the compiler CRT's `_purecall` as
// game.exe's 006E73EB), a derived link's tag deriving from its base's.
extern "C" int __cdecl _purecall();

class FileStreamClass final : public ei::CompiledTag<FileStreamClass> {
public:
    virtual void* DeleteScalar(unsigned flags) { return Object()->DeletingDestruct(flags); }   // 0: 00477F90
    virtual std::uint32_t Read(void* buffer, std::uint32_t size) { return Object()->ReadImpl(buffer, size); }   // 1: 00477F00
    virtual void Write(const void* buffer, std::uint32_t size) { Object()->WriteImpl(buffer, size); }   // 2: 00477F30
    virtual long Seek(std::uint32_t position) { return Object()->SeekFrom(static_cast<long>(position)); }   // 3: 00477F50
    virtual std::uint32_t Size() { return Object()->FileStream::Size(); }           // 4: 00477F70
    virtual std::uint32_t Position() { return Object()->FileStream::Position(); }   // 5: 00477F80
    virtual long Clear() { return Object()->Restart(); }                             // 6: 00477EE0
    virtual void CopyFrom(CStream* source) { Object()->CopyFromImpl(source); }       // 7: 004A4D40

private:
    FileStream* Object() { return reinterpret_cast<FileStream*>(this); }
};

class NetLinkClass : public ei::CompiledTag<NetLinkClass> {
public:
    virtual void* DeleteScalar(unsigned flags) { return As<NetLink>()->DeletingDestruct(flags); }   // 0: 00478030
    virtual void Serialize(ChunkArchive*) {}                                                        // 1: 00412580
    virtual int OnShutdown() { return _purecall(); }                                                // 2: pure
    virtual CRefObject* Clone() {                                                                   // 3: pure
        _purecall();
        return nullptr;
    }
    virtual bool Connected() { return _purecall() != 0; }     // 4: pure
    virtual bool Alive() { return _purecall() != 0; }         // 5: pure
    virtual bool HasIncoming() { return _purecall() != 0; }   // 6: pure
    virtual int IncomingCount() { return As<NetLink>()->NetLink::IncomingCount(); }   // 7: 00478000
    virtual bool Send(CStream* stream) { return As<NetLink>()->SendImpl(stream); }     // 8: 00478100
    virtual bool Receive(CStream* stream) { return As<NetLink>()->ReceiveImpl(stream); }   // 9: 00478110
    virtual void Pump() {}                      // 10: 00478010 (empty)
    virtual void Disconnect() { _purecall(); }  // 11: pure
    virtual int Unknown12() { return 0; }       // 12: 00478020

protected:
    template <typename T>
    T* As() { return reinterpret_cast<T*>(this); }
};

class LoopbackLinkClass final : public NetLinkClass {
public:
    static void* Vtable() { return ei::CompiledVtable<LoopbackLinkClass>(); }

    void* DeleteScalar(unsigned flags) override { return As<LoopbackLink>()->DeletingDestruct(flags); }   // 0: 00478780
    int OnShutdown() override { return As<LoopbackLink>()->LoopbackLink::OnShutdown(); }   // 2: 00478610
    CRefObject* Clone() override { return As<LoopbackLink>()->LoopbackLink::Clone(); }     // 3: 004784D0
    bool Connected() override { return As<LoopbackLink>()->LoopbackLink::Connected(); }    // 4: 00478710
    bool Alive() override { return As<LoopbackLink>()->LoopbackLink::Alive(); }            // 5: 00478730
    bool HasIncoming() override { return As<LoopbackLink>()->LoopbackLink::HasIncoming(); }   // 6: 00478750
    int IncomingCount() override { return As<LoopbackLink>()->LoopbackLink::IncomingCount(); }   // 7: 00478760
    bool Send(CStream* stream) override { return As<LoopbackLink>()->SendImpl(stream); }         // 8: 00478220
    bool Receive(CStream* stream) override { return As<LoopbackLink>()->ReceiveImpl(stream); }   // 9: 00478270
    void Disconnect() override { As<LoopbackLink>()->LoopbackLink::Disconnect(); }               // 11: 00478140
};

class ReplayLinkClass final : public NetLinkClass {
public:
    static void* Vtable() { return ei::CompiledVtable<ReplayLinkClass>(); }

    void* DeleteScalar(unsigned flags) override { return As<ReplayLink>()->DeletingDestruct(flags); }   // 0: 00478CA0
    int OnShutdown() override { return As<ReplayLink>()->ReplayLink::OnShutdown(); }   // 2: 00478B10
    CRefObject* Clone() override { return As<ReplayLink>()->ReplayLink::Clone(); }     // 3: 00478A50
    bool Connected() override { return false; }                                        // 4: 00478C20
    bool Alive() override { return As<ReplayLink>()->ReplayLink::Alive(); }            // 5: 00478C30
    bool HasIncoming() override { return As<ReplayLink>()->ReplayLink::HasIncoming(); }   // 6: 00478C50
    bool Receive(CStream* stream) override { return As<ReplayLink>()->ReceiveImpl(stream); }   // 9: 00478C70
    void Disconnect() override { As<ReplayLink>()->ReplayLink::Disconnect(); }                 // 11: 00478C90
};

class FramedLinkClass : public NetLinkClass {
public:
    static void* Vtable() { return ei::CompiledVtable<FramedLinkClass>(); }

    void* DeleteScalar(unsigned flags) override { return As<FramedLink>()->DeletingDestruct(flags); }   // 0: 00479B70
    bool HasIncoming() override { return As<FramedLink>()->FramedLink::HasIncoming(); }        // 6: 0047B9F0
    int IncomingCount() override { return As<FramedLink>()->FramedLink::IncomingCount(); }     // 7: 0047BA40
    bool Send(CStream* stream) override { return As<FramedLink>()->SendImpl(stream); }         // 8: 0047BAD0
    bool Receive(CStream* stream) override { return As<FramedLink>()->ReceiveImpl(stream); }   // 9: 0047BB70
    virtual bool SendBytes(const void*, std::uint32_t) { return _purecall() != 0; }            // 13: pure
};

class DriverLinkClass final : public FramedLinkClass {
public:
    static void* Vtable() { return ei::CompiledVtable<DriverLinkClass>(); }

    void* DeleteScalar(unsigned flags) override { return As<DriverLink>()->DeletingDestruct(flags); }   // 0: 00479D20
    int OnShutdown() override { return As<DriverLink>()->DriverLink::OnShutdown(); }   // 2: 00479C10
    CRefObject* Clone() override { return As<DriverLink>()->DriverLink::Clone(); }     // 3: 00479A30
    bool Connected() override { return As<DriverLink>()->DriverLink::Connected(); }    // 4: 00479780
    bool Alive() override { return true; }                                             // 5: 00479D10
    void Pump() override { As<DriverLink>()->DriverLink::Pump(); }                     // 10: 004797A0
    void Disconnect() override { As<DriverLink>()->DriverLink::Disconnect(); }         // 11: 00479810
    bool SendBytes(const void* data, std::uint32_t size) override {                    // 13: 004796B0
        return As<DriverLink>()->DriverLink::SendBytes(data, size);
    }
};

class DriverSessionClass final : public ei::CompiledTag<DriverSessionClass> {
public:
    virtual void* DeleteScalar(unsigned flags) { return Object()->DeletingDestruct(flags); }   // 0: 0055F790
    virtual void Serialize(ChunkArchive*) {}                                                   // 1: 00412580
    virtual int OnShutdown() { return Object()->DriverSession::OnShutdown(); }                 // 2: 0055F6B0
    virtual CRefObject* Clone() { return Object()->DriverSession::Clone(); }                   // 3: 0055F550
    virtual bool Query() { return true; }                                                      // 4: 0047A220
    virtual void Pump() {}                                                                     // 5: 0055F190 (empty)
    virtual void TakeNew(void* joined) { Object()->TakeNewImpl(joined); }                      // 6: 0047A230

private:
    DriverSession* Object() { return reinterpret_cast<DriverSession*>(this); }
};

inline void* FileStream::Vtable() { return FileStreamClass::Vtable(); }
inline void* NetLink::Vtable() { return NetLinkClass::Vtable(); }
inline void* LoopbackLink::Vtable() { return LoopbackLinkClass::Vtable(); }
inline void* ReplayLink::Vtable() { return ReplayLinkClass::Vtable(); }
inline void* FramedLink::Vtable() { return FramedLinkClass::Vtable(); }
inline void* DriverLink::Vtable() { return DriverLinkClass::Vtable(); }
inline void* DriverSession::Vtable() { return DriverSessionClass::Vtable(); }

} // namespace netlink
