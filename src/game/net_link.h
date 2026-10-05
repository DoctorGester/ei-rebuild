#pragma once

// Message links over streams (00477E60-0047BC10): the endpoints a player's messages go through.
// Names are provisional; see docs/notes/unmapped_410_470.md ("Tenth round").
//
// Class map (vtables):
//   0073C8D8  FileStream (0x1C): a CStream (0073BDB0) over an MFC CFile at +0xC. Slots: 0 deleting
//             destructor 00477F90, 1 Read 00477F00, 2 Write 00477F30, 3 Seek 00477F50, 4 Size
//             00477F70, 5 Position 00477F80, 6 Rewind 00477EE0, 7 CStream's copy 004A4D40.
//             Constructor 00478470 (also inlined), destructor body 00477FB0. Save files go through
//             it (world_game.cpp), and the links record their messages with it.
//   0073C8A4  NetLink (0x28, abstract, on 0073BB58): +8 recording, +9 skip the next message, the
//             recording file at +0xC. Slots: 0 deleting destructor 00478030 (body 00478050),
//             1 empty, 2 reset, 3 clone, 4 connected, 5 alive, 6 has incoming (pure), 7 incoming
//             count 00478000 (1 when slot 6), 8 Send 00478100 (drops it: true), 9 Record 00478110,
//             10 Pump (empty), 11 Disconnect (pure), 12 00478020 (0). Constructor 00477E60
//             (net_session_pump.cpp); recording Open 004780B0 / Close 004780E0.
//   0073C8F8  LoopbackLink (0x30): two of them joined to each other (004782E0), the single-player
//             link between the local client and the server. +0x28 the incoming queue (STLport
//             list<CMemoryStream>), +0x2C the peer. Send puts a copy of the stream on the peer's
//             queue, Receive takes the front of its own.
//   0073C92C  ReplayLink (0x44): plays back a recording (00478960 opens it): +0x28 a second file
//             stream, read message by message (004A51C0) by Receive. Connected 0 (00478C20), alive
//             and has-incoming while the file has more, Disconnect closes the file.
//   0073C9C8  FramedLink (0x50, abstract): the received bytes in a memory stream at +0x28, each
//             message a 4-byte length and its bytes. Slots 6-9 (has message 0047B9F0, count
//             0047BA40, Send 0047BAD0, Receive 0047BB70) and two members (append 0047B980, take
//             everything 0047B9B0); Send frames the message and hands it to slot 13.
//   0073C990  DriverLink (0x54): a FramedLink over a network connection's reliable streams, the
//             player at +0x50 (counted). Pump (slot 10) moves the connection's incoming reliable
//             bytes into the inbox, slot 13 writes to its outgoing one, Disconnect (slot 11) puts
//             the unread bytes back in front of the incoming stream. Constructor 0047A030
//             (net_session_pump.cpp).
//   0073CA10 is another FramedLink whose constructors (0047CAA0, 0047CFA0) are dead code; its own
//             slots are listed in config/dead_code.csv.

#include "core_refcount.h"
#include "object.h"

#include <cstddef>
#include <cstdint>

