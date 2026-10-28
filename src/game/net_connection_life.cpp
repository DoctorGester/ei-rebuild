// A connection's construction, reset, clone and destruction, the id pools' copy and the replica entries'
// slots. See net_connection.h and docs/notes/network.md.
#include "net_coder.h"
#include "net_connection.h"

#include "core_streams.h"
#include "lib/gameapi.h"

#include <cstring>
#include <memory>

namespace net {

namespace {

void Put32(void* object, std::size_t offset, std::uint32_t v) { std::memcpy(static_cast<std::uint8_t*>(object) + offset, &v, 4); }

// The std::vector<int> of received sequences given back to the pool.
void FreeReceived(Connection* c) { std::destroy_at(&c->received_); }

// Every container destroyed in the order the destructor's unwinding would (Reset's teardown).
void DestroyForReset(Connection* c) {
    c->reliableIn_.~GrowingBuffer();
    c->reliableOut_.~GrowingBuffer();
    std::destroy_at(&c->incoming_);
    std::destroy_at(&c->headers_);
    std::destroy_at(&c->attached_);
    std::destroy_at(&c->records_);
    std::destroy_at(&c->acknowledged_);
    std::destroy_at(&c->history_);
    std::destroy_at(&c->priorities_);
    std::destroy_at(&c->byObject_);
    std::destroy_at(&c->bySentId_);
    std::destroy_at(&c->freedIds_);
    for (int i = kMessageTypes; i-- > 0;) std::destroy_at(&c->idPools_[i].free);   // the last first
    FreeReceived(c);
}

// The entry's counted object pointer released as the original inlines it (16-bit count; slot 0
// deletes at zero).
void ReleaseEntryObject(ReplicaEntry* e) {
    CRefObject* object = e->object_.ptr;
    if (object && --object->refs_ == 0) delete object;
}

} // namespace

// The constructor (id). The containers are members, built before the body (the original builds the reliable
// streams last); then: the send sequence starts at the tick count, the round trip at the session's
// maxRoundTrip_ (or a constant without a session), its square average is the square of what the memory held,
// send interval 0.04, budget 220.
Connection::Connection(std::uint32_t id) {
    address_ = NetAddress{};
    for (IdPool& pool : idPools_) pool.next = 0;
    std::uint32_t tick = gameapi::GetTickCount();
    id_ = 0;
    SessionBase* session = g_currentSession;
    if (session) {
        roundTrip_ = static_cast<float>(static_cast<double>(session->maxRoundTrip_));
    } else {
        roundTrip_ = 5.0f /*0073BF34*/;
    }
    double old = roundTripSquare_;   // never written before: whatever the allocation held
    sendSequence_ = static_cast<std::uint16_t>(tick);
    Put32(&sendInterval_, 0, 0x3D23D70A);   // 0.04f
    sendTimer_ = 0.0f;
    idleTime_ = 0.0f;
    updateBudget_ = 0xDC;
    roundTripSquare_ = static_cast<float>(old * old);
    lastReceived_ = 0;
    sequenceHigh_ = 0x10000;
    freedIds_.resize(2);
    id_ = id;
    Put32(&clock_, 0, 0);
    freedTimer_ = 0.0f;
    headerSequence_ = 0;
    incomingSequence_ = 0;
    Put32(&clock_, 4, 0);
    ackMask_ = 0;
    ackBase_ = 0;
}

// Tears every container down (in the order the destructor's unwinding would)
// and constructs the connection again with id 0 (the count and the flags kept).
int Connection::OnShutdown() {
    DestroyForReset(this);
    new (this) Connection(0);
    return 0;
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
    : CRefObject(), freedIds_(other.freedIds_), bySentId_(other.bySentId_), byObject_(other.byObject_),
      priorities_(other.priorities_), history_(other.history_), acknowledged_(other.acknowledged_),
      records_(other.records_), attached_(other.attached_), headers_(other.headers_), incoming_(other.incoming_),
      reliableOut_(other.reliableOut_), reliableIn_(other.reliableIn_) {
    sendSequence_ = other.sendSequence_;
    lastReceived_ = other.lastReceived_;
    sequenceHigh_ = other.sequenceHigh_;
    received_ = other.received_;
    // id, round trips; timers, clock, budget: copied as dwords.
    std::memcpy(&id_, &other.id_, offsetof(Connection, address_) - offsetof(Connection, id_));
    address_ = other.address_;
    std::memcpy(&sendInterval_, &other.sendInterval_, offsetof(Connection, idPools_) - offsetof(Connection, sendInterval_));
    for (int i = 0; i < kMessageTypes; ++i) idPools_[i] = other.idPools_[i];
    freedTimer_ = other.freedTimer_;
    headerSequence_ = other.headerSequence_;
    incomingSequence_ = other.incomingSequence_;
    ackMask_ = other.ackMask_;
    ackBase_ = other.ackBase_;
}

} // namespace net

// ---- Id pools and replica entries ---------------------------------------------------------------------

namespace net {

ReplicaEntry::ReplicaEntry() {
    object_.FromRawCounted(nullptr);
    version_ = 0;
    id_ = 0;
}

ReplicaEntry::ReplicaEntry(const ReplicaEntry& other)
    : CRefObject(), version_(other.version_), id_(other.id_), type_(other.type_),
      flags_(other.flags_) {   // the original merges bit 0 and then the other bits: all of them
    object_.ptr = other.object_.ptr;
    if (object_.ptr) ++object_.ptr->refs_;
}

// Entry copy: a copy of the entry sharing the object.
ReplicaEntry* ReplicaEntry::Clone() { return NewCounted<ReplicaEntry>(*this); }

// Entry reset: releases the object, then built again in place as the constructor would (object, version and
// id cleared; type and flags kept). The result means nothing.
int ReplicaEntry::OnShutdown() {
    ReleaseEntryObject(this);
    object_.ptr = nullptr;
    version_ = 0;
    id_ = 0;
    return 0;
}

ReplicaEntry::~ReplicaEntry() { ReleaseEntryObject(this); }

} // namespace net
