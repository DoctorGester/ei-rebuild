#pragma once

// A connection of a network session to one peer (vtable 0073BF38, 0x1168 bytes; constructor
// 00432DA0, created by SessionBase::AddClient 00436DD0) and the object replication it runs.
// Names are provisional; see docs/notes/network.md.
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

#include "lib/stlp_list_refs.h"
#include "lib/stlp_tree_abi.h"

#include <cstddef>
#include <cstdint>

namespace net {

// A free-list of message ids for one message type (16 bytes): returned ids, then the next new id.
struct IdPool {
    stlp::vector<std::uint16_t> free;   // +0x00 the ids returned
    std::uint16_t next;        // +0x0C the last id handed out new
    std::uint16_t unused_0E;

    // A returned id (the last one) if any, else ++next.
    std::uint16_t Allocate();
    // Copy constructor; returns this.
    IdPool* CopyFrom(const IdPool& other);
};
static_assert(sizeof(IdPool) == 0x10);

// The tracking record of one replicated object (game.exe's vtable 0073BF4C, 0x14 bytes).
class ReplicaEntry final : public CRefObject {
public:
    // As Track inlines it: no object, no version and no id yet (the type and the flags are the
    // caller's).
    ReplicaEntry();
    // Clone's copy: the same object, one more reference.
    ReplicaEntry(const ReplicaEntry& other);
    // 004344C0 (deleting: 004344A0): the object released.
    ~ReplicaEntry() override;
    // 1: Serialize (empty).
    int OnShutdown() override;              // 2: 00434430 reset: the object let go, version and id cleared
    ReplicaEntry* Clone() override;         // 3: 004343C0 a copy sharing the object

    Message* Object() const { return static_cast<Message*>(object_.ptr); }

    std::uint32_t version_;    // +0x08 the object's version last sent (1: removal sent)
    std::uint16_t id_;         // +0x0C 0 until first sent
    std::uint8_t type_;        // +0x0E message type of the object
    std::uint8_t flags_;       // +0x0F bit 0 reliable; bits 1..7 priority (0..10)
    RefPtr object_;            // +0x10 the object (null once it is gone)
};

// One item of a history entry: what a packet carried for one entry (an element of
// SentPacket::items; its list's element policy is library_net_lists.h's SentRecordElement).
struct SentItem {
    enum Action : std::uint32_t {
        kChanged = 0,
        kChangedReliable = 1,
        kRemoved = 2,
        kRemovedReliable = 3,
    };

    std::uint32_t action;      // +0x00 Action
    std::uint32_t version;     // +0x04 the object's version sent
    ReplicaEntry* entry;       // +0x08 counted pointer
};
using SentItemNode = stlp::list<SentItem>::Node;
static_assert(sizeof(SentItemNode) == 0x14);

// A chunk of the reliable stream: {uint16 sequence, uint8 size, size bytes}.
struct ReliableChunk {
    std::uint8_t bytes[0x104];

    std::uint16_t Sequence() const { return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8); }
    std::uint8_t Size() const { return bytes[2]; }
};

// A packet in the send history: when it went out and what it carried (its list's element policy is
// library_net_lists.h's PacketRecordElement).
struct SentPacket {
    double time;               // +0x00 the connection's clock when sent
    std::uint16_t sequence;    // +0x08
    std::uint16_t unused_0A;
    stlp::list<SentItem> items;   // +0x0C
    ReliableChunk header;      // +0x10 the reliable chunk the packet carried
    std::uint8_t rolledBack;   // +0x114
    std::uint8_t unused_115[3];
};
static_assert(offsetof(SentPacket, items) == 0x0C);
static_assert(offsetof(SentPacket, rolledBack) == 0x114);

// A node of the send history (history_ / acknowledged_).
using HistoryNode = stlp::list<SentPacket>::Node;
static_assert(offsetof(HistoryNode, data) == 0x08);
static_assert(sizeof(HistoryNode) == 0x120);

// A node of the chunk lists (headers_: waiting to be sent; incoming_: received out of order).
using HeaderNode = stlp::list<ReliableChunk>::Node;
static_assert(sizeof(HeaderNode) == 0x10C);

