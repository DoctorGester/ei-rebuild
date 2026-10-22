// A connection's construction, reset, clone and destruction (00432DA0, 00433320, 00433060, 004334E0,
// 004334C0), the id pools' copy and the replica entries' slots.
// The containers are STLport instantiations (library), called through their original addresses.
// See net_connection.h and docs/notes/network.md.
#include "net_coder.h"
#include "net_connection.h"
#include "net_connection_trees.h"
#include "library_net_lists.h"

#include "core_streams.h"
#include "ei/hook.h"
#include "lib/stlp_vector.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/stlp_tree_abi.h"
#include "game_data.h"

#include <cstring>

namespace net {

namespace {

using AttachedMap = trees::Attached;

}  // namespace
IdPool* __fastcall IdPoolConstruct(IdPool* self);   // below
namespace {
// An id pool's destructor: its vector of free ids freed.
void __fastcall IdPoolDestruct(IdPool* pool) { pool->free.FreeStorage(); }
// The CRT's eh vector constructor / destructor iterators take them as libcrt::Element<...>.
using libcrt::Element;

// The lists' element policies (library_net_lists.h).
using netlists::ChunkElement;
using netlists::FreedIdsElement;
using netlists::PacketRecordElement;
inline void* __fastcall NodeAllocate(std::size_t a0) { return libcrt::NodeAllocate(a0); }

void Put32(void* object, std::size_t offset, std::uint32_t v) { std::memcpy(static_cast<std::uint8_t*>(object) + offset, &v, 4); }

// The std::vector<int> of received sequences given back to the pool (0043A300).
void FreeReceived(Connection* c) { c->received_.FreeStorage(); }

// Every container destroyed in the order the destructor's unwinding would (Reset's teardown).
void DestroyForReset(Connection* c) {
    static_cast<GameBuffer*>(&c->reliableIn_)->DestructGrowing();
    static_cast<GameBuffer*>(&c->reliableOut_)->DestructGrowing();
    c->incoming_.Release<ChunkElement>();   // 00439D40
    c->headers_.Release<ChunkElement>();
    trees::Attached::Destroy(&c->attached_);
    trees::Records::Destroy(&c->records_);
    c->acknowledged_.Release<PacketRecordElement>();   // 004399C0
    c->history_.Release<PacketRecordElement>();
    libcrt::DestroyElements(c->priorities_, sizeof(Connection::EntryMap), 11, Element<trees::Entries::Destroy>);
    trees::Objects::Clear(&c->byObject_);
    trees::Objects::FreeHeader(&c->byObject_);
    trees::SentIds::Clear(&c->bySentId_);   // 004394A0
    trees::SentIds::FreeHeader(&c->bySentId_);   // 00439500
    c->freedIds_.Clear<FreedIdsElement>();   // 0043BA20
    stlp::deallocate(c->freedIds_.End(), sizeof(FreedBatch));   // 0043A410
    libcrt::DestroyElements(c->idPools_, sizeof(IdPool), kMessageTypes, Element<IdPoolDestruct>);
    FreeReceived(c);
}

} // namespace

// 00432DA0: the constructor (id). Builds every container, then: the send sequence starts at the
// tick count, the round trip at the session's maxRoundTrip_ (or 0073BF34 without a session), its
// square average is the square of what the memory held, send interval 0.04, budget 220.
Connection::Connection(std::uint32_t id) {
    std::uint8_t tags[4] = {};   // the empty allocator / comparison objects the constructors take
    received_.ConstructEmpty();   // vector<int>() (00438CE0)
    address_ = NetAddress{};
    libcrt::ConstructElements(idPools_, sizeof(IdPool), kMessageTypes, Element<IdPoolConstruct>, Element<IdPoolDestruct>);
    freedIds_.Construct();   // 00439120
    bySentId_.ConstructWith(&tags[3], &tags[2]);   // 004393C0
    byObject_.ConstructWith(&tags[2], &tags[3]);
    libcrt::ConstructElements(priorities_, sizeof(EntryMap), 11, Element<trees::Entries::Construct>,
                              Element<trees::Entries::Destroy>);
    history_.Construct();   // 00439960
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
    headers_.Construct();   // 00439CE0
    // The list's allocator proxy (0043A980) holds null until the sentinel is made.
    auto* sentinel = static_cast<HeaderNode*>(NodeAllocate(sizeof(HeaderNode)));
    incoming_.head = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    reliableOut_.ConstructGrowing();
    reliableIn_.ConstructGrowing();
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
    stlp::list<FreedIds>::InsertDefault<FreedIdsElement>(freedIds_.End());   // 00439000
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
EI_REWRITTEN_SPECIAL(0x00432DA0, Connection::Connection);

// 00433320 (slot 2): tears every container down (in the order the destructor's unwinding would)
// and constructs the connection again with id 0 (the count and the flags kept).
int Connection::OnShutdown() {
    DestroyForReset(this);
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(new (this) Connection(0)));
}
EI_REWRITTEN(0x00433320, Connection::OnShutdown);

// 004334E0 (deleting: 004334C0).
Connection::~Connection() {
    static_cast<GameBuffer*>(&reliableIn_)->DestructGrowing();
    static_cast<GameBuffer*>(&reliableOut_)->DestructGrowing();
    incoming_.Clear<ChunkElement>();   // 0043D420
    stlp::deallocate(incoming_.End(), sizeof(HeaderNode));   // 00439D80
    headers_.Clear<ChunkElement>();
    stlp::deallocate(headers_.End(), sizeof(HeaderNode));
    trees::Attached::Clear(&attached_);
    trees::Attached::FreeHeader(&attached_);
    trees::Records::Clear(&records_);
    trees::Records::FreeHeader(&records_);
    acknowledged_.Clear<PacketRecordElement>();   // 0043CA40
    stlp::deallocate(acknowledged_.End(), sizeof(HistoryNode));   // 0043AAF0
    history_.Clear<PacketRecordElement>();
    stlp::deallocate(history_.End(), sizeof(HistoryNode));
    libcrt::DestroyElements(priorities_, sizeof(EntryMap), 11, Element<trees::Entries::Destroy>);
    trees::Objects::Clear(&byObject_);
    stlp::deallocate(byObject_.end(), 24);   // 0043A4E0
    trees::SentIds::Clear(&bySentId_);
    stlp::deallocate(bySentId_.end(), 24);   // 0043A480
    freedIds_.Clear<FreedIdsElement>();
    stlp::deallocate(freedIds_.End(), sizeof(FreedBatch));   // 0043B180
    libcrt::DestroyElements(idPools_, sizeof(IdPool), kMessageTypes, Element<IdPoolDestruct>);
    FreeReceived(this);
}
EI_REWRITTEN_SPECIAL(0x004334E0, Connection::~Connection);
EI_REWRITTEN_SPECIAL(0x004334C0, Connection::~Connection);

} // namespace net

