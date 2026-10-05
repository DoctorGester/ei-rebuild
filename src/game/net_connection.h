#pragma once

// A connection of a network session to one peer (vtable 0073BF38, 0x1168 bytes; constructor
// 00432DA0, created by SessionBase::AddClient 00436DD0) and the object replication it runs.
// Names are provisional; see docs/notes/unmapped_410_470.md ("Third round: connections").
//
// Replication: every replicated object (a Message, whose version changes with it) is tracked by a
// ReplicaEntry in one of 11 priority maps. Each update packet carries, highest priority first, the
// entries whose object version differs from the version last sent (a 1 bit, type, id, then the
// object's own Write) or whose object went away (a 0 bit, type, id). Every packet is remembered in
// the send history with what it carried, so an acknowledgement (or its absence) can mark entries as
// delivered or resend them. A reliable byte stream (reliableOut_, reliableIn_) travels in front of
// the entries in chunks of up to 255 bytes ("headers"). The peer keeps the objects it received in
// `records_` (key type << 16 | id).
#include "core_refcount.h"
#include "net_classes.h"
#include "net_coder.h"

#include <cstddef>
#include <cstdint>

namespace net {

// A node of an STLport red-black tree (map<Key, Value>): color, parent, children, then the pair.
template <typename Key, typename Value>
struct TreeNode {
    std::uint32_t color;
    TreeNode* parent;
    TreeNode* left;
    TreeNode* right;
    Key key;                   // +0x10
    Value value;               // +0x14
};

// An STLport red-black tree (map / set; 0xC bytes): the header node (its parent is the root, its
// children the leftmost and rightmost nodes), the element count, the comparison (empty).
template <typename Key, typename Value>
struct TreeMap {
    using Node = TreeNode<Key, Value>;

    Node* header;              // +0x00
    std::uint32_t count;       // +0x04
    std::uint8_t compare;      // +0x08
    std::uint8_t unknown_09[3];

    Node* Begin() const { return header->left; }
};

// A free-list of message ids for one message type (16 bytes): returned ids, then the next new id.
struct IdPool {
    std::uint16_t* begin;      // +0x00 vector<uint16_t> of free ids
    std::uint16_t* end;        // +0x04
    std::uint16_t* eos;        // +0x08
    std::uint16_t next;        // +0x0C the last id handed out new
    std::uint16_t unknown_0E;

    // 00435F10. A returned id (the last one) if any, else ++next.
    std::uint16_t Allocate();
    // 00433A30 (net_connection_life.cpp). Copy constructor; returns this.
    IdPool* CopyFrom(const IdPool& other);
};
static_assert(sizeof(IdPool) == 0x10);

// The tracking record of one replicated object (vtable 0073BF4C, 0x14 bytes).
class __declspec(novtable) ReplicaEntry : public CRefObject {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BF4C;

    // 0: 004344A0 deleting destructor (DeletingDestruct); 1: Serialize (empty).
    int OnShutdown() override;              // 2: 00434430 reset: the object let go, version and id cleared
    ReplicaEntry* Clone() override;         // 3: 004343C0 a copy sharing the object

    // 004344C0. The destructor body: the object released.
    void Destruct();
    // 004344A0 (slot 0). Destruct, then operator delete when bit 0 of `flags` is set.
    ReplicaEntry* DeletingDestruct(unsigned flags);

    Message* Object() const { return static_cast<Message*>(object_.ptr); }

    std::uint32_t version_;    // +0x08 the object's version last sent (1: removal sent)
    std::uint16_t id_;         // +0x0C 0 until first sent
    std::uint8_t type_;        // +0x0E message type of the object
    std::uint8_t flags_;       // +0x0F bit 0 reliable; bits 1..7 priority (0..10)
    RefPtr object_;            // +0x10 the object (null once it is gone)
};
static_assert(offsetof(ReplicaEntry, version_) == 0x08);
static_assert(offsetof(ReplicaEntry, object_) == 0x10);
static_assert(sizeof(ReplicaEntry) == 0x14);

// One item of a history entry: what a packet carried for one entry.
struct SentItem {
    enum Action : std::uint32_t {
        kChanged = 0,
        kChangedReliable = 1,
        kRemoved = 2,
        kRemovedReliable = 3,
    };

