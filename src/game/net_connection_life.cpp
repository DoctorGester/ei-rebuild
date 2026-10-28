// A connection's construction, reset, clone and destruction, the id pools' copy and the replica entries'
// slots. See net_connection.h and docs/notes/network.md.
#include "net_coder.h"
#include "net_connection.h"
#include "net_connection_trees.h"
#include "library_net_lists.h"

#include "core_streams.h"
#include "lib/stlp_vector.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp_tree_abi.h"
#include "game_data.h"

#include <cstring>

namespace net {

IdPool* IdPoolConstruct(IdPool* self);   // below
namespace {
// An id pool's destructor: its vector of free ids freed.
void IdPoolDestruct(IdPool* pool) { pool->free.FreeStorage(); }
// The CRT's eh vector constructor / destructor iterators take them as libcrt::Element<...>.
using libcrt::Element;

// The lists' element policies (library_net_lists.h).
using netlists::ChunkElement;
using netlists::FreedIdsElement;
using netlists::PacketRecordElement;
inline void* NodeAllocate(std::size_t a0) { return libcrt::NodeAllocate(a0); }

void Put32(void* object, std::size_t offset, std::uint32_t v) { std::memcpy(static_cast<std::uint8_t*>(object) + offset, &v, 4); }

// The std::vector<int> of received sequences given back to the pool.
void FreeReceived(Connection* c) { c->received_.FreeStorage(); }

// Every container destroyed in the order the destructor's unwinding would (Reset's teardown).
void DestroyForReset(Connection* c) {
    c->reliableIn_.~GrowingBuffer();
    c->reliableOut_.~GrowingBuffer();
    c->incoming_.Release<ChunkElement>();
    c->headers_.Release<ChunkElement>();
    trees::Attached::Destroy(&c->attached_);
    trees::Records::Destroy(&c->records_);
    c->acknowledged_.Release<PacketRecordElement>();
    c->history_.Release<PacketRecordElement>();
    libcrt::DestroyElements(c->priorities_, sizeof(Connection::EntryMap), 11, Element<trees::Entries::Destroy>);
    trees::Objects::Clear(&c->byObject_);
    trees::Objects::FreeHeader(&c->byObject_);
    trees::SentIds::Clear(&c->bySentId_);
    trees::SentIds::FreeHeader(&c->bySentId_);
    c->freedIds_.Clear<FreedIdsElement>();
    stlp::deallocate(c->freedIds_.End(), sizeof(FreedBatch));
    libcrt::DestroyElements(c->idPools_, sizeof(IdPool), kMessageTypes, Element<IdPoolDestruct>);
    FreeReceived(c);
}

} // namespace

// The constructor (id). Builds every container, then: the send sequence starts at the
// tick count, the round trip at the session's maxRoundTrip_ (or a constant without a session), its
// square average is the square of what the memory held, send interval 0.04, budget 220.
// The reliable streams are members, built before the body (the original builds them last).
Connection::Connection(std::uint32_t id) {
    std::uint8_t tags[4] = {};   // the empty allocator / comparison objects the constructors take
    received_.ConstructEmpty();
    address_ = NetAddress{};
    libcrt::ConstructElements(idPools_, sizeof(IdPool), kMessageTypes, Element<IdPoolConstruct>, Element<IdPoolDestruct>);
    freedIds_.Construct();
    bySentId_.ConstructWith(&tags[3], &tags[2]);
    byObject_.ConstructWith(&tags[2], &tags[3]);
    libcrt::ConstructElements(priorities_, sizeof(EntryMap), 11, Element<trees::Entries::Construct>,
                              Element<trees::Entries::Destroy>);
    history_.Construct();
    acknowledged_.Construct();
    // The trees' comparison byte is copied from the stack slot of the id argument.
    records_.ConstructBase(&tags[3]);
    records_.Count() = 0;
    records_.Compare() = static_cast<std::uint8_t>(id);
    trees::Records::EmptyInitialize(&records_);
    attached_.ConstructBase(&tags[3]);
    attached_.Compare() = static_cast<std::uint8_t>(id);
    attached_.Count() = 0;
    trees::Attached::EmptyInitialize(&attached_);
    headers_.Construct();
    // The list's allocator proxy holds null until the sentinel is made.
    auto* sentinel = static_cast<HeaderNode*>(NodeAllocate(sizeof(HeaderNode)));
    incoming_.head = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
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
    stlp::list<FreedIds>::InsertDefault<FreedIdsElement>(freedIds_.End());
    stlp::list<FreedIds>::InsertDefault<FreedIdsElement>(freedIds_.End());
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

// The reliable streams are members, destroyed after the body (the original frees them first).
Connection::~Connection() {
    incoming_.Clear<ChunkElement>();
    stlp::deallocate(incoming_.End(), sizeof(HeaderNode));
    headers_.Clear<ChunkElement>();
    stlp::deallocate(headers_.End(), sizeof(HeaderNode));
    trees::Attached::Clear(&attached_);
    trees::Attached::FreeHeader(&attached_);
    trees::Records::Clear(&records_);
    trees::Records::FreeHeader(&records_);
    acknowledged_.Clear<PacketRecordElement>();
    stlp::deallocate(acknowledged_.End(), sizeof(HistoryNode));
    history_.Clear<PacketRecordElement>();
    stlp::deallocate(history_.End(), sizeof(HistoryNode));
    libcrt::DestroyElements(priorities_, sizeof(EntryMap), 11, Element<trees::Entries::Destroy>);
    trees::Objects::Clear(&byObject_);
    stlp::deallocate(byObject_.end(), 24);
    trees::SentIds::Clear(&bySentId_);
    stlp::deallocate(bySentId_.end(), 24);
    freedIds_.Clear<FreedIdsElement>();
    stlp::deallocate(freedIds_.End(), sizeof(FreedBatch));
    libcrt::DestroyElements(idPools_, sizeof(IdPool), kMessageTypes, Element<IdPoolDestruct>);
    FreeReceived(this);
}

} // namespace net

namespace net {

// The copy below as a new connection.
Connection* Connection::Clone() { return NewCounted<Connection>(*this); }

// The reliable streams are members, copied before the body (the original copies them last).
Connection::Connection(const Connection& other)
    : CRefObject(), reliableOut_(other.reliableOut_), reliableIn_(other.reliableIn_) {
    sendSequence_ = other.sendSequence_;
    lastReceived_ = other.lastReceived_;
    sequenceHigh_ = other.sequenceHigh_;
    // vector(const vector&): room for exactly size() sequences, copied.
    received_.ConstructWithCapacity(other.received_.size());
    received_.Finish() = stlp::CopyTrivial(other.received_.begin(), other.received_.end(), received_.begin());
    // id, round trips; timers, clock, budget: copied as dwords.
    std::memcpy(&id_, &other.id_, offsetof(Connection, address_) - offsetof(Connection, id_));
    address_ = other.address_;
    std::memcpy(&sendInterval_, &other.sendInterval_, offsetof(Connection, idPools_) - offsetof(Connection, sendInterval_));
    for (int i = 0; i < kMessageTypes; ++i) idPools_[i].CopyFrom(other.idPools_[i]);
    freedIds_.CopyConstruct<FreedIdsElement>(other.freedIds_);
    freedTimer_ = other.freedTimer_;
    bySentId_.CopyConstruct(&other.bySentId_);
    byObject_.CopyConstruct(&other.byObject_);
    for (int i = 0; i < 11; ++i) priorities_[i].CopyConstruct(&other.priorities_[i]);
    history_.CopyConstruct<PacketRecordElement>(other.history_);
    acknowledged_.CopyConstruct<PacketRecordElement>(other.acknowledged_);
    records_.CopyConstruct(&other.records_);
    attached_.CopyConstruct(&other.attached_);
    headerSequence_ = other.headerSequence_;
    incomingSequence_ = other.incomingSequence_;
    headers_.CopyConstruct<ChunkElement>(other.headers_);
    incoming_.CopyConstruct<ChunkElement>(other.incoming_);
    ackMask_ = other.ackMask_;
    ackBase_ = other.ackBase_;
}

} // namespace net

// ---- Id pools and replica entries ---------------------------------------------------------------------

namespace net {

IdPool* IdPool::CopyFrom(const IdPool& other) {
    free.ConstructEmpty();
    std::size_t count = other.free.size();
    std::uint16_t* block = count ? static_cast<std::uint16_t*>(NodeAllocate(count * 2)) : nullptr;
    free.Start() = block;
    free.Finish() = block;
    free.EndOfStorage() = block + count;
    std::memmove(block, other.free.begin(), count * 2);
    free.Finish() = block + count;
    next = other.next;
    return this;
}

namespace {
// The entry's counted object pointer released as the original inlines it (16-bit count; slot 0
// deletes at zero).
void ReleaseEntryObject(ReplicaEntry* e) {
    CRefObject* object = e->object_.ptr;
    if (object && --object->refs_ == 0) delete object;
}
} // namespace

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

// An empty id pool (no free ids, next 0); the connection's constructor passes it to the vector constructor
// iterator.
IdPool* IdPoolConstruct(IdPool* self) {
    self->free.ConstructEmpty();
    self->next = 0;
    return self;
}

} // namespace net
