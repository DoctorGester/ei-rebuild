#pragma once

// A connection of a network session to one peer (constructor created by SessionBase::AddClient)
// and the object replication it runs.
// More: docs/notes/comments-c22i.md, "net_connection.h".
#include "core_refcount.h"
#include "net_classes.h"
#include "net_coder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <list>
#include <map>
#include <vector>

namespace net {

// A free-list of message ids for one message type: returned ids, then the next new id.
struct IdPool {
    std::vector<std::uint16_t> free;   // the ids returned
    std::uint16_t next;        // the last id handed out new
    std::uint16_t unused_0E;

    // A returned id (the last one) if any, else ++next.
    std::uint16_t Allocate();
};

// The tracking record of one replicated object.
class ReplicaEntry final : public CRefObject {
public:
    ReplicaEntry();
    // The same object, one more reference.
    ReplicaEntry(const ReplicaEntry& other);
    ~ReplicaEntry() override;
    int OnShutdown() override;              // Reset: the object let go, version and id cleared
    ReplicaEntry* Clone() override;

    Message* Object() const { return object_.Get(); }

    std::uint32_t version_;    // the object's version last sent (1: removal sent)
    std::uint16_t id_;         // 0 until first sent
    std::uint8_t type_;        // message type of the object
    std::uint8_t flags_;       // bit 0 reliable; bits 1..7 priority (0..10)
    RefPtrTo<Message> object_;   // the object (null once it is gone)
};

// A counted reference to a replica entry (the connection's maps' values, a sent item's entry).
using EntryRef = CountedPtr<ReplicaEntry>;

// One item of a history entry: what a packet carried for one entry.
struct SentItem {
    enum Action : std::uint32_t {
        kChanged = 0,
        kChangedReliable = 1,
        kRemoved = 2,
        kRemovedReliable = 3,
    };

    std::uint32_t action = 0;    // Action
    std::uint32_t version = 0;   // the object's version sent
    EntryRef entry;
};

// A chunk of the reliable stream: {uint16 sequence, uint8 size, size bytes}.
struct ReliableChunk {
    std::uint8_t bytes[0x104];

    std::uint16_t Sequence() const { return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8); }
    std::uint8_t Size() const { return bytes[2]; }
};

// A packet in the send history: when it went out and what it carried.
struct SentPacket {
    double time = 0.0;           // the connection's clock when sent
    std::uint16_t sequence = 0;
    std::list<SentItem> items;
    ReliableChunk header{};      // the reliable chunk the packet carried
    std::uint8_t rolledBack = 0;
};

// A batch of freed ids (an element of freedIds_).
struct FreedId {
    std::uint16_t id;
    std::uint8_t type;
    std::uint8_t unused_3;
};
using FreedIds = std::vector<FreedId>;

// An object received from the peer (the value of records_): its share (copied with Share, released as its owner).
struct Record {
    SharedRefPtr object;
    std::uint32_t version = 0;   // the version last applied
};

// The (type, id) a received object came with (the value of attached_).
struct AttachedId {
    std::uint16_t id;
    std::uint8_t type;
    std::uint8_t unused_3;
};

class Connection final : public CRefObject {
public:
    using EntryMap = std::map<ReplicaEntry*, EntryRef>;
    using RecordMap = std::map<std::int32_t, Record>;

    explicit Connection(std::uint32_t id);
    // Every container copied.
    Connection(const Connection& other);
    ~Connection() override;

    // Torn down and built again with id 0.
    int OnShutdown() override;
    Connection* Clone() override;