    SentItem* next;
    SentItem* prev;
    std::uint32_t action;      // +0x08 Action
    std::uint32_t version;     // +0x0C the object's version sent
    ReplicaEntry* entry;       // +0x10 counted pointer
};

// A chunk of the reliable stream: {uint16 sequence, uint8 size, size bytes}.
struct ReliableChunk {
    std::uint8_t bytes[0x104];

    std::uint16_t Sequence() const { return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8); }
    std::uint8_t Size() const { return bytes[2]; }
};

// A node of the send history (history_ / acknowledged_): when a packet went out and what it carried.
struct HistoryNode {
    HistoryNode* next;
    HistoryNode* prev;
    double time;               // +0x08 the connection's clock when sent
    std::uint16_t sequence;    // +0x10
    std::uint16_t unknown_12;
    SentItem* items;           // +0x14 STLport list sentinel
    ReliableChunk header;      // +0x18 the reliable chunk the packet carried
    std::uint8_t rolledBack;   // +0x11C
};
static_assert(offsetof(HistoryNode, time) == 0x08);
static_assert(offsetof(HistoryNode, items) == 0x14);
static_assert(offsetof(HistoryNode, rolledBack) == 0x11C);

// A node of the chunk lists (headers_: waiting to be sent; incoming_: received out of order).
struct HeaderNode {
    HeaderNode* next;
    HeaderNode* prev;
    ReliableChunk data;        // +0x08
};

// A batch of freed ids (a node of freedIds_): STLport vector<{uint16 id, uint8 type}>.
struct FreedId {
    std::uint16_t id;
    std::uint8_t type;
    std::uint8_t unknown_3;
};
struct FreedBatch {
    FreedBatch* next;
    FreedBatch* prev;
    FreedId* begin;            // +0x08
    FreedId* end;
    FreedId* eos;
};

// An object received from the peer (the value of records_).
struct Record {
    RefPtr object;
    std::uint32_t version;     // +0x04 the version last applied
};

// The (type, id) a received object came with (the value of attached_).
struct AttachedId {
    std::uint16_t id;
    std::uint8_t type;
    std::uint8_t unknown_3;
};

class __declspec(novtable) Connection : public CRefObject {
public:
    static constexpr std::uintptr_t kVtableAddress = 0x0073BF38;

    using EntryMap = TreeMap<ReplicaEntry*, RefPtr>;
    using RecordMap = TreeMap<std::int32_t, Record>;

    // 0: 004334C0 deleting destructor (DeletingDestruct); 1: Serialize (empty).
    // 2: 00433320 (net_connection_life.cpp). Destroyed and built again with id 0; returns what
    // the constructor returns.
    int OnShutdown() override;
    // 3: 00433060 (net_connection_life.cpp). A copy of the connection (every container copied).
    Connection* Clone() override;

    // net_connection_life.cpp: 00432DA0 constructor (returns this), 004334E0 destructor body,
    // 004334C0 deleting destructor (slot 0).
    Connection* Construct(std::uint32_t id);
    void Destruct();
    Connection* DeletingDestruct(unsigned flags);

    // 00435A00. Builds and sends one update packet of at most `budget` entry bytes. False when
    // entries had to be left out (or the send failed).
    bool SendUpdate(int budget);
    // 00435F30. Reads one entry of an update (created / changed or removed) into `records_` and
    // calls the session's handlers (channel 0 created, 1 changed, 2 removed). False when the
    // object could not be created.
    bool ReadRecord(BufferCursor* cursor, std::uint32_t version);
    // 004352F0. Remembers which (type, id) the received object has (attached_).
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
    // reliable chunk goes back to the front of the queue (once). `historyData` is the history
    // node's data (the node + 8, as the list iterators hand it over).
    void RollBack(std::uint8_t* historyData);
    // 004346D0. Brings the tracked entries up to date: the session's slot 13 lists what this
    // connection should have (sorted by object); new objects get an entry (Track), entries whose
    // object is no longer listed are dropped (never sent) or marked gone (sent).
    void CollectChanges();
    // 00434110. Starts tracking `item` (an {object, priority} pair): a new entry for the object,
    // kept in byObject_ and its priority map.
    void Track(Replicated* item);
    // 00433BD0 (net_connection_tick.cpp). Clocks, lost packets, sending, delayed id reuse; false
    // when the peer timed out or the reliable stream overflowed.
    bool Tick(double dt);
    // 00433FA0 (net_connection_tick.cpp). A packet arrived: the round-trip averages take its age;
    // reliable changes it carried count as delivered, removals it carried end their entries (the
    // id goes to the newest freed-id batch).
    void Delivered(std::uint8_t* historyData);
    // 00433B10. 1 when the peer has been silent for longer than three average round trips (or a
    // value is NaN), else 0.
    int Silent() const;
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
    // 00436620. The peer goes away: every object received from it is reported to the session's
    // removal handlers (channel 2) and released.
    void Close();

