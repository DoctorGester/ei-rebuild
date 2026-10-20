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
#include "originals_script.h"
#include "originals_library.h"

#include <cstring>

namespace net {

namespace {

using ObjectMap = stlp::map<CRefObject*, RefPtr>;
using SentIdMap = stlp::map<std::int32_t, RefPtr>;
using AttachedMap = stlp::map<CRefObject*, AttachedId>;

// CRT eh vector constructor / destructor iterators (stdcall).
void __stdcall VectorCtorIterator(void* a0, unsigned a1, int a2, void* a3, void* a4) { libcrt::ConstructElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3), reinterpret_cast<libcrt::ElementFunction>(a4)); }
void __stdcall VectorDtorIterator(void* a0, unsigned a1, int a2, void* a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }
}  // namespace
IdPool* __fastcall IdPoolConstruct(IdPool* self);   // below
namespace {
// An id pool's destructor: its vector of free ids freed.
void __fastcall IdPoolDestruct(IdPool* pool) { pool->free.FreeStorage(); }
// A function as the pointer the iterators take.
template <typename F>
void* Callback(F function) { return reinterpret_cast<void*>(function); }

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
    GrowingBufferDestruct(&c->reliableIn_);
    GrowingBufferDestruct(&c->reliableOut_);
    c->incoming_.Release<ChunkElement>();   // 00439D40
    c->headers_.Release<ChunkElement>();
    c->attached_.Destroy<stlp::TreeElement<AttachedMap::Value>>();
    trees::Records::Destroy(&c->records_);
    c->acknowledged_.Release<PacketRecordElement>();   // 004399C0
    c->history_.Release<PacketRecordElement>();
    VectorDtorIterator(c->priorities_, sizeof(Connection::EntryMap), 11, Callback(trees::Entries::Destroy));
    trees::Objects::Clear(&c->byObject_);
    trees::Objects::FreeHeader(&c->byObject_);
    trees::SentIds::Clear(&c->bySentId_);   // 004394A0
    trees::SentIds::FreeHeader(&c->bySentId_);   // 00439500
    c->freedIds_.Clear<FreedIdsElement>();   // 0043BA20
    stlp::deallocate(c->freedIds_.End(), sizeof(FreedBatch));   // 0043A410
    VectorDtorIterator(c->idPools_, sizeof(IdPool), kMessageTypes, Callback(IdPoolDestruct));
    FreeReceived(c);
}

} // namespace