namespace netlink {

constexpr std::uintptr_t kFileStreamVtable = 0x0073C8D8;
constexpr std::uintptr_t kLinkVtable = 0x0073C8A4;
constexpr std::uintptr_t kLoopbackVtable = 0x0073C8F8;
constexpr std::uintptr_t kNodeStreamVtable = 0x0073C960;   // the queued streams' CMemoryStream vtable
constexpr std::uintptr_t kReplayVtable = 0x0073C92C;
constexpr std::uintptr_t kFramedVtable = 0x0073C9C8;
constexpr std::uintptr_t kDriverVtable = 0x0073C990;

// A CStream over a file (vtable 0073C8D8).
struct FileStream {
    void* vtable;
    std::uint8_t reading;        // +0x04 CStream's direction byte (1 reading, 0 writing)
    std::uint8_t ok;             // +0x05 cleared by a short read
    std::uint8_t unknown_06[2];
    std::uint32_t limit;         // +0x08 2000000000
    alignas(4) std::uint8_t file[0x10];   // +0x0C mfc::CFile (raw: the naked forwarders take pointers)
};
static_assert(sizeof(FileStream) == 0x1C);
static_assert(offsetof(FileStream, file) == 0xC);

// A node of the incoming queue (0x30 bytes from the node pool): next, prev, the stream.
struct QueueNode {
    QueueNode* next;
    QueueNode* prev;
    CMemoryStream stream;
};
static_assert(sizeof(QueueNode) == 0x30);

// The link base (vtable 0073C8A4).
struct NetLink {
    void* vtable;
    std::int16_t refs;           // +0x04 CRefObject's count
    std::uint8_t flags6;
    std::uint8_t flags7;         // +0x07 0x80: detached
    std::uint8_t recording;      // +0x08
    std::uint8_t skipNext;       // +0x09 the next recorded message is dropped (set by Open)
    std::uint8_t unknown_0A[2];
    FileStream record;           // +0x0C
};
static_assert(sizeof(NetLink) == 0x28);
static_assert(offsetof(NetLink, record) == 0xC);

// The loopback link (vtable 0073C8F8).
struct LoopbackLink {
    NetLink base;
    QueueNode* queue;            // +0x28 the list's header node
    CRefObject* peer;            // +0x2C counted
};
static_assert(sizeof(LoopbackLink) == 0x30);
static_assert(offsetof(LoopbackLink, queue) == 0x28);

// The replay link (vtable 0073C92C).
struct ReplayLink {
    NetLink base;
    FileStream replay;           // +0x28 the recording played back (reading)
};
static_assert(sizeof(ReplayLink) == 0x44);
static_assert(offsetof(ReplayLink, replay) == 0x28);

// The framed link (vtable 0073C9C8, abstract).
struct FramedLink {
    NetLink base;
    CMemoryStream inbox;         // +0x28 received bytes: {uint32 length, bytes} per message
};
static_assert(sizeof(FramedLink) == 0x50);
static_assert(offsetof(FramedLink, inbox) == 0x28);

// The per-player driver (vtable 0073C990).
struct DriverLink {
    FramedLink base;
    CRefObject* player;          // +0x50 a net::Connection (counted)
};
static_assert(sizeof(DriverLink) == 0x54);
static_assert(offsetof(DriverLink, player) == 0x50);

// FileStream (0073C8D8).
FileStream* __fastcall FileStreamConstruct(FileStream* self);                                   // 00478470
void __fastcall FileStreamDestruct(FileStream* self);                                           // 00477FB0
void* __fastcall FileStreamDeletingDestructor(FileStream* self, void*, unsigned flags);         // 00477F90 (slot 0)
unsigned __fastcall FileStreamRead(FileStream* self, void*, void* buffer, unsigned count);      // 00477F00 (slot 1)
void __fastcall FileStreamWrite(FileStream* self, void*, const void* buffer, unsigned count);   // 00477F30 (slot 2)
long __fastcall FileStreamSeek(FileStream* self, void*, long position);                         // 00477F50 (slot 3)
unsigned __fastcall FileStreamSize(FileStream* self);                                           // 00477F70 (slot 4)
unsigned __fastcall FileStreamPosition(FileStream* self);                                       // 00477F80 (slot 5)
long __fastcall FileStreamRewind(FileStream* self);                                             // 00477EE0 (slot 6)

// NetLink (0073C8A4).
void __fastcall LinkDestruct(NetLink* self);                                                    // 00478050
void* __fastcall LinkDeletingDestructor(NetLink* self, void*, unsigned flags);                  // 00478030 (slot 0)
int __fastcall LinkIncomingCount(NetLink* self);                                                // 00478000 (slot 7)
bool __fastcall LinkSend(NetLink* self, void*, void* stream);                                   // 00478100 (slot 8)
bool __fastcall LinkRecord(NetLink* self, void*, void* stream);                                 // 00478110 (slot 9)
void __fastcall LinkOpenRecording(NetLink* self, void*, const char* path);                      // 004780B0
void __fastcall LinkCloseRecording(NetLink* self);                                              // 004780E0

// LoopbackLink (0073C8F8).
LoopbackLink* __fastcall LoopbackCreatePair(void* unused, void*, std::uint8_t* player);         // 004782E0
void __fastcall LoopbackDestruct(LoopbackLink* self);                                           // 004787A0
void* __fastcall LoopbackDeletingDestructor(LoopbackLink* self, void*, unsigned flags);         // 00478780 (slot 0)
void __fastcall LoopbackReset(LoopbackLink* self);                                              // 00478610 (slot 2)
LoopbackLink* __fastcall LoopbackClone(LoopbackLink* self);                                     // 004784D0 (slot 3)
int __fastcall LoopbackConnected(LoopbackLink* self);                                           // 00478710 (slot 4)
int __fastcall LoopbackAlive(LoopbackLink* self);                                               // 00478730 (slot 5)
bool __fastcall LoopbackHasIncoming(LoopbackLink* self);                                        // 00478750 (slot 6)
int __fastcall LoopbackIncomingCount(LoopbackLink* self);                                       // 00478760 (slot 7)
bool __fastcall LoopbackSend(LoopbackLink* self, void*, void* stream);                          // 00478220 (slot 8)
bool __fastcall LoopbackReceive(LoopbackLink* self, void*, void* stream);                       // 00478270 (slot 9)
void __fastcall LoopbackDisconnect(LoopbackLink* self);                                         // 00478140 (slot 11)

// ReplayLink (0073C92C).
ReplayLink* __fastcall ReplayCreate(void* unused, void*, const char* path);                     // 00478960
void __fastcall ReplayDestruct(ReplayLink* self);                                               // 00478CC0
void* __fastcall ReplayDeletingDestructor(ReplayLink* self, void*, unsigned flags);             // 00478CA0 (slot 0)
void __fastcall ReplayReset(ReplayLink* self);                                                  // 00478B10 (slot 2)
ReplayLink* __fastcall ReplayClone(ReplayLink* self);                                           // 00478A50 (slot 3)
int __fastcall ReplayAlive(ReplayLink* self);                                                   // 00478C30 (slot 5)
int __fastcall ReplayHasIncoming(ReplayLink* self);                                             // 00478C50 (slot 6)
bool __fastcall ReplayReceive(ReplayLink* self, void*, void* stream);                           // 00478C70 (slot 9)
void __fastcall ReplayDisconnect(ReplayLink* self);                                             // 00478C90 (slot 11)

// FramedLink (0073C9C8).
FileStream* __fastcall FileStreamCopyConstruct(FileStream* self, void*, const FileStream* other); // 00479B10
void __fastcall FramedDestruct(FramedLink* self);                                               // 00479B90
void* __fastcall FramedDeletingDestructor(FramedLink* self, void*, unsigned flags);             // 00479B70 (slot 0)
void __fastcall FramedAppend(FramedLink* self, void*, const void* data, unsigned size);         // 0047B980
void __fastcall FramedTakeAll(FramedLink* self, void*, void* out);                              // 0047B9B0
int __fastcall FramedHasMessage(FramedLink* self);                                              // 0047B9F0 (slot 6)
int __fastcall FramedMessageCount(FramedLink* self);                                            // 0047BA40 (slot 7)
bool __fastcall FramedSend(FramedLink* self, void*, void* stream);                              // 0047BAD0 (slot 8)
bool __fastcall FramedReceive(FramedLink* self, void*, CMemoryStream* stream);                  // 0047BB70 (slot 9)

// DriverLink (0073C990).
void __fastcall DriverDestruct(DriverLink* self);                                               // 00479D40
void* __fastcall DriverDeletingDestructor(DriverLink* self, void*, unsigned flags);             // 00479D20 (slot 0)
void __fastcall DriverReset(DriverLink* self);                                                  // 00479C10 (slot 2)
DriverLink* __fastcall DriverClone(DriverLink* self);                                           // 00479A30 (slot 3)
int __fastcall DriverConnected(DriverLink* self);                                               // 00479780 (slot 4)
void __fastcall DriverPump(DriverLink* self);                                                   // 004797A0 (slot 10)
void __fastcall DriverDisconnect(DriverLink* self);                                             // 00479810 (slot 11)
int __fastcall DriverSendBytes(DriverLink* self, void*, const void* data, unsigned size);       // 004796B0 (slot 13)

} // namespace netlink