    // Builds and sends one update packet of at most `budget` entry bytes. False when entries had to be left
    // out (or the send failed).
    bool SendUpdate(int budget);
    // Reads one entry of an update (created / changed or removed) into `records_` and calls the session's
    // handlers (channel 0 created, 1 changed, 2 removed). False when the object could not be created.
    bool ReadRecord(BufferCursor* cursor, std::uint32_t version);
    // Remembers which (type, id) the received object has (attached_).
    void AttachRecord(CRefObject* object, std::uint32_t type, std::int32_t key);
    // Writes a reference to `object`: a 0 bit, type and id for an object this side replicates (an id is
    // assigned on first use), a 1 bit, type and id for one received from the peer, or a 0 bit and zeros.
    void WriteObjectRef(BufferCursor* cursor, CRefObject* object);
    // Reads such a reference: the peer's replicated object (created from the type's factory when missing or
    // detached) or one this side sent; null for none.
    CRefObject* ReadObjectRef(BufferCursor* cursor);
    // Writes the acknowledgements: the last sequence received and a mask of the 32 before it; forgets
    // sequences more than 32 old.
    void WriteAcks(std::uint8_t** p);
    // Makes sure a reliable chunk is queued: a new one of up to min(budget, 255) bytes from the reliable
    // stream when none is waiting, else the oldest waiting one moves to the front.
    void QueueHeader(int budget);
    // A packet was lost: entries it sent unreliably are marked unsent again and its reliable chunk goes back
    // to the front of the queue (once). `packet` is a history node's data.
    void RollBack(SentPacket* packet);
    // Brings the tracked entries up to date: the session's CollectReplicated lists what this connection
    // should have (sorted by object); new objects get an entry (Track), entries whose object is no longer
    // listed are dropped (never sent) or marked gone (sent).
    void CollectChanges();
    // Starts tracking `item` (an {object, priority} pair): a new entry for the object,
    // kept in byObject_ and its priority map.
    void Track(Replicated* item);
    // Clocks, lost packets, sending, delayed id reuse; false when the peer timed out or the reliable stream
    // overflowed.
    bool Tick(double dt);
    // A packet arrived: the round-trip averages take its age; reliable changes it carried count as delivered,
    // removals it carried end their entries (the id goes to the newest freed-id batch).
    void Delivered(SentPacket* packet);
    // The packet `sequence` (in the history or the acknowledged list) was acknowledged: Delivered, and it
    // leaves its list.
    void Acknowledge(std::uint16_t sequence);
    // Reads the peer's acknowledgements (last sequence, mask of the 32 before it); acknowledges what is new.
    void ReadAcks(BufferCursor* cursor);
    // Whether update `sequence` is new (not 32 older than the newest, not seen before); remembers it.
    bool Accept(std::uint32_t sequence);
    // Appends the queued incoming reliable chunks that are next in sequence to the incoming reliable stream.
    void ProcessHeaders();
    // Reads an update packet: sequence, acknowledgements, a reliable chunk and the entries. False when an
    // entry could not be read; true otherwise (also for a stale packet).
    bool ReadUpdate(BufferCursor* cursor);
    // The peer goes away: every object received from it goes to the session's removal handlers (channel 2)
    // and is released.
    void Close();

    std::uint16_t sendSequence_;        // starts at GetTickCount()
    std::uint16_t unused_000A;
    std::uint32_t lastReceived_;        // sequence of the newest packet received
    std::uint32_t sequenceHigh_;        // 0x10000: the upper bits of the received sequences
    std::vector<std::int32_t> received_;   // sequences received recently
    std::uint32_t id_;                  // the connection id (sent with a leave packet)
    float roundTrip_;                   // average round trip (seconds)
    float roundTripSquare_;             // average of its square
    NetAddress address_;                // the peer
    float sendInterval_;                // 0.04
    float sendTimer_;
    float idleTime_;                    // seconds since the peer was last heard
    double clock_;                      // seconds since the connection started
    std::uint32_t updateBudget_;        // 220: bytes per update
    IdPool idPools_[kMessageTypes];     // one per message type
    std::list<FreedIds> freedIds_;     // batches of ids returned to the pools later
    float freedTimer_;
    std::map<std::int32_t, EntryRef> bySentId_;    // type << 16 | id -> entry
    std::map<CRefObject*, EntryRef> byObject_;     // object -> entry
    std::array<EntryMap, 11> priorities_;          // per priority: entry -> entry
    std::list<SentPacket> history_;    // packets sent, waiting for an acknowledgement
    std::list<SentPacket> acknowledged_;   // lost packets, kept a while for late acknowledgements
    RecordMap records_;                 // objects received (key type << 16 | id)
    std::map<CRefObject*, AttachedId> attached_;   // received object -> (type, id)
    std::uint16_t headerSequence_;      // sequence of the next reliable chunk sent
    std::uint16_t incomingSequence_;    // sequence of the next reliable chunk expected
    std::list<ReliableChunk> headers_;    // reliable chunks waiting to be sent
    std::list<ReliableChunk> incoming_;   // reliable chunks received ahead of their turn
    std::uint32_t ackMask_;             // bit k: packet ackBase_ - 1 - k acknowledged already
    std::uint16_t ackBase_;             // the peer's newest acknowledged sequence
    std::uint16_t unused_112A;
    GrowingBuffer reliableOut_;
    GrowingBuffer reliableIn_;
    std::uint32_t padding_1164;
};
} // namespace net