// 00432DA0: the constructor (id). Builds every container, then: the send sequence starts at the
// tick count, the round trip at the session's maxRoundTrip_ (or 0073BF34 without a session), its
// square average is the square of what the memory held, send interval 0.04, budget 220.
Connection* Connection::Construct(std::uint32_t id) {
    ei::SetVtable(this, CRefObject::kVtableAddress);
    std::uint8_t tags[4] = {};   // the empty allocator / comparison objects the constructors take
    received_.ConstructEmpty();   // vector<int>() (00438CE0)
    address_ = NetAddress{};
    VectorCtorIterator(idPools_, sizeof(IdPool), kMessageTypes, Callback(IdPoolConstruct), Callback(IdPoolDestruct));
    freedIds_.Construct();   // 00439120
    trees::SentIds::ConstructWith(&bySentId_, nullptr, &tags[3], &tags[2]);   // 004393C0
    trees::Objects::ConstructWith(&byObject_, nullptr, &tags[2], &tags[3]);
    VectorCtorIterator(priorities_, sizeof(EntryMap), 11, Callback(trees::Entries::Construct), Callback(trees::Entries::Destroy));
    history_.Construct();   // 00439960
    acknowledged_.Construct();
    // The trees' comparison byte is copied from the stack slot of the id argument.
    trees::Records::ConstructBase(&records_, nullptr, &tags[3]);
    records_.Count() = 0;
    records_.Compare() = static_cast<std::uint8_t>(id);
    records_.EmptyInitialize();
    trees::Attached::ConstructBase(&attached_, nullptr, &tags[3]);
    attached_.Compare() = static_cast<std::uint8_t>(id);
    attached_.Count() = 0;
    attached_.EmptyInitialize();
    headers_.Construct();   // 00439CE0
    // The list's allocator proxy (0043A980) holds null until the sentinel is made.
    auto* sentinel = static_cast<HeaderNode*>(NodeAllocate(sizeof(HeaderNode)));
    incoming_.head = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    static_cast<GameBuffer*>(&reliableOut_)->ConstructGrowing();
    static_cast<GameBuffer*>(&reliableIn_)->ConstructGrowing();
    ei::SetVtable(this, kVtableAddress);
    std::uint32_t tick = gameapi::GetTickCount();
    id_ = 0;
    SessionBase* session = CurrentSession();
    if (session) {
        roundTrip_ = static_cast<float>(static_cast<double>(session->maxRoundTrip_));
    } else {
        roundTrip_ = *reinterpret_cast<const float*>(0x0073BF34);
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
    return this;
}
EI_REPLACE_METHOD(0x00432DA0, Connection::Construct);

// 00433320 (slot 2): tears every container down (in the order the destructor's unwinding would)
// and constructs the connection again with id 0.
int Connection::OnShutdown() {
    ei::SetVtable(this, kVtableAddress);
    DestroyForReset(this);
    ei::SetVtable(this, CRefCounted::kVtableAddress);
    return static_cast<int>(reinterpret_cast<std::uintptr_t>(Construct(0)));
}
namespace {
int __fastcall ResetHook(Connection* self) { return self->Connection::OnShutdown(); }
} // namespace
EI_REPLACE(0x00433320, ResetHook);

// 004334E0: the destructor body.
void Connection::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    GrowingBufferDestruct(&reliableIn_);
    GrowingBufferDestruct(&reliableOut_);
    incoming_.Clear<ChunkElement>();   // 0043D420
    stlp::deallocate(incoming_.End(), sizeof(HeaderNode));   // 00439D80
    headers_.Clear<ChunkElement>();
    stlp::deallocate(headers_.End(), sizeof(HeaderNode));
    attached_.Clear<stlp::TreeElement<AttachedMap::Value>>();
    trees::Attached::FreeHeader(&attached_);
    trees::Records::Clear(&records_);
    trees::Records::FreeHeader(&records_);
    acknowledged_.Clear<PacketRecordElement>();   // 0043CA40
    stlp::deallocate(acknowledged_.End(), sizeof(HistoryNode));   // 0043AAF0
    history_.Clear<PacketRecordElement>();
    stlp::deallocate(history_.End(), sizeof(HistoryNode));
    VectorDtorIterator(priorities_, sizeof(EntryMap), 11, Callback(trees::Entries::Destroy));
    trees::Objects::Clear(&byObject_);
    stlp::deallocate(byObject_.end(), 24);   // 0043A4E0
    trees::SentIds::Clear(&bySentId_);
    stlp::deallocate(bySentId_.end(), 24);   // 0043A480
    freedIds_.Clear<FreedIdsElement>();
    stlp::deallocate(freedIds_.End(), sizeof(FreedBatch));   // 0043B180
    VectorDtorIterator(idPools_, sizeof(IdPool), kMessageTypes, Callback(IdPoolDestruct));
    FreeReceived(this);
    ei::SetVtable(this, CRefCounted::kVtableAddress);
}
EI_REPLACE_METHOD(0x004334E0, Connection::Destruct);

// 004334C0 (slot 0): the deleting destructor.
Connection* Connection::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004334C0, Connection::DeletingDestruct);

} // namespace net