namespace net {

// 00433060 (slot 3): the copy below as a new connection.
Connection* Connection::Clone() { return NewCounted<Connection>(*this); }
EI_REWRITTEN(0x00433060, Connection::Clone);

// The copy as 00433060 builds it, field by field.
Connection::Connection(const Connection& other) : CRefObject() {
    sendSequence_ = other.sendSequence_;
    lastReceived_ = other.lastReceived_;
    sequenceHigh_ = other.sequenceHigh_;
    // vector(const vector&): room for exactly size() sequences (00438D00), copied (0043B360).
    received_.ConstructWithCapacity(other.received_.size());
    received_.Finish() = stlp::CopyTrivial(other.received_.begin(), other.received_.end(), received_.begin());
    // id, round trips; timers, clock, budget: copied as dwords.
    std::memcpy(&id_, &other.id_, offsetof(Connection, address_) - offsetof(Connection, id_));
    address_ = other.address_;
    std::memcpy(&sendInterval_, &other.sendInterval_, offsetof(Connection, idPools_) - offsetof(Connection, sendInterval_));
    for (int i = 0; i < kMessageTypes; ++i) idPools_[i].CopyFrom(other.idPools_[i]);
    freedIds_.CopyConstruct<FreedIdsElement>(other.freedIds_);   // 00437930
    freedTimer_ = other.freedTimer_;
    bySentId_.CopyConstruct(&other.bySentId_);   // 00437AA0
    byObject_.CopyConstruct(&other.byObject_);
    for (int i = 0; i < 11; ++i) priorities_[i].CopyConstruct(&other.priorities_[i]);
    history_.CopyConstruct<PacketRecordElement>(other.history_);   // 00437F60
    acknowledged_.CopyConstruct<PacketRecordElement>(other.acknowledged_);
    records_.CopyConstruct(&other.records_);
    attached_.CopyConstruct(&other.attached_);
    headerSequence_ = other.headerSequence_;
    incomingSequence_ = other.incomingSequence_;
    headers_.CopyConstruct<ChunkElement>(other.headers_);   // 00438690
    incoming_.CopyConstruct<ChunkElement>(other.incoming_);
    ackMask_ = other.ackMask_;
    ackBase_ = other.ackBase_;
    reliableOut_.ConstructCopy(&other.reliableOut_);
    reliableIn_.ConstructCopy(&other.reliableIn_);
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
EI_REWRITTEN(0x00433A30, IdPool::CopyFrom);

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

// 004343C0 (entry slot 3): a copy of the entry sharing the object.
ReplicaEntry* ReplicaEntry::Clone() { return NewCounted<ReplicaEntry>(*this); }
EI_REWRITTEN(0x004343C0, ReplicaEntry::Clone);

// 00434430 (entry slot 2): releases the object, then built again in place as the constructor
// would (object, version and id cleared; type and flags kept). Returns what EAX held (nothing in
// particular).
int ReplicaEntry::OnShutdown() {
    ReleaseEntryObject(this);
    object_.ptr = nullptr;
    version_ = 0;
    id_ = 0;
    return 0;
}
EI_REWRITTEN(0x00434430, ReplicaEntry::OnShutdown);

// 004344C0 (deleting: 004344A0).
ReplicaEntry::~ReplicaEntry() { ReleaseEntryObject(this); }
EI_REWRITTEN_SPECIAL(0x004344C0, ReplicaEntry::~ReplicaEntry);
EI_REWRITTEN_SPECIAL(0x004344A0, ReplicaEntry::~ReplicaEntry);

// 00433040 (no Ghidra function; the element constructor the connection's constructor passes to the
// vector constructor iterator): an empty id pool (no free ids, next 0). Returns this.
IdPool* __fastcall IdPoolConstruct(IdPool* self) {
    self->free.ConstructEmpty();
    self->next = 0;
    return self;
}
EI_REWRITTEN(0x00433040, IdPoolConstruct);

} // namespace net
