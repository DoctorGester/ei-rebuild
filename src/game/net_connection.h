#pragma once

// A connection of a network session to one peer (vtable 0073BF38, 0x1168 bytes; constructor
// 00432DA0, created by SessionBase::AddClient 00436DD0) and the object replication it runs.
// Names are provisional; see docs/notes/unmapped_410_470.md ("Third round: connections").
//
// Replication: every replicated object (a CRefObject whose +8 is a version that changes with it)
// is tracked by a ReplicaEntry in one of 11 priority maps. Each update packet carries, highest
// priority first, the entries whose object version differs from the version last sent (a 1 bit,
// type, id, then the object's own serialisation through its slot 4) or whose object went away
// (a 0 bit, type, id). Every packet is remembered in the send history with what it carried, so
// an acknowledgement (or its absence) can mark entries as delivered or resend them. A reliable
// byte stream (+0x112C out, +0x1148 in) travels in front of the entries in chunks of up to 255
// bytes ("headers"). The peer keeps the objects it received in `records` (key type << 16 | id).
#include "core_refcount.h"
#include "net_classes.h"
#include "net_coder.h"

#include <cstddef>
#include <cstdint>

namespace net {

// STLport red-black tree (map / set): header node, element count, comparison (empty).
struct RbTree {
    void* header;              // +0x00 the header node: +4 root, +8 leftmost, +0xC rightmost
    std::uint32_t count;       // +0x04
    std::uint8_t compare;      // +0x08
    std::uint8_t unknown_09[3];
};
static_assert(sizeof(RbTree) == 0xC);

// A free-list of message ids for one message type (16 bytes): returned ids, then the next new id.
struct IdPool {
    std::uint16_t* begin;      // +0x00 vector<uint16_t> of free ids
    std::uint16_t* end;        // +0x04
    std::uint16_t* eos;        // +0x08
    std::uint16_t next;        // +0x0C the last id handed out new
    std::uint16_t unknown_0E;

