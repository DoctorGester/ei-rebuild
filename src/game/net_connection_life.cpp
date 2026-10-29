// A connection's construction, reset, clone and destruction, the id pools' copy and the replica entries'
// slots. See net_connection.h and docs/notes/network.md.
#include "net_coder.h"
#include "net_connection.h"

#include "core_streams.h"
#include "lib/bytes.h"
#include "lib/gameapi.h"

#include <cstring>
#include <memory>

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
    std::uint32_t tick = gameapi::GetTickCount();
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