// A batch of freed ids (an element of freedIds_).
struct FreedId {
    std::uint16_t id;
    std::uint8_t type;
    std::uint8_t unused_3;
};
using FreedIds = stlp::vector<FreedId>;
using FreedBatch = stlp::list<FreedIds>::Node;
static_assert(sizeof(FreedBatch) == 0x14);

// An object received from the peer (the value of records_).
struct Record {
    RefPtr object;
    std::uint32_t version;     // +0x04 the version last applied
};

// The (type, id) a received object came with (the value of attached_).
struct AttachedId {
    std::uint16_t id;
    std::uint8_t type;
    std::uint8_t unused_3;
};

// The connection's five STLport maps as registered instantiations (net_connection_trees.cpp): their
// value policies and stlp::TreeAbi types, which the connection's fields are.
namespace trees {

// Counted references to a replica entry, copied with one more reference and released.
struct CountedValue : stlp::CountedRef {
    using Type = RefPtrTo<ReplicaEntry>;
};
// A record copied as the pair constructor 0043A090 and _M_create_node 0043DF00 copy it: shared,
// then the version; destroyed with the owner's detach.
struct RecordValue {
    using Type = Record;
    static void Copy(Record* to, const Record& from) {
        to->object.Share(from.object.ptr);
        to->version = from.version;
    }
    static void Destroy(Record* p) { p->object.Destroy(); }
};

template <typename K, typename Second>
using PairTree = stlp::TreeAbi<stlp::map<K, typename Second::Type>,
                               stlp::PairElement<typename stlp::map<K, typename Second::Type>::Value, Second>>;

using SentIds = PairTree<std::int32_t, CountedValue>;    // type << 16 | id -> entry
using Objects = PairTree<CRefObject*, CountedValue>;     // object -> entry
using Entries = PairTree<ReplicaEntry*, CountedValue>;   // entry -> entry
using Records = PairTree<std::int32_t, RecordValue>;     // type << 16 | id -> record
using Attached = stlp::TreeAbi<stlp::map<CRefObject*, AttachedId>>;   // object -> (type, id)

} // namespace trees

class Connection final : public CRefObject {
public:
    using EntryMap = trees::Entries;
    using RecordMap = trees::Records;

    // Every container empty, the clocks started.
    explicit Connection(std::uint32_t id);
    // Clone's copy: every container copied.
    Connection(const Connection& other);
    // 004334E0 (deleting: 004334C0; net_connection_life.cpp).
    ~Connection() override;

    // 1: Serialize (empty).
    // 2: 00433320 (net_connection_life.cpp). Torn down and built again with id 0; returns this.
    int OnShutdown() override;
    // 3: 00433060 (net_connection_life.cpp). A copy of the connection.
    Connection* Clone() override;