    // 00435F10. A returned id (the last one) if any, else ++next.
    std::uint16_t Allocate();
};
static_assert(sizeof(IdPool) == 0x10);

// The tracking record of one replicated object (vtable 0073BF4C, 0x14 bytes).
struct ReplicaEntry {
    void* vtable;
    std::uint32_t refHeader;   // +0x04
    std::uint32_t version;     // +0x08 the object's version last sent (1: removal sent)
    std::uint16_t id;          // +0x0C 0 until first sent
    std::uint8_t type;         // +0x0E message type of the object
    std::uint8_t flags;        // +0x0F bit 0 reliable; bits 1..7 priority (0..10)
    CRefObject* object;        // +0x10 counted pointer to the object (null once it is gone)
};
static_assert(sizeof(ReplicaEntry) == 0x14);

// One item of a history entry: what a packet carried for one entry.
struct SentItem {
    SentItem* next;
    SentItem* prev;
    std::uint32_t action;      // +0x08 0 changed, 1 changed (reliable), 2 removed, 3 removed (reliable)
    std::uint32_t version;     // +0x0C the object's version sent
    ReplicaEntry* entry;       // +0x10 counted pointer
};

// A node of the send history (list at +0x10F8): when a packet went out and what it carried.
struct HistoryNode {
    HistoryNode* next;
    HistoryNode* prev;
    std::uint32_t time[2];     // +0x08 the connection's clock (+0x48, a double) when sent
    std::uint16_t sequence;    // +0x10
    std::uint16_t unknown_12;
    SentItem* items;           // +0x14 STLport list sentinel
    std::uint8_t header[0x104];// +0x18 the reliable-stream chunk the packet carried {seq, size, bytes}
    std::uint8_t rolledBack;   // +0x11C
};

// A node of the header list (+0x111C): one chunk of the reliable stream waiting to be sent.
struct HeaderNode {
    HeaderNode* next;
    HeaderNode* prev;
    std::uint8_t data[0x104];  // +0x08 {uint16 sequence, uint8 size, size bytes}
};

// A node of `records` (+0x1100, map<int, Record>): an object received from the peer.
struct RecordNode {
    std::uint32_t color;
    RecordNode* parent;
    RecordNode* left;
    RecordNode* right;
    std::int32_t key;          // +0x10 type << 16 | id
    CRefObject* object;        // +0x14 counted pointer
    std::uint32_t version;     // +0x18 the version last applied
};

struct Connection {
    void* vtable;                       // +0x0000 0073BF38
    std::uint32_t refHeader;            // +0x0004
    std::uint16_t sendSequence;         // +0x0008 starts at GetTickCount()
    std::uint16_t unknown_000A;
    std::uint32_t lastReceived;         // +0x000C sequence of the last packet received
    std::uint32_t unknown_0010;         // +0x0010 0x10000
    std::int32_t* received;             // +0x0014 vector<int>: sequences received recently
    std::int32_t* receivedEnd;
    std::int32_t* receivedEos;
    std::uint32_t connectionId;         // +0x0020
    float roundTrip;                    // +0x0024 average round trip
    float roundTripSquare;              // +0x0028 average of its square
    std::uint8_t address[0x10];         // +0x002C sockaddr_in of the peer
    float sendInterval;                 // +0x003C 0.04
    float sendTimer;                    // +0x0040
    float idleTime;                     // +0x0044
    std::uint32_t clock[2];             // +0x0048 double: time since the connection started
    std::uint32_t updateBudget;         // +0x0050 220: bytes per update
    IdPool idPools[0x100];              // +0x0054 one per message type
    void* freedIds;                     // +0x1054 list of ids to return to the pools later
    float freedTimer;                   // +0x1058
    std::uint8_t bySentId[0xC];         // +0x105C map<type << 16 | id, RefPtr<ReplicaEntry>>
    RbTree byObject;                    // +0x1068 map<CRefObject*, RefPtr<ReplicaEntry>>
    RbTree priorities[11];              // +0x1074 map<ReplicaEntry*, RefPtr<ReplicaEntry>> per priority
    void* history;                      // +0x10F8 list<HistoryNode>
    void* acknowledged;                 // +0x10FC list
    RbTree records;                     // +0x1100 map<int, Record> (RecordNode)
    RbTree attached;                    // +0x110C map<CRefObject*, {uint16 id, uint8 type}>
    std::uint16_t headerSequence;       // +0x1118 sequence of the next reliable chunk
    std::uint16_t unknown_111A;
    void* headers;                      // +0x111C list<HeaderNode>
    void* list_1120;                    // +0x1120 list (0x10C-byte nodes)
    std::uint32_t unknown_1124;
    std::uint16_t unknown_1128;
    std::uint16_t unknown_112A;
    GameBuffer reliableOut;             // +0x112C
    GameBuffer reliableIn;              // +0x1148
    std::uint32_t padding_1164;

