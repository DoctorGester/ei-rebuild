// A connection's construction, reset, clone and destruction, the id pools' copy and the replica entries'
// slots. See net_connection.h and docs/notes/network.md.
#include "net_coder.h"

#include "core_streams.h"
#include "net_connection.h"
#include "lib/bytes.h"
#include "lib/float_select.h"
#include "lib/nan_audit.h"
#include "platform/clock.h"

#include <cmath>
#include <cstring>
#include <iterator>
#include <memory>
#include <vector>

namespace net {

namespace {

void Put32(void* object, std::size_t offset, std::uint32_t v) { lib::StoreBytes(static_cast<std::uint8_t*>(object) + offset, v); }

// The std::vector<int> of received sequences given back to the pool.
void FreeReceived(Connection* c) { std::destroy_at(&c->received); }

// Every container destroyed in the order the destructor's unwinding would (Reset's teardown).
void DestroyForReset(Connection* c) {
    c->reliableIn.~GrowingBuffer();
    c->reliableOut.~GrowingBuffer();
    std::destroy_at(&c->incoming);
    std::destroy_at(&c->headers);
    std::destroy_at(&c->attached);
    std::destroy_at(&c->records);
    std::destroy_at(&c->acknowledged);
    std::destroy_at(&c->history);
    std::destroy_at(&c->priorities);
    std::destroy_at(&c->byObject);
    std::destroy_at(&c->bySentId);
    std::destroy_at(&c->freedIds);
    for (int i = kMessageTypes; i-- > 0;) std::destroy_at(&c->idPools[i].free);    // the last first
    FreeReceived(c);
}

} // namespace

// The constructor (id). The containers are members, built before the body (the original builds the reliable
// streams last); then: the send sequence starts at the tick count, the round trip at the session's
// maxRoundTrip (or a constant without a session), its square average is the square of what the memory held,
// send interval 0.04, budget 220.
Connection::Connection(std::uint32_t newId) {
    address = NetAddress{};
    for (IdPool& pool : idPools) pool.next = 0;
    std::uint32_t tick = platform::TickCount();
    id = 0;
    SessionBase* session = g_currentSession;
    if (session) {
        roundTrip = static_cast<float>(session->maxRoundTrip);
    } else {
        roundTrip = 5.0f;
    }
    const float old = roundTripSquare;    // never written before: whatever the allocation held
    sendSequence = static_cast<std::uint16_t>(tick);
    Put32(&sendInterval, 0, 0x3D23D70A);    // 0.04f
    sendTimer = 0.0f;
    idleTime = 0.0f;
    updateBudget = 0xDC;
    roundTripSquare = old * old;
    lastReceived = 0;
    sequenceHigh = 0x10000;
    freedIds.resize(2);
    id = newId;
    Put32(&clock, 0, 0);
    freedTimer = 0.0f;
    headerSequence = 0;
    incomingSequence = 0;
    Put32(&clock, 4, 0);
    ackMask = 0;
    ackBase = 0;
}

// Tears every container down (in the order the destructor's unwinding would)
// and constructs the connection again with id 0 (the count and the flags kept).
void Connection::OnShutdown() {
    DestroyForReset(this);
    new (this) Connection(0);
}

// The members are destroyed in the original's order (the reliable streams first, then the lists and maps
// from the last).
Connection::~Connection() = default;

} // namespace net

namespace net {

// The copy below as a new connection.
Connection* Connection::Clone() { return NewCounted<Connection>(*this); }

// The containers and the reliable streams are members, copied before the body (the original copies them in
// order).
Connection::Connection(const Connection& other)
    : RefObject(), freedIds(other.freedIds), bySentId(other.bySentId), byObject(other.byObject),
      priorities(other.priorities), entriesTracked(other.entriesTracked), history(other.history), acknowledged(other.acknowledged),
      records(other.records), attached(other.attached), headers(other.headers), incoming(other.incoming),
      reliableOut(other.reliableOut), reliableIn(other.reliableIn) {
    sendSequence = other.sendSequence;
    lastReceived = other.lastReceived;
    sequenceHigh = other.sequenceHigh;
    received = other.received;
    // id, round trips; timers, clock, budget: copied bit for bit.
    std::memcpy(&id, &other.id, offsetof(Connection, address) - offsetof(Connection, id));
    address = other.address;
    std::memcpy(&sendInterval, &other.sendInterval, offsetof(Connection, idPools) - offsetof(Connection, sendInterval));
    for (int i = 0; i < kMessageTypes; ++i) idPools[i] = other.idPools[i];
    freedTimer = other.freedTimer;
    headerSequence = other.headerSequence;
    incomingSequence = other.incomingSequence;
    ackMask = other.ackMask;
    ackBase = other.ackBase;
}

} // namespace net

// ---- Id pools and replica entries ---------------------------------------------------------------------

namespace net {

ReplicaEntry::ReplicaEntry() {
    version = 0;
    id = 0;
}

ReplicaEntry::ReplicaEntry(const ReplicaEntry& other)
    : RefObject(), version(other.version), id(other.id), type(other.type),
      flags(other.flags), serial(other.serial) {       // the original merges bit 0 and then the other bits: all of them
    object.Assign(other.object);
}

// Entry copy: a copy of the entry sharing the object.
ReplicaEntry* ReplicaEntry::Clone() { return NewCounted<ReplicaEntry>(*this); }

// Entry reset: releases the object, then built again in place as the constructor would (object, version and
// id cleared; type and flags kept). The result means nothing.
void ReplicaEntry::OnShutdown() {
    object.Reset();
    version = 0;
    id = 0;
}

ReplicaEntry::~ReplicaEntry() {}

} // namespace net