    // Builds and sends one update packet of at most `budget` entry bytes. False when
    // entries had to be left out (or the send failed).
    bool SendUpdate(int budget);
    // Reads one entry of an update (created / changed or removed) into `records_` and
    // calls the session's handlers (channel 0 created, 1 changed, 2 removed). False when the
    // object could not be created.
    bool ReadRecord(BufferCursor* cursor, std::uint32_t version);
    // Remembers which (type, id) the received object has (attached_).
    void AttachRecord(CRefObject* object, std::uint32_t type, std::int32_t key);
    // Writes a reference to `object`: a 0 bit, type and id for an object this side
    // replicates (an id is assigned on first use), a 1 bit, type and id for one received from the
    // peer, or a 0 bit and zeros for none.
    void WriteObjectRef(BufferCursor* cursor, CRefObject* object);
    // Reads such a reference: the peer's replicated object (created from the type's
    // factory when missing or detached) or one this side sent; null for none.
    CRefObject* ReadObjectRef(BufferCursor* cursor);
    // Writes the acknowledgements: the last sequence received and a mask of the 32
    // before it; forgets sequences more than 32 old.
    void WriteAcks(std::uint8_t** p);
    // Makes sure a reliable chunk is queued: a new one of up to min(budget, 255) bytes
    // from the reliable stream when none is waiting, else the oldest waiting one moves to the front.
    void QueueHeader(int budget);
    // A packet was lost: entries it sent unreliably are marked unsent again and its
    // reliable chunk goes back to the front of the queue (once). `packet` is a history node's data
    // (the node + 8, as the list iterators hand it over).
    void RollBack(SentPacket* packet);
    // Brings the tracked entries up to date: the session's slot 13 lists what this
    // connection should have (sorted by object); new objects get an entry (Track), entries whose
    // object is no longer listed are dropped (never sent) or marked gone (sent).
    void CollectChanges();
    // Starts tracking `item` (an {object, priority} pair): a new entry for the object,
    // kept in byObject_ and its priority map.
    void Track(Replicated* item);
    // Clocks, lost packets, sending, delayed id reuse; false
    // when the peer timed out or the reliable stream overflowed.
    bool Tick(double dt);
    // A packet arrived: the round-trip averages take its age;
    // reliable changes it carried count as delivered, removals it carried end their entries (the
    // id goes to the newest freed-id batch).
    void Delivered(SentPacket* packet);
    // 1 when the peer has been silent for longer than three average round trips (or a
    // value is NaN), else 0.
    int Silent() const;
    // The packet with sequence `sequence` (in the history or the acknowledged list) was
    // acknowledged: Delivered, and it leaves its list.
    void Acknowledge(std::uint16_t sequence);
    // Reads the peer's acknowledgements (last sequence, mask of the 32 before it) and
    // acknowledges what is new.
    void ReadAcks(BufferCursor* cursor);
    // Whether an update with sequence `sequence` is new (not 32 older than the newest,
    // not seen before); remembers it.
    bool Accept(std::uint32_t sequence);
    // Appends the queued incoming reliable chunks that are next in sequence to the
    // incoming reliable stream.
    void ProcessHeaders();
    // Reads an update packet: sequence, acknowledgements, a reliable chunk and the
    // entries. False when an entry could not be read; true otherwise (also for a stale packet).
    bool ReadUpdate(BufferCursor* cursor);
    // The peer goes away: every object received from it is reported to the session's
    // removal handlers (channel 2) and released.
    void Close();

    std::uint16_t sendSequence_;        // +0x0008 starts at GetTickCount()
    std::uint16_t unused_000A;
    std::uint32_t lastReceived_;        // +0x000C sequence of the newest packet received
    std::uint32_t sequenceHigh_;        // +0x0010 0x10000: the upper bits of the received sequences
    stlp::vector<std::int32_t> received_;   // +0x0014 sequences received recently
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
    stlp::list<FreedIds> freedIds_;     // +0x1054 batches of ids returned to the pools later
    float freedTimer_;                  // +0x1058
    trees::SentIds bySentId_;           // +0x105C type << 16 | id -> entry
    trees::Objects byObject_;           // +0x1068 object -> entry
    EntryMap priorities_[11];           // +0x1074 per priority: entry -> entry
    stlp::list<SentPacket> history_;    // +0x10F8 packets sent, waiting for an acknowledgement
    stlp::list<SentPacket> acknowledged_;   // +0x10FC lost packets, kept a while for late acknowledgements
    RecordMap records_;                 // +0x1100 objects received (key type << 16 | id)
    trees::Attached attached_;          // +0x110C received object -> (type, id)
    std::uint16_t headerSequence_;      // +0x1118 sequence of the next reliable chunk sent
    std::uint16_t incomingSequence_;    // +0x111A sequence of the next reliable chunk expected
    stlp::list<ReliableChunk> headers_;    // +0x111C reliable chunks waiting to be sent
    stlp::list<ReliableChunk> incoming_;   // +0x1120 reliable chunks received ahead of their turn
    std::uint32_t ackMask_;             // +0x1124 bit k: packet ackBase_ - 1 - k acknowledged already
    std::uint16_t ackBase_;             // +0x1128 the peer's newest acknowledged sequence
    std::uint16_t unused_112A;
    GrowingBuffer reliableOut_;         // +0x112C
    GrowingBuffer reliableIn_;          // +0x1148
    std::uint32_t padding_1164;
};

} // namespace net