    // 00435A00. Builds and sends one update packet of at most `budget` entry bytes. False when
    // entries had to be left out (or the send failed).
    bool SendUpdate(int budget);
    // 00435F30. Reads one entry of an update (created / changed or removed) into `records` and
    // calls the session's handlers (channel 0 created, 1 changed, 2 removed). False when the
    // object could not be created.
    bool ReadRecord(BufferCursor* cursor, std::uint32_t version);
    // 004352F0. Remembers which (type, id) the received object has (map at +0x110C).
    void AttachRecord(CRefObject* object, std::uint32_t type, std::int32_t key);
    // 004355B0. Writes a reference to `object`: a 0 bit, type and id for an object this side
    // replicates (an id is assigned on first use), a 1 bit, type and id for one received from the
    // peer, or a 0 bit and zeros for none.
    void WriteObjectRef(BufferCursor* cursor, CRefObject* object);
    // 00435820. Reads such a reference: the peer's replicated object (created from the type's
    // factory when missing or detached) or one this side sent; null for none.
    CRefObject* ReadObjectRef(BufferCursor* cursor);
    // 00434C20. Writes the acknowledgements: the last sequence received and a mask of the 32
    // before it; forgets sequences more than 32 old.
    void WriteAcks(std::uint8_t** p);
    // 00434990. Makes sure a reliable chunk is queued: a new one of up to min(budget, 255) bytes
    // from the reliable stream when none is waiting, else the oldest waiting one moves to the front.
    void QueueHeader(int budget);
    // 00433F20. A packet was lost: entries it sent unreliably are marked unsent again and its
    // reliable chunk goes back to the front of the queue (once).
    void RollBack(std::uint8_t* historyData);
    // 004346D0. Brings the tracked entries up to date: the session's slot 13 lists what this
    // connection should have (sorted by object); new objects get an entry (Track), entries whose
    // object is no longer listed are dropped (never sent) or marked gone (sent).
    void CollectChanges();
    // 00434110. Starts tracking `item` (an {object, priority} pair): a new entry for the object,
    // kept in byObject and its priority map.
    void Track(Replicated* item);
    // 00433BD0 (net_connection_tick.cpp). Clocks, lost packets, sending, delayed id reuse; false
    // when the peer timed out or the reliable stream overflowed.
    bool Tick(double dt);
    // 00433FA0 (net_connection_tick.cpp). A packet arrived: the round-trip averages take its age;
    // reliable changes it carried count as delivered, removals it carried end their entries (the
    // id goes to the newest freed-id batch).
    void Delivered(std::uint8_t* historyData);
    // 00434CE0. The packet with sequence `sequence` (in the history or the acknowledged list) was
    // acknowledged: Delivered, and it leaves its list.
    void Acknowledge(std::uint16_t sequence);
    // 00434D70. Reads the peer's acknowledgements (last sequence, mask of the 32 before it) and
    // acknowledges what is new.
    void ReadAcks(BufferCursor* cursor);
    // 00434E50. Whether an update with sequence `sequence` is new (not 32 older than the newest,
    // not seen before); remembers it.
    bool Accept(std::uint32_t sequence);
    // 00434AE0. Appends the queued incoming reliable chunks that are next in sequence to the
    // incoming reliable stream.
    void ProcessHeaders();
    // 00436470. Reads an update packet: sequence, acknowledgements, a reliable chunk and the
    // entries. False when an entry could not be read; true otherwise (also for a stale packet).
    bool ReadUpdate(BufferCursor* cursor);
};
static_assert(offsetof(Connection, address) == 0x2C);
static_assert(offsetof(Connection, clock) == 0x48);
static_assert(offsetof(Connection, idPools) == 0x54);
static_assert(offsetof(Connection, freedIds) == 0x1054);
static_assert(offsetof(Connection, bySentId) == 0x105C);
static_assert(offsetof(Connection, byObject) == 0x1068);
static_assert(offsetof(Connection, priorities) == 0x1074);
static_assert(offsetof(Connection, history) == 0x10F8);
static_assert(offsetof(Connection, records) == 0x1100);
static_assert(offsetof(Connection, attached) == 0x110C);
static_assert(offsetof(Connection, headerSequence) == 0x1118);
static_assert(offsetof(Connection, headers) == 0x111C);
static_assert(offsetof(Connection, reliableOut) == 0x112C);
static_assert(offsetof(Connection, reliableIn) == 0x1148);
static_assert(sizeof(Connection) == 0x1168);

// 0079B920: the session being updated (set by Receive and by the connection code while it runs).
inline SessionBase* CurrentSession() { return *reinterpret_cast<SessionBase**>(0x0079B920); }
// 0079B91C: the network log stream (null when logging is off).
inline void* NetLog() { return *reinterpret_cast<void**>(0x0079B91C); }
// 0079B900: the shared send buffer.
inline GameBuffer* SendBuffer() { return reinterpret_cast<GameBuffer*>(0x0079B900); }

} // namespace net