namespace net {

// 00433060 (slot 3).
Connection* Connection::Clone() {
    auto* copy = NewMessageMemory<Connection>();
    if (!copy) return nullptr;
    ei::SetVtable(copy, CRefObject::kVtableAddress);
    copy->sendSequence_ = sendSequence_;
    copy->lastReceived_ = lastReceived_;
    copy->sequenceHigh_ = sequenceHigh_;
    // vector(const vector&): room for exactly size() sequences (00438D00), copied (0043B360).
    copy->received_.ConstructWithCapacity(received_.size());
    copy->received_.Finish() = stlp::CopyTrivial(received_.begin(), received_.end(), copy->received_.begin());
    // id, round trips; timers, clock, budget: copied as dwords.
    std::memcpy(&copy->id_, &id_, offsetof(Connection, address_) - offsetof(Connection, id_));
    copy->address_ = address_;
    std::memcpy(&copy->sendInterval_, &sendInterval_, offsetof(Connection, idPools_) - offsetof(Connection, sendInterval_));
    for (int i = 0; i < kMessageTypes; ++i) copy->idPools_[i].CopyFrom(idPools_[i]);
    netlists::CopyConstruct<FreedIds, FreedIdsElement>(&copy->freedIds_, nullptr, &freedIds_);   // 00437930
    copy->freedTimer_ = freedTimer_;
    trees::SentIds::CopyConstruct(&copy->bySentId_, nullptr, &bySentId_);   // 00437AA0
    trees::Objects::CopyConstruct(&copy->byObject_, nullptr, &byObject_);
    for (int i = 0; i < 11; ++i) trees::Entries::CopyConstruct(&copy->priorities_[i], nullptr, &priorities_[i]);
    netlists::CopyConstruct<SentPacket, PacketRecordElement>(&copy->history_, nullptr, &history_);   // 00437F60
    netlists::CopyConstruct<SentPacket, PacketRecordElement>(&copy->acknowledged_, nullptr, &acknowledged_);
    trees::Records::CopyConstruct(&copy->records_, nullptr, &records_);
    trees::Attached::CopyConstruct(&copy->attached_, nullptr, &attached_);
    copy->headerSequence_ = headerSequence_;
    copy->incomingSequence_ = incomingSequence_;
    netlists::CopyConstruct<ReliableChunk, ChunkElement>(&copy->headers_, nullptr, &headers_);   // 00438690
    netlists::CopyConstruct<ReliableChunk, ChunkElement>(&copy->incoming_, nullptr, &incoming_);
    copy->ackMask_ = ackMask_;
    copy->ackBase_ = ackBase_;
    copy->reliableOut_.ConstructCopy(&reliableOut_);
    copy->reliableIn_.ConstructCopy(&reliableIn_);
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}
namespace {
Connection* __fastcall CloneHook(Connection* self) { return self->Connection::Clone(); }
} // namespace
EI_REPLACE(0x00433060, CloneHook);

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
EI_REPLACE_METHOD(0x00433A30, IdPool::CopyFrom);

namespace {
// The entry's counted object pointer released as the original inlines it (16-bit count; slot 0
// deletes at zero).
void ReleaseEntryObject(ReplicaEntry* e) {
    CRefObject* object = e->object_.ptr;
    if (object && --object->refs_ == 0) delete object;
}
} // namespace

// 004343C0 (entry slot 3): a copy of the entry sharing the object (one more reference).
ReplicaEntry* ReplicaEntry::Clone() {
    auto* copy = NewMessageMemory<ReplicaEntry>();
    if (!copy) return nullptr;
    ei::SetVtable(copy, CRefObject::kVtableAddress);
    copy->version_ = version_;
    copy->id_ = id_;
    copy->type_ = type_;
    copy->flags_ = flags_;   // the original merges bit 0 and then the other bits: all of them
    copy->object_.ptr = object_.ptr;
    if (copy->object_.ptr) ++copy->object_.ptr->refs_;
    ei::SetVtable(copy, kVtableAddress);
    return copy;
}

// 00434430 (entry slot 2): releases the object, then constructs again in place (object, version
// and id cleared; type and flags kept). Returns what EAX held (nothing in particular).
int ReplicaEntry::OnShutdown() {
    ei::SetVtable(this, kVtableAddress);
    ReleaseEntryObject(this);
    ei::SetVtable(this, CRefCounted::kVtableAddress);
    object_.ptr = nullptr;
    ei::SetVtable(this, kVtableAddress);
    version_ = 0;
    id_ = 0;
    return 0;
}

namespace {
ReplicaEntry* __fastcall ReplicaEntryCloneHook(ReplicaEntry* self) { return self->ReplicaEntry::Clone(); }
int __fastcall ReplicaEntryResetHook(ReplicaEntry* self) { return self->ReplicaEntry::OnShutdown(); }
} // namespace
EI_REPLACE(0x004343C0, ReplicaEntryCloneHook);
EI_REPLACE(0x00434430, ReplicaEntryResetHook);

// 004344C0: the destructor body.
void ReplicaEntry::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    ReleaseEntryObject(this);
    ei::SetVtable(this, CRefCounted::kVtableAddress);
}
EI_REPLACE_METHOD(0x004344C0, ReplicaEntry::Destruct);

// 004344A0 (entry slot 0): the deleting destructor.
ReplicaEntry* ReplicaEntry::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004344A0, ReplicaEntry::DeletingDestruct);

// 00433040 (no Ghidra function; the element constructor the connection's constructor passes to the
// vector constructor iterator): an empty id pool (no free ids, next 0). Returns this.
IdPool* __fastcall IdPoolConstruct(IdPool* self) {
    self->free.ConstructEmpty();
    self->next = 0;
    return self;
}
EI_REPLACE(0x00433040, IdPoolConstruct);

} // namespace net
