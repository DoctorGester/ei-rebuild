// A connection's construction, reset, clone and destruction (00432DA0, 00433320, 00433060, 004334E0,
// 004334C0), the id pools' copy and the replica entries' slots.
// The containers are STLport instantiations (library), called through their original addresses.
// See net_connection.h and docs/notes/network.md.
#include "net_coder.h"
#include "net_connection.h"
#include "net_connection_trees.h"

#include "core_streams.h"
#include "ei/hook.h"
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
using SentIdMap = stlp::map<std::uint32_t, RefPtr>;
using AttachedMap = stlp::map<CRefObject*, AttachedId>;

// CRT eh vector constructor / destructor iterators (stdcall).
void __stdcall VectorCtorIterator(void* a0, unsigned a1, int a2, void* a3, void* a4) { EhVectorConstructorIterator(a0, a1, a2, a3, a4); }
void __stdcall VectorDtorIterator(void* a0, unsigned a1, int a2, void* a3) { libcrt::DestroyElements(a0, a1, a2, reinterpret_cast<libcrt::ElementFunction>(a3)); }
}  // namespace
IdPool* __fastcall IdPoolConstruct(IdPool* self);   // below
namespace {
// An id pool's destructor: its vector of free ids freed.
void __fastcall IdPoolDestruct(IdPool* pool) { stlp::VectorAbi<std::uint16_t>::DestroyBase(reinterpret_cast<stlp::vector<std::uint16_t>*>(pool)); }
// A function as the pointer the iterators take.
template <typename F>
void* Callback(F function) { return reinterpret_cast<void*>(function); }

// Container constructors (allocator / comparison arguments are empty objects passed by address).
__declspec(naked) void __fastcall ReceivedConstruct(std::int32_t** /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00438CE0)
__declspec(naked) void __fastcall FreedListConstruct(FreedBatch** /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439120)
__declspec(naked) void __fastcall BySentIdConstruct(SentIdMap* /*tree*/, void*, const void* /*a*/, const void* /*b*/) EI_ORIGINAL(0x004393C0)
__declspec(naked) void __fastcall HistoryListConstruct(HistoryNode** /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439960)
__declspec(naked) void __fastcall HeaderListConstruct(HeaderNode** /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439CE0)
__declspec(naked) void __fastcall IncomingProxyConstruct(HeaderNode** /*list*/, void*, const void* /*alloc*/, const void* /*value*/) EI_ORIGINAL(0x0043A980)
inline void* __fastcall NodeAllocate(std::size_t a0) { return NodeAllocateOriginal(a0); }

// Reset's container teardown.
__declspec(naked) void __fastcall ListDeleteAll(HeaderNode** /*list*/) EI_ORIGINAL(0x00439D40)
__declspec(naked) void __fastcall HistoryDestroy(HistoryNode** /*list*/) EI_ORIGINAL(0x004399C0)
__declspec(naked) void __fastcall BySentIdClear(SentIdMap* /*tree*/) EI_ORIGINAL(0x004394A0)
__declspec(naked) void __fastcall BySentIdFreeHeader(SentIdMap* /*tree*/) EI_ORIGINAL(0x00439500)
__declspec(naked) void __fastcall FreedClear(FreedBatch** /*list*/) EI_ORIGINAL(0x0043BA20)
__declspec(naked) void __fastcall FreedFreeSentinelA(FreedBatch** /*list*/, void*, FreedBatch* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043A410)
__declspec(naked) void __fastcall ReceivedDeallocate(std::int32_t** /*eos*/, void*, std::int32_t* /*start*/, int /*count*/) EI_ORIGINAL(0x0043A300)

// The destructor's container teardown.
__declspec(naked) void __fastcall ListDeleteNodes(HeaderNode** /*list*/) EI_ORIGINAL(0x0043D420)
__declspec(naked) void __fastcall HeaderSentinelFree(HeaderNode** /*list*/, void*, HeaderNode* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x00439D80)
__declspec(naked) void __fastcall HistoryClear(HistoryNode** /*list*/) EI_ORIGINAL(0x0043CA40)
__declspec(naked) void __fastcall HistorySentinelFree(HistoryNode* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043AAF0)
__declspec(naked) void __fastcall ByObjectHeaderFree(ObjectMap* /*tree*/, void*, ObjectMap::Node* /*header*/, int /*n*/) EI_ORIGINAL(0x0043A4E0)
__declspec(naked) void __fastcall BySentIdHeaderFree(SentIdMap* /*tree*/, void*, SentIdMap::Node* /*header*/, int /*n*/) EI_ORIGINAL(0x0043A480)
__declspec(naked) void __fastcall FreedSentinelFreeB(FreedBatch* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043B180)

void Put32(void* object, std::size_t offset, std::uint32_t v) { std::memcpy(static_cast<std::uint8_t*>(object) + offset, &v, 4); }

// The std::vector<int> of received sequences given back to the pool.
void FreeReceived(Connection* c) {
    ReceivedDeallocate(&c->receivedEos_, nullptr, c->received_, static_cast<int>(c->receivedEos_ - c->received_));
}

// Every container destroyed in the order the destructor's unwinding would (Reset's teardown).
void DestroyForReset(Connection* c) {
    GrowingBufferDestruct(&c->reliableIn_);
    GrowingBufferDestruct(&c->reliableOut_);
    ListDeleteAll(&c->incoming_);
    ListDeleteAll(&c->headers_);
    c->attached_.Destroy<stlp::TreeElement<AttachedMap::Value>>();
    trees::Records::Destroy(&c->records_);
    HistoryDestroy(&c->acknowledged_);
    HistoryDestroy(&c->history_);
    VectorDtorIterator(c->priorities_, sizeof(Connection::EntryMap), 11, Callback(trees::Entries::Destroy));
    trees::Objects::Clear(&c->byObject_);
    trees::Objects::FreeHeader(&c->byObject_);
    BySentIdClear(&c->bySentId_);
    BySentIdFreeHeader(&c->bySentId_);
    FreedClear(&c->freedIds_);
    FreedFreeSentinelA(&c->freedIds_, nullptr, c->freedIds_, 1);
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
    ReceivedConstruct(&received_, nullptr, &tags[2]);
    address_ = NetAddress{};
    VectorCtorIterator(idPools_, sizeof(IdPool), kMessageTypes, Callback(IdPoolConstruct), Callback(IdPoolDestruct));
    FreedListConstruct(&freedIds_, nullptr, &tags[2]);
    BySentIdConstruct(&bySentId_, nullptr, &tags[3], &tags[2]);
    trees::Objects::ConstructWith(&byObject_, nullptr, &tags[2], &tags[3]);
    VectorCtorIterator(priorities_, sizeof(EntryMap), 11, Callback(trees::Entries::Construct), Callback(trees::Entries::Destroy));
    HistoryListConstruct(&history_, nullptr, &tags[3]);
    HistoryListConstruct(&acknowledged_, nullptr, &tags[3]);
    // The trees' comparison byte is copied from the stack slot of the id argument.
    trees::Records::ConstructBase(&records_, nullptr, &tags[3]);
    records_.Count() = 0;
    records_.Compare() = static_cast<std::uint8_t>(id);
    records_.EmptyInitialize();
    trees::Attached::ConstructBase(&attached_, nullptr, &tags[3]);
    attached_.Compare() = static_cast<std::uint8_t>(id);
    attached_.Count() = 0;
    attached_.EmptyInitialize();
    HeaderListConstruct(&headers_, nullptr, &tags[3]);
    std::uint32_t zero = 0;
    IncomingProxyConstruct(&incoming_, nullptr, &tags[3], &zero);
    auto* sentinel = static_cast<HeaderNode*>(NodeAllocate(sizeof(HeaderNode)));
    incoming_ = sentinel;
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
    FreedBatch* out;
    FreedInsert(&freedIds_, nullptr, &out, freedIds_);
    FreedInsert(&freedIds_, nullptr, &out, freedIds_);
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
    ListDeleteNodes(&incoming_);
    HeaderSentinelFree(&incoming_, nullptr, incoming_, 1);
    ListDeleteNodes(&headers_);
    HeaderSentinelFree(&headers_, nullptr, headers_, 1);
    attached_.Clear<stlp::TreeElement<AttachedMap::Value>>();
    trees::Attached::FreeHeader(&attached_);
    trees::Records::Clear(&records_);
    trees::Records::FreeHeader(&records_);
    HistoryClear(&acknowledged_);
    HistorySentinelFree(acknowledged_, 1);
    HistoryClear(&history_);
    HistorySentinelFree(history_, 1);
    VectorDtorIterator(priorities_, sizeof(EntryMap), 11, Callback(trees::Entries::Destroy));
    trees::Objects::Clear(&byObject_);
    ByObjectHeaderFree(&byObject_, nullptr, byObject_.end(), 1);
    BySentIdClear(&bySentId_);
    BySentIdHeaderFree(&bySentId_, nullptr, bySentId_.end(), 1);
    FreedClear(&freedIds_);
    FreedSentinelFreeB(freedIds_, 1);
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

namespace {
// Copy constructors of the containers (library).
__declspec(naked) void* __fastcall ReceivedGetAllocator(std::int32_t* const* /*vector*/, void*, void* /*out*/) EI_ORIGINAL(0x00438CC0)
__declspec(naked) void __fastcall ReceivedReserveConstruct(std::int32_t** /*vector*/, void*, int /*n*/, const void* /*alloc*/) EI_ORIGINAL(0x00438D00)
__declspec(naked) std::int32_t* __fastcall CopyInts(const std::int32_t* /*first*/, const std::int32_t* /*last*/, std::int32_t* /*dest*/) EI_ORIGINAL(0x0043B360)
__declspec(naked) void __fastcall FreedListCopy(FreedBatch** /*to*/, void*, FreedBatch* const* /*from*/) EI_ORIGINAL(0x00437930)
__declspec(naked) void __fastcall BySentIdCopy(SentIdMap* /*to*/, void*, const SentIdMap* /*from*/) EI_ORIGINAL(0x00437AA0)
__declspec(naked) void __fastcall HistoryCopy(HistoryNode** /*to*/, void*, HistoryNode* const* /*from*/) EI_ORIGINAL(0x00437F60)
__declspec(naked) void __fastcall HeaderListCopy(HeaderNode** /*to*/, void*, HeaderNode* const* /*from*/) EI_ORIGINAL(0x00438690)
} // namespace

// 00433060 (slot 3).
Connection* Connection::Clone() {
    auto* copy = NewMessageMemory<Connection>();
    if (!copy) return nullptr;
    ei::SetVtable(copy, CRefObject::kVtableAddress);
    copy->sendSequence_ = sendSequence_;
    copy->lastReceived_ = lastReceived_;
    copy->sequenceHigh_ = sequenceHigh_;
    std::uint8_t tag = 0;
    void* alloc = ReceivedGetAllocator(&received_, nullptr, &tag);
    ReceivedReserveConstruct(&copy->received_, nullptr, static_cast<int>(receivedEnd_ - received_), alloc);
    copy->receivedEnd_ = CopyInts(received_, receivedEnd_, copy->received_);
    // id, round trips; timers, clock, budget: copied as dwords.
    std::memcpy(&copy->id_, &id_, offsetof(Connection, address_) - offsetof(Connection, id_));
    copy->address_ = address_;
    std::memcpy(&copy->sendInterval_, &sendInterval_, offsetof(Connection, idPools_) - offsetof(Connection, sendInterval_));
    for (int i = 0; i < kMessageTypes; ++i) copy->idPools_[i].CopyFrom(idPools_[i]);
    FreedListCopy(&copy->freedIds_, nullptr, &freedIds_);
    copy->freedTimer_ = freedTimer_;
    BySentIdCopy(&copy->bySentId_, nullptr, &bySentId_);
    trees::Objects::CopyConstruct(&copy->byObject_, nullptr, &byObject_);
    for (int i = 0; i < 11; ++i) trees::Entries::CopyConstruct(&copy->priorities_[i], nullptr, &priorities_[i]);
    HistoryCopy(&copy->history_, nullptr, &history_);
    HistoryCopy(&copy->acknowledged_, nullptr, &acknowledged_);
    trees::Records::CopyConstruct(&copy->records_, nullptr, &records_);
    trees::Attached::CopyConstruct(&copy->attached_, nullptr, &attached_);
    copy->headerSequence_ = headerSequence_;
    copy->incomingSequence_ = incomingSequence_;
    HeaderListCopy(&copy->headers_, nullptr, &headers_);
    HeaderListCopy(&copy->incoming_, nullptr, &incoming_);
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
    begin = nullptr;
    end = nullptr;
    eos = nullptr;
    std::size_t count = static_cast<std::size_t>(other.end - other.begin);
    std::uint16_t* block = count ? static_cast<std::uint16_t*>(NodeAllocate(count * 2)) : nullptr;
    begin = block;
    end = block;
    eos = block + count;
    std::size_t bytes = static_cast<std::size_t>(other.end - other.begin) * 2;
    std::memmove(block, other.begin, bytes);
    end = reinterpret_cast<std::uint16_t*>(reinterpret_cast<std::uint8_t*>(block) + bytes);
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
    self->begin = nullptr;
    self->end = nullptr;
    self->eos = nullptr;
    self->next = 0;
    return self;
}
EI_REPLACE(0x00433040, IdPoolConstruct);

} // namespace net
