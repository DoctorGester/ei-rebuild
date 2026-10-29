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
#include <unordered_map>
#include <vector>

namespace net {

// A free-list of message ids for one message type: returned ids, then the next new id.
struct IdPool {
    std::vector<std::uint16_t> free;   // the ids returned
    std::uint16_t next;        // the last id handed out new

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
    void OnShutdown() override;              // Reset: the object let go, version and id cleared
    ReplicaEntry* Clone() override;

    Message* Object() const { return object.Get(); }

    std::uint32_t version;     // the object's version last sent (1: removal sent)
    std::uint16_t id;          // 0 until first sent
    std::uint8_t type;         // message type of the object
    std::uint8_t flags;        // bit 0 reliable; bits 1..7 priority (0..10)
    Ref<Message> object;    // the object (null once it is gone)
    std::uint32_t serial = 0;    // the connection's count of tracked entries when this one was made: its key in priorities
};

// A counted reference to a replica entry (the connection's maps' values, a sent item's entry). Kept: the name of
// the reference type of the replication code.
using EntryRef = HeldRef<ReplicaEntry>;

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

// A batch of freed ids (an element of freedIds).
struct FreedId {
    std::uint16_t id;
    std::uint8_t type;
};

// An object received from the peer (the value of records): its share (copied with Share, released as its owner).
struct Record {
    SharedRef<CRefObject> object;
    std::uint32_t version = 0;   // the version last applied
};

// The (type, id) a received object came with (the value of attached).
struct AttachedId {
    std::uint16_t id;
    std::uint8_t type;
};

class Connection final : public CRefObject {
public:

    explicit Connection(std::uint32_t id);
    // Every container copied.
    Connection(const Connection& other);
    ~Connection() override;

    // Torn down and built again with id 0.
    void OnShutdown() override;
    Connection* Clone() override;

    // Builds and sends one update packet of at most `budget` entry bytes. False when entries had to be left
    // out (or the send failed).
    bool SendUpdate(int budget);
    // Reads one entry of an update (created / changed or removed) into `records` and calls the session's
    // handlers (channel 0 created, 1 changed, 2 removed). False when the object could not be created.
    bool ReadRecord(BufferCursor* cursor, std::uint32_t version);
    // Remembers which (type, id) the received object has (attached).
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
    // should have; entries whose object is no longer listed are dropped (never sent) or marked gone (sent),
    // then new objects get an entry (Track) in the list's order.
    void CollectChanges();
    // Starts tracking `item` (an {object, priority} pair): a new entry for the object (the next serial),
    // kept in byObject and its priority map.
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

    std::uint16_t sendSequence;         // starts at GetTickCount()
    std::uint32_t lastReceived;         // sequence of the newest packet received
    std::uint32_t sequenceHigh;         // 0x10000: the upper bits of the received sequences
    std::vector<std::int32_t> received;    // sequences received recently
    std::uint32_t id;                   // the connection id (sent with a leave packet)
    float roundTrip;                    // average round trip (seconds)
    float roundTripSquare;              // average of its square
    NetAddress address;                 // the peer
    float sendInterval;                 // 0.04
    float sendTimer;
    float idleTime;                     // seconds since the peer was last heard
    double clock;                       // seconds since the connection started
    std::uint32_t updateBudget;         // 220: bytes per update
    IdPool idPools[kMessageTypes];      // one per message type
    std::list<std::vector<FreedId>> freedIds;      // batches of ids returned to the pools later
    float freedTimer;
    std::map<std::int32_t, EntryRef> bySentId;     // type << 16 | id -> entry
    // Lookups only; nothing walks it. The original keys both by address (an ordered map each); the priority
    // maps are walked to send, so ours key them by the entry's serial (the order entries were tracked in).
    std::unordered_map<CRefObject*, EntryRef> byObject;      // object -> entry
    std::array<std::map<std::uint32_t, EntryRef>, 11> priorities;           // per priority: entry serial -> entry
    std::uint32_t entriesTracked = 0;    // the last serial handed out
    std::list<SentPacket> history;     // packets sent, waiting for an acknowledgement
    std::list<SentPacket> acknowledged;    // lost packets, kept a while for late acknowledgements
    std::map<std::int32_t, Record> records;                  // objects received (key type << 16 | id)
    std::unordered_map<CRefObject*, AttachedId> attached;    // received object -> (type, id); lookups only
    std::uint16_t headerSequence;       // sequence of the next reliable chunk sent
    std::uint16_t incomingSequence;     // sequence of the next reliable chunk expected
    std::list<ReliableChunk> headers;     // reliable chunks waiting to be sent
    std::list<ReliableChunk> incoming;    // reliable chunks received ahead of their turn
    std::uint32_t ackMask;              // bit k: packet ackBase - 1 - k acknowledged already
    std::uint16_t ackBase;              // the peer's newest acknowledged sequence
    GrowingBuffer reliableOut;
    GrowingBuffer reliableIn;
    std::uint32_t padding_1164;
};
} // namespace net