    std::uint16_t sendSequence_;        // +0x0008 starts at GetTickCount()
    std::uint16_t unknown_000A;
    std::uint32_t lastReceived_;        // +0x000C sequence of the newest packet received
    std::uint32_t sequenceHigh_;        // +0x0010 0x10000: the upper bits of the received sequences
    std::int32_t* received_;            // +0x0014 vector<int>: sequences received recently
    std::int32_t* receivedEnd_;
    std::int32_t* receivedEos_;
    std::uint32_t id_;                  // +0x0020 the connection id (sent with a leave packet)
    float roundTrip_;                   // +0x0024 average round trip (seconds)
    float roundTripSquare_;             // +0x0028 average of its square
    NetAddress address_;                // +0x002C the peer
    float sendInterval_;                // +0x003C 0.04
    float sendTimer_;                   // +0x0040
    float idleTime_;                    // +0x0044 seconds since the peer was last heard
    double clock_;                      // +0x0048 seconds since the connection started
    std::uint32_t updateBudget_;        // +0x0050 220: bytes per update
    IdPool idPools_[kMessageTypes];     // +0x0054 one per message type
    FreedBatch* freedIds_;              // +0x1054 list of batches of ids returned to the pools later
    float freedTimer_;                  // +0x1058
    TreeMap<std::uint32_t, RefPtr> bySentId_;   // +0x105C type << 16 | id -> entry
    TreeMap<CRefObject*, RefPtr> byObject_;     // +0x1068 object -> entry
    EntryMap priorities_[11];           // +0x1074 per priority: entry -> entry
    HistoryNode* history_;              // +0x10F8 packets sent, waiting for an acknowledgement
    HistoryNode* acknowledged_;         // +0x10FC lost packets, kept a while for late acknowledgements
    RecordMap records_;                 // +0x1100 objects received (key type << 16 | id)
    TreeMap<CRefObject*, AttachedId> attached_;   // +0x110C received object -> (type, id)
    std::uint16_t headerSequence_;      // +0x1118 sequence of the next reliable chunk sent
    std::uint16_t incomingSequence_;    // +0x111A sequence of the next reliable chunk expected
    HeaderNode* headers_;               // +0x111C reliable chunks waiting to be sent
    HeaderNode* incoming_;              // +0x1120 reliable chunks received ahead of their turn
    std::uint32_t ackMask_;             // +0x1124 bit k: packet ackBase_ - 1 - k acknowledged already
    std::uint16_t ackBase_;             // +0x1128 the peer's newest acknowledged sequence
    std::uint16_t unknown_112A;
    GameBuffer reliableOut_;            // +0x112C
    GameBuffer reliableIn_;             // +0x1148
    std::uint32_t padding_1164;
};
static_assert(offsetof(Connection, sendSequence_) == 0x8);
static_assert(offsetof(Connection, id_) == 0x20);
static_assert(offsetof(Connection, address_) == 0x2C);
static_assert(offsetof(Connection, clock_) == 0x48);
static_assert(offsetof(Connection, idPools_) == 0x54);
static_assert(offsetof(Connection, freedIds_) == 0x1054);
static_assert(offsetof(Connection, bySentId_) == 0x105C);
static_assert(offsetof(Connection, byObject_) == 0x1068);
static_assert(offsetof(Connection, priorities_) == 0x1074);
static_assert(offsetof(Connection, history_) == 0x10F8);
static_assert(offsetof(Connection, records_) == 0x1100);
static_assert(offsetof(Connection, attached_) == 0x110C);
static_assert(offsetof(Connection, headerSequence_) == 0x1118);
static_assert(offsetof(Connection, headers_) == 0x111C);
static_assert(offsetof(Connection, ackMask_) == 0x1124);
static_assert(offsetof(Connection, reliableOut_) == 0x112C);
static_assert(offsetof(Connection, reliableIn_) == 0x1148);
static_assert(sizeof(Connection) == 0x1168);

} // namespace net
