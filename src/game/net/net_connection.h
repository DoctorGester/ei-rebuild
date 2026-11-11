#pragma once

// A connection of a network session to one peer (created by SessionBase::AddClient) and the object replication it
// runs.
// More: docs/notes/network.md, "Replication".
#include "engine/core/core_refcount.h"
#include "game/net/net_session.h"
#include "engine/core/coder.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>

namespace net {

// A free-list of message ids for one message type: returned ids, then the next new id.
struct IdPool {
    std::vector<std::uint16_t> free;   // the ids returned
    std::uint16_t next = 0;    // the last id handed out new

    // A returned id (the last one) if any, else ++next.
    std::uint16_t Allocate();
};

// The tracking record of one replicated object.
class ReplicaEntry final : public RefObject {
public:
    std::uint32_t version = 0;    // the object's version last sent (1: removal sent)
    std::uint16_t id = 0;         // 0 until first sent
    std::uint8_t type = 0;        // the object's MessageType
    std::uint8_t priority = 0;    // 0..10: the priority map it is kept in
    Ref<Message> object;    // the object (null once it is gone)
    std::uint32_t serial = 0;    // the connection's count of tracked entries when this one was made: its key in priorities
};

// A counted reference to a replica entry (the connection's maps' values, a sent item's entry). Kept: the name of
// the reference type of the replication code.
using EntryRef = HeldRef<ReplicaEntry>;

// One item of a history entry: what a packet carried for one entry.
struct SentItem {
    enum class Action : std::uint8_t { Changed, Removed };

    Action action = Action::Changed;
    std::uint32_t version = 0;   // the object's version sent (a change)
    EntryRef entry;
};

// A chunk of the reliable stream: {uint16 sequence, uint8 size, size bytes}.
struct ReliableChunk {
    std::uint8_t bytes[0x104] = {};

    std::uint16_t Sequence() const { return static_cast<std::uint16_t>(bytes[0] | bytes[1] << 8); }
    std::uint8_t Size() const { return bytes[2]; }
};

// A packet in the send history: when it went out and what it carried.
struct SentPacket {
    double time = 0.0;           // the connection's clock when sent
    std::uint16_t sequence = 0;
    std::vector<SentItem> items;
    ReliableChunk header{};      // the reliable chunk the packet carried
    std::uint8_t rolledBack = 0;
};

// An id freed by a delivered removal, returned to its pool later (freedOlder, freedNewer).
struct FreedId {
    std::uint16_t id = 0;
    std::uint8_t type = 0;
};

// An object received from the peer (the value of records): its share (copied with Share, released as its owner).
struct Record {
    SharedRef<Message> object;
    std::uint32_t version = 0;   // the version last applied
};

// The (type, id) a received object came with (the value of attached).
struct AttachedId {
    std::uint16_t id = 0;
    std::uint8_t type = 0;
};

// The connection timings (the original's session fields, which nothing changed), in seconds.
constexpr float kPeerTimeout = 60.0f;    // silence before a peer is dropped
// A new connection's round trip estimate, the longest loss timeout and how long lost packets stay remembered.
constexpr float kMaxRoundTrip = 5.0f;
constexpr float kIdReuseTime = 75.0f;    // before a freed message id is reused

// The key of an object in records and bySentId: type << 16 | id.
constexpr std::int32_t RecordKey(std::uint8_t type, std::uint16_t id) { return (type << 16) | id; }

// Everything a connection holds apart from its count and flags: a new connection's state, which OnShutdown
// restores (with id 0).
struct ConnectionState {
    // The send sequence starts at the tick count, the round trip at kMaxRoundTrip.
    explicit ConnectionState(std::uint32_t id);