// --- Net connection tick -----------------------------------------------------------------------------

// A connection's tick: clocks, lost-packet detection, sending, delayed id reuse and the
// timeout. See net_connection.h and docs/notes/network.md. Float maths: the original's sums round at the main
// thread's 24-bit precision, so its double clocks hold float values (docs/notes/cleanup-tooling.md, "The x87 files
// to SSE2").

namespace net {

namespace {

// The time from `then` to `now` on a connection's clock (doubles holding float values).
float Age(double now, double then) { return static_cast<float>(now) - static_cast<float>(then); }

// The reliable stream's size (the buffer's high mark follows its position first).
std::int32_t ReliableSize(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
    return buffer->PositionOf(buffer->high);
}

constexpr float kRttKeep = 0.93f;   // the share of the old average kept per sample

} // namespace

// Advances the idle time and the clock by `dt`. Packets older than the loss timeout (3 standard deviations
// above the average round trip, at least 0.1, at most the session's maxRoundTrip) are rolled back and move
// to the acknowledged list, where they stay for maxRoundTrip seconds. Sends an update every sendInterval
// (stopping early once the reliable stream and the header queue are empty). Every idReuseTime seconds the
// oldest batch of freed ids returns to the id pools. False once the peer was silent for the session's
// timeout or the reliable stream holds 32000 bytes.
bool Connection::Tick(double dt) {
    g_currentConnection = this;
    const float step = static_cast<float>(dt);
    idleTime += step;
    clock = static_cast<float>(clock) + step;
    const float spread = AbsBits(roundTrip * roundTrip - roundTripSquare);
    float loss = std::sqrt(spread) * 3.0f + roundTrip;
    const float tenth = 0.1f;
    loss = MaxKeepB(loss, tenth);
    const auto longest = static_cast<float>(g_currentSession->maxRoundTrip);
    loss = MinKeepA(longest, loss);
    const auto kept = static_cast<float>(g_currentSession->maxRoundTrip);

    auto lost = history.begin();
    for (; lost != history.end(); ++lost) {
        const float age = Age(clock, lost->time);
        EI_NAN_AUDIT(loss, age);
        if (loss >= age) break;
        RollBack(&*lost);
    }
    acknowledged.insert(acknowledged.end(), std::make_move_iterator(history.begin()), std::make_move_iterator(lost));
    history.erase(history.begin(), lost);
    std::erase_if(acknowledged, [&](const SentPacket& packet) {
        return kept < Age(clock, packet.time);   // NaN: kept
    });

    sendTimer += step;
    if (sendTimer > sendInterval) {
        do {
            sendTimer -= sendInterval;
            if (SendUpdate(static_cast<int>(updateBudget)) && headers.empty()) {
                if (ReliableSize(&reliableOut) == 0) break;
            }
        } while (sendTimer > sendInterval);
    }
    while (sendTimer > sendInterval) sendTimer -= sendInterval;

    freedTimer += step;
    if (freedTimer > static_cast<float>(g_currentSession->idReuseTime)) {
        freedTimer = 0.0f;
        std::vector<FreedId>& oldest = freedIds.front();
        for (auto& e : oldest) {
            auto entryId = static_cast<std::uint16_t>(e.id);
            std::vector<std::uint16_t>& pool = idPools[e.type].free;
            pool.push_back(entryId);
        }
        oldest.clear();
        freedIds.erase(freedIds.begin());
        freedIds.emplace_back();
    }

    bool alive = false;
    if (static_cast<float>(g_currentSession->timeout) > idleTime) alive = ReliableSize(&reliableOut) < 32000;
    g_currentConnection = nullptr;
    return alive;
}

} // namespace net

namespace net {

void Connection::Delivered(SentPacket* packet) {
    const float sample = Age(clock, packet->time);
    roundTrip = (1.0f - kRttKeep) * sample + kRttKeep * roundTrip;
    roundTripSquare = (1.0f - kRttKeep) * sample * sample + kRttKeep * roundTripSquare;
    for (SentItem& sent : packet->items) {
        SentItem* item = &sent;
        auto action = static_cast<std::int32_t>(item->action);
        if (action == SentItem::kChangedReliable) {
            std::uint32_t version = item->version;
            if (item->entry->version > version) version = item->entry->version;
            item->entry->version = version;
        } else if (action > 1 && action < 4) {
            ReplicaEntry* entry = item->entry;
            std::map<std::uint32_t, EntryRef>& tree = priorities[entry->flags >> 1];
            auto found = tree.find(entry->serial);
            if (found != tree.end()) {
                entry = item->entry;
                std::int32_t key = (static_cast<std::int32_t>(entry->type) << 16) + entry->id;
                bySentId.erase(key);
                FreedId value{};
                value.id = entry->id;
                value.type = entry->type;
                std::vector<FreedId>& batch = freedIds.back();    // the newest batch
                batch.push_back(value);
                tree.erase(found);
            }
        }
    }
}

} // namespace net