    std::uint16_t sendSequence = 0;
    std::uint32_t lastReceived = 0;     // sequence of the newest packet received
    std::uint32_t sequenceHigh = 0x10000;    // the upper bits of the received sequences
    std::vector<std::int32_t> received;    // sequences received recently
    std::uint32_t id = 0;               // the connection id (sent with a leave packet)
    float roundTrip = kMaxRoundTrip;    // average round trip (seconds)
    // The average of its square. The original's started as whatever the memory held, so the first loss timeouts
    // were the memory's.
    float roundTripSquare = 0.0f;
    NetAddress address{};               // the peer
    float sendInterval = 0.04f;         // AddClient sets it from the rate
    float sendTimer = 0.0f;
    float idleTime = 0.0f;              // seconds since the peer was last heard
    double clock = 0.0;                 // seconds since the connection started
    std::uint32_t updateBudget = 220;   // bytes per update
    IdPool idPools[kMessageTypes];      // one per message type
    std::vector<FreedId> freedOlder;    // ids returned to the pools at the next kIdReuseTime tick
    std::vector<FreedId> freedNewer;    // ids freed since the last one (the older batch after it)
    float freedTimer = 0.0f;
    std::map<std::int32_t, EntryRef> bySentId;     // RecordKey -> entry
    // Lookups only; nothing walks it. The original keys both by address (an ordered map each); the priority
    // maps are walked to send, so ours key them by the entry's serial (the order entries were tracked in).
    std::unordered_map<Message*, EntryRef> byObject;      // object -> entry
    std::array<std::map<std::uint32_t, EntryRef>, 11> priorities;           // per priority: entry serial -> entry
    std::uint32_t entriesTracked = 0;    // the last serial handed out
    std::vector<SentPacket> history;       // packets sent, waiting for an acknowledgement (the oldest first)
    std::vector<SentPacket> acknowledged;  // lost packets, kept a while for late acknowledgements
    std::map<std::int32_t, Record> records;                  // objects received (RecordKey)
    std::unordered_map<Message*, AttachedId> attached;    // received object -> (type, id); lookups only
    std::uint16_t headerSequence = 0;       // sequence of the next reliable chunk sent
    std::uint16_t incomingSequence = 0;     // sequence of the next reliable chunk expected
    std::vector<ReliableChunk> headers;     // reliable chunks waiting to be sent
    std::vector<ReliableChunk> incoming;    // reliable chunks received ahead of their turn
    std::uint32_t ackMask = 0;              // bit k: packet ackBase - 1 - k acknowledged already
    std::uint16_t ackBase = 0;              // the peer's newest acknowledged sequence
    GrowingBuffer reliableOut;
    GrowingBuffer reliableIn;
};

class Connection final : public RefObject, public ConnectionState {
public:
    Connection(SessionBase& owner, std::uint32_t id) : ConnectionState(id), session(owner) {}

    // Back to a new connection's state with id 0 (the count, the flags and the session kept).
    void OnShutdown() override;

    SessionBase& session;    // the session whose clients list holds it: its socket, OnMessage and CollectReplicated

    // Builds and sends one update packet of at most `budget` entry bytes. False when entries had to be left
    // out (or the send failed).
    bool SendUpdate(int budget);
    // Reads one entry of an update (created / changed or removed) into `records` and tells the session's
    // OnMessage. False when the object could not be created.
    bool ReadRecord(BufferCursor* cursor, std::uint32_t version);
    // Writes a reference to `object`: a 0 bit, type and id for an object this side replicates (an id is
    // assigned on first use), a 1 bit, type and id for one received from the peer, or a 0 bit and zeros.
    void WriteObjectRef(BufferCursor* cursor, Message* object);
    // Reads such a reference: the peer's replicated object (made by CreateMessage when missing or detached) or one
    // this side sent; null for none.
    Message* ReadObjectRef(BufferCursor* cursor);
    // Writes the acknowledgements: the last sequence received and a mask of the 32 before it; forgets
    // sequences more than 32 old.
    void WriteAcks(BufferCursor* cursor);
    // Makes sure a reliable chunk is queued: a new one of up to min(budget, 255) bytes from the reliable
    // stream when none is waiting, else the oldest waiting one moves to the front.
    void QueueHeader(int budget);
    // A packet was lost: what it carried is sent again and its reliable chunk goes back to the front of the
    // queue (once). `packet` is a history node's data.
    void RollBack(SentPacket* packet);
    // Brings the tracked entries up to date: the session's CollectReplicated lists what this connection
    // should have; entries whose object is no longer listed are dropped (never sent) or marked gone (sent),
    // then new objects get an entry (Track) in the list's order.
    void CollectChanges();
    // Starts tracking `item`'s object: a new entry (the next serial), kept in byObject and its priority map.
    void Track(const Replicated& item);
    // Clocks, lost packets, sending, delayed id reuse; false when the peer timed out or the reliable stream
    // overflowed.
    bool Tick(double dt);
    // A packet arrived: the round-trip averages take its age; removals it carried end their entries (the id
    // goes to freedNewer).
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
    // The peer goes away: every object received from it goes to the session's OnMessage (Removed) and is
    // released.
    void Close();

private:
    // Gives `entry` an id from its type's pool (kept in bySentId) unless it has one.
    void AssignId(ReplicaEntry* entry);
    // A new object of `type` (CreateMessage) into `record` (version 0), remembered as (type, objectId) in attached;
    // null (the record left empty) for a type no class has.
    Message* CreateRecord(Record& record, std::uint8_t type, std::uint16_t objectId);
};
} // namespace net
