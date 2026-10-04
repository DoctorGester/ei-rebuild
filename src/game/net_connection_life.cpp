// A connection's construction, reset, clone and destruction (00432DA0, 00433320, 00433060, 004334E0,
// 004334C0), the id pools' copy and the replica entries' slots.
// The containers are STLport instantiations (library), called through their original addresses.
// See net_connection.h and docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_connection.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstring>

#pragma float_control(precise, on)

namespace net {

namespace {

float Store(double v) {
    volatile float r = static_cast<float>(v);
    return r;
}

// CRT eh vector constructor / destructor iterators (stdcall).
__declspec(naked) void __stdcall VectorCtorIterator(void* /*ptr*/, unsigned /*size*/, int /*count*/, std::uintptr_t /*ctor*/,
                                                    std::uintptr_t /*dtor*/) EI_ORIGINAL(0x006E8693)
__declspec(naked) void __stdcall VectorDtorIterator(void* /*ptr*/, unsigned /*size*/, int /*count*/, std::uintptr_t /*dtor*/) EI_ORIGINAL(0x006E859F)
constexpr std::uintptr_t kIdPoolCtor = 0x00433040;   // IdPoolConstruct below, called through its address
constexpr std::uintptr_t kIdPoolDtor = 0x004336D0;
constexpr std::uintptr_t kPriorityCtor = 0x00437D70;
constexpr std::uintptr_t kPriorityDtor = 0x00433850;

// Container constructors (allocator / comparison arguments are empty objects passed by address).
__declspec(naked) void __fastcall ReceivedConstruct(void* /*vector*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00438CE0)
__declspec(naked) void __fastcall FreedListConstruct(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439120)
__declspec(naked) void __fastcall BySentIdConstruct(void* /*tree*/, void*, const void* /*a*/, const void* /*b*/) EI_ORIGINAL(0x004393C0)
__declspec(naked) void __fastcall ByObjectConstruct(void* /*tree*/, void*, const void* /*a*/, const void* /*b*/) EI_ORIGINAL(0x00439570)
__declspec(naked) void __fastcall HistoryListConstruct(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439960)
__declspec(naked) void __fastcall RecordsBaseConstruct(void* /*tree*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0043A590)
__declspec(naked) void __fastcall RecordsEmpty(void* /*tree*/) EI_ORIGINAL(0x0043A570)
__declspec(naked) void __fastcall AttachedBaseConstruct(void* /*tree*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x0043A780)
__declspec(naked) void __fastcall AttachedEmpty(void* /*tree*/) EI_ORIGINAL(0x0043A760)
__declspec(naked) void __fastcall HeaderListConstruct(void* /*list*/, void*, const void* /*alloc*/) EI_ORIGINAL(0x00439CE0)
__declspec(naked) void __fastcall IncomingProxyConstruct(void* /*list*/, void*, const void* /*alloc*/, const void* /*value*/) EI_ORIGINAL(0x0043A980)
__declspec(naked) void* __fastcall NodeAllocate(std::size_t /*bytes*/) EI_ORIGINAL(0x00405950)
__declspec(naked) void __fastcall GameBufferConstruct(GameBuffer* /*buffer*/) EI_ORIGINAL(0x004DA7F0)
__declspec(naked) void __fastcall GameBufferDestruct(GameBuffer* /*buffer*/) EI_ORIGINAL(0x004DA910)
__declspec(naked) void* __fastcall FreedPushDefault(void* /*list*/, void*, void** /*out*/, void* /*pos*/) EI_ORIGINAL(0x00439000)

// Reset's container teardown.
__declspec(naked) void __fastcall ListDeleteAll(void* /*list*/) EI_ORIGINAL(0x00439D40)
__declspec(naked) void __fastcall AttachedDestroy(void* /*tree*/) EI_ORIGINAL(0x004385A0)
__declspec(naked) void __fastcall RecordsDestroy(void* /*tree*/) EI_ORIGINAL(0x004383A0)
__declspec(naked) void __fastcall HistoryDestroy(void* /*list*/) EI_ORIGINAL(0x004399C0)
__declspec(naked) void __fastcall ByObjectClear(void* /*tree*/) EI_ORIGINAL(0x004396A0)
__declspec(naked) void __fastcall ByObjectFreeHeader(void* /*tree*/) EI_ORIGINAL(0x00439700)
__declspec(naked) void __fastcall BySentIdClear(void* /*tree*/) EI_ORIGINAL(0x004394A0)
__declspec(naked) void __fastcall BySentIdFreeHeader(void* /*tree*/) EI_ORIGINAL(0x00439500)
__declspec(naked) void __fastcall FreedClear(void* /*list*/) EI_ORIGINAL(0x0043BA20)
__declspec(naked) void __fastcall FreedFreeSentinelA(void* /*list*/, void*, void* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043A410)
__declspec(naked) void __fastcall ReceivedDeallocate(void* /*eos*/, void*, std::int32_t* /*start*/, int /*count*/) EI_ORIGINAL(0x0043A300)

// The destructor's container teardown.
__declspec(naked) void __fastcall ListDeleteNodes(void* /*list*/) EI_ORIGINAL(0x0043D420)
__declspec(naked) void __fastcall HeaderSentinelFree(void* /*list*/, void*, void* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x00439D80)
__declspec(naked) void __fastcall AttachedClear(void* /*tree*/) EI_ORIGINAL(0x00439BB0)
__declspec(naked) void __fastcall AttachedFreeHeader(void* /*tree*/) EI_ORIGINAL(0x00439C10)
__declspec(naked) void __fastcall RecordsClear(void* /*tree*/) EI_ORIGINAL(0x00439AE0)
__declspec(naked) void __fastcall RecordsFreeHeader(void* /*tree*/) EI_ORIGINAL(0x00439B40)
__declspec(naked) void __fastcall HistoryClear(void* /*list*/) EI_ORIGINAL(0x0043CA40)
__declspec(naked) void __fastcall HistorySentinelFree(void* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043AAF0)
__declspec(naked) void __fastcall ByObjectHeaderFree(void* /*tree*/, void*, void* /*header*/, int /*n*/) EI_ORIGINAL(0x0043A4E0)
__declspec(naked) void __fastcall BySentIdHeaderFree(void* /*tree*/, void*, void* /*header*/, int /*n*/) EI_ORIGINAL(0x0043A480)
__declspec(naked) void __fastcall FreedSentinelFreeB(void* /*sentinel*/, int /*n*/) EI_ORIGINAL(0x0043B180)

constexpr std::uintptr_t kConnectionVtable = 0x0073BF38;
constexpr std::uintptr_t kRefValueVtable = 0x0073BB58;
constexpr std::uintptr_t kRefObjectRoot = 0x0073BB68;

std::uint8_t* Bytes(Connection* c) { return reinterpret_cast<std::uint8_t*>(c); }
void SetVtable(Connection* c, std::uintptr_t v) { std::memcpy(c, &v, 4); }
void Put32(Connection* c, std::size_t offset, std::uint32_t v) { std::memcpy(Bytes(c) + offset, &v, 4); }

// The std::vector<int> of received sequences given back to the pool.
void FreeReceived(Connection* c) {
    ReceivedDeallocate(&c->receivedEos, nullptr, c->received, static_cast<int>(c->receivedEos - c->received));
}

} // namespace

// 00432DA0: the constructor (id). Builds every container, then: the send sequence starts at the
// tick count, the round trip at the session's +0x10 (or 0073BF34 without a session), its square
// average is the square of what the memory held, send interval 0.04, budget 220.
Connection* Connection::Construct(std::uint32_t id) {
    SetVtable(this, kRefValueVtable);
    std::uint8_t tags[4] = {};   // the empty allocator / comparison objects the constructors take
    ReceivedConstruct(&received, nullptr, &tags[2]);
    Put32(this, 0x2C, 0);
    Put32(this, 0x30, 0);
    Put32(this, 0x34, 0);
    Put32(this, 0x38, 0);
    VectorCtorIterator(idPools, sizeof(IdPool), 0x100, kIdPoolCtor, kIdPoolDtor);
    FreedListConstruct(&freedIds, nullptr, &tags[2]);
    BySentIdConstruct(bySentId, nullptr, &tags[3], &tags[2]);
    ByObjectConstruct(&byObject, nullptr, &tags[2], &tags[3]);
    VectorCtorIterator(priorities, sizeof(RbTree), 11, kPriorityCtor, kPriorityDtor);
    HistoryListConstruct(&history, nullptr, &tags[3]);
    HistoryListConstruct(&acknowledged, nullptr, &tags[3]);
    // The trees' comparison byte is copied from the stack slot of the id argument.
    RecordsBaseConstruct(&records, nullptr, &tags[3]);
    records.count = 0;
    records.compare = static_cast<std::uint8_t>(id);
    RecordsEmpty(&records);
    AttachedBaseConstruct(&attached, nullptr, &tags[3]);
    attached.compare = static_cast<std::uint8_t>(id);
    attached.count = 0;
    AttachedEmpty(&attached);
    HeaderListConstruct(&headers, nullptr, &tags[3]);
    std::uint32_t zero = 0;
    IncomingProxyConstruct(&list_1120, nullptr, &tags[3], &zero);
    void** sentinel = static_cast<void**>(NodeAllocate(0x10C));
    list_1120 = sentinel;
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    GameBufferConstruct(&reliableOut);
    GameBufferConstruct(&reliableIn);
    SetVtable(this, kConnectionVtable);
    std::uint32_t tick = gameapi::GetTickCount();
    connectionId = 0;
    SessionBase* session = CurrentSession();
    if (session) {
        std::int32_t longest;
        std::memcpy(&longest, reinterpret_cast<std::uint8_t*>(session) + 0x10, 4);
        roundTrip = Store(static_cast<double>(longest));
    } else {
        roundTrip = *reinterpret_cast<const float*>(0x0073BF34);
    }
    double old = roundTripSquare;   // never written before: whatever the allocation held
    sendSequence = static_cast<std::uint16_t>(tick);
    Put32(this, 0x3C, 0x3D23D70A);  // 0.04f
    Put32(this, 0x40, 0);
    Put32(this, 0x44, 0);
    updateBudget = 0xDC;
    roundTripSquare = Store(old * old);
    lastReceived = 0;
    unknown_0010 = 0x10000;
    void* out;
    FreedPushDefault(&freedIds, nullptr, &out, freedIds);
    FreedPushDefault(&freedIds, nullptr, &out, freedIds);
    connectionId = id;
    clock[0] = 0;
    freedTimer = 0.0f;
    headerSequence = 0;
    unknown_111A = 0;
    clock[1] = 0;
    unknown_1124 = 0;
    unknown_1128 = 0;
    return this;
}
EI_REPLACE_METHOD(0x00432DA0, Connection::Construct);

// 00433320 (slot 2): Reset. Tears every container down (in the order the destructor's unwinding
// would) and constructs the connection again with id 0.
void Connection::Reset() {
    SetVtable(this, kConnectionVtable);
    GameBufferDestruct(&reliableIn);
    GameBufferDestruct(&reliableOut);
    ListDeleteAll(&list_1120);
    ListDeleteAll(&headers);
    AttachedDestroy(&attached);
    RecordsDestroy(&records);
    HistoryDestroy(&acknowledged);
    HistoryDestroy(&history);
    VectorDtorIterator(priorities, sizeof(RbTree), 11, kPriorityDtor);
    ByObjectClear(&byObject);
    ByObjectFreeHeader(&byObject);
    BySentIdClear(bySentId);
    BySentIdFreeHeader(bySentId);
    FreedClear(&freedIds);
    FreedFreeSentinelA(&freedIds, nullptr, freedIds, 1);
    VectorDtorIterator(idPools, sizeof(IdPool), 0x100, kIdPoolDtor);
    FreeReceived(this);
    SetVtable(this, kRefObjectRoot);
    Construct(0);
}
namespace {
void __fastcall ResetHook(Connection* self) { self->Reset(); }
} // namespace
EI_REPLACE(0x00433320, ResetHook);

// 004334E0: the destructor body.
void Connection::Destruct() {
    SetVtable(this, kConnectionVtable);
    GameBufferDestruct(&reliableIn);
    GameBufferDestruct(&reliableOut);
    ListDeleteNodes(&list_1120);
    HeaderSentinelFree(&list_1120, nullptr, list_1120, 1);
    ListDeleteNodes(&headers);
    HeaderSentinelFree(&headers, nullptr, headers, 1);
    AttachedClear(&attached);
    AttachedFreeHeader(&attached);
    RecordsClear(&records);
    RecordsFreeHeader(&records);
    HistoryClear(&acknowledged);
    HistorySentinelFree(acknowledged, 1);
    HistoryClear(&history);
    HistorySentinelFree(history, 1);
    VectorDtorIterator(priorities, sizeof(RbTree), 11, kPriorityDtor);
    ByObjectClear(&byObject);
    ByObjectHeaderFree(&byObject, nullptr, byObject.header, 1);
    BySentIdClear(bySentId);
    BySentIdHeaderFree(bySentId, nullptr, *reinterpret_cast<void**>(bySentId), 1);
    FreedClear(&freedIds);
    FreedSentinelFreeB(freedIds, 1);
    VectorDtorIterator(idPools, sizeof(IdPool), 0x100, kIdPoolDtor);
    FreeReceived(this);
    SetVtable(this, kRefObjectRoot);
}
EI_REPLACE_METHOD(0x004334E0, Connection::Destruct);

// 004334C0 (slot 0): the deleting destructor.
Connection* __fastcall ConnectionDeletingDestructor(Connection* self, void*, unsigned flags) {
    self->Destruct();
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004334C0, ConnectionDeletingDestructor);

} // namespace net

namespace net {

namespace {
// Copy constructors of the containers (library) and of the id pools (00433A30, not rewritten yet).
__declspec(naked) void* __fastcall ReceivedGetAllocator(const void* /*vector*/, void*, void* /*out*/) EI_ORIGINAL(0x00438CC0)
__declspec(naked) void __fastcall ReceivedReserveConstruct(void* /*vector*/, void*, int /*n*/, const void* /*alloc*/) EI_ORIGINAL(0x00438D00)
__declspec(naked) std::int32_t* __fastcall CopyInts(const std::int32_t* /*first*/, const std::int32_t* /*last*/, std::int32_t* /*dest*/) EI_ORIGINAL(0x0043B360)
__declspec(naked) void __fastcall IdPoolCopy(IdPool* /*to*/, void*, const IdPool* /*from*/) EI_ORIGINAL(0x00433A30)
__declspec(naked) void __fastcall FreedListCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x00437930)
__declspec(naked) void __fastcall BySentIdCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x00437AA0)
__declspec(naked) void __fastcall ByObjectCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x00437C50)
__declspec(naked) void __fastcall PriorityCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x00437E00)
__declspec(naked) void __fastcall HistoryCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x00437F60)
__declspec(naked) void __fastcall RecordsCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x004381B0)
__declspec(naked) void __fastcall AttachedCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x00438440)
__declspec(naked) void __fastcall HeaderListCopy(void* /*to*/, void*, const void* /*from*/) EI_ORIGINAL(0x00438690)
__declspec(naked) void __fastcall GameBufferCopy(GameBuffer* /*to*/, void*, const GameBuffer* /*from*/) EI_ORIGINAL(0x004DA9E0)
} // namespace

Connection* Connection::Clone() const {
    auto* copy = static_cast<Connection*>(gamecrt::operator_new(sizeof(Connection)));
    Put32(copy, 4, 0);
    if (!copy) return nullptr;
    SetVtable(copy, kRefValueVtable);
    copy->sendSequence = sendSequence;
    copy->lastReceived = lastReceived;
    copy->unknown_0010 = unknown_0010;
    std::uint8_t tag = 0;
    void* alloc = ReceivedGetAllocator(&received, nullptr, &tag);
    ReceivedReserveConstruct(&copy->received, nullptr, static_cast<int>(receivedEnd - received), alloc);
    copy->receivedEnd = CopyInts(received, receivedEnd, copy->received);
    std::memcpy(Bytes(copy) + 0x20, reinterpret_cast<const std::uint8_t*>(this) + 0x20, 0x0C);   // id, round trips
    std::memcpy(copy->address, address, sizeof(address));
    std::memcpy(Bytes(copy) + 0x3C, reinterpret_cast<const std::uint8_t*>(this) + 0x3C, 0x18);   // timers, clock, budget
    for (int i = 0; i < 0x100; ++i) IdPoolCopy(&copy->idPools[i], nullptr, &idPools[i]);
    FreedListCopy(&copy->freedIds, nullptr, &freedIds);
    copy->freedTimer = freedTimer;
    BySentIdCopy(copy->bySentId, nullptr, bySentId);
    ByObjectCopy(&copy->byObject, nullptr, &byObject);
    for (int i = 0; i < 11; ++i) PriorityCopy(&copy->priorities[i], nullptr, &priorities[i]);
    HistoryCopy(&copy->history, nullptr, &history);
    HistoryCopy(&copy->acknowledged, nullptr, &acknowledged);
    RecordsCopy(&copy->records, nullptr, &records);
    AttachedCopy(&copy->attached, nullptr, &attached);
    copy->headerSequence = headerSequence;
    copy->unknown_111A = unknown_111A;
    HeaderListCopy(&copy->headers, nullptr, &headers);
    HeaderListCopy(&copy->list_1120, nullptr, &list_1120);
    copy->unknown_1124 = unknown_1124;
    copy->unknown_1128 = unknown_1128;
    GameBufferCopy(&copy->reliableOut, nullptr, &reliableOut);
    GameBufferCopy(&copy->reliableIn, nullptr, &reliableIn);
    SetVtable(copy, kConnectionVtable);
    return copy;
}
namespace {
Connection* __fastcall CloneHook(const Connection* self) { return self->Clone(); }
} // namespace
EI_REPLACE(0x00433060, CloneHook);

} // namespace net

// ---- Id pools and replica entries ---------------------------------------------------------------------

namespace net {

namespace {
constexpr std::uintptr_t kReplicaEntryVtable = 0x0073BF4C;
void SetVtableOf(void* object, std::uintptr_t v) { std::memcpy(object, &v, 4); }
// The entry's counted object pointer released as the original inlines it (16-bit count; slot 0
// deletes at zero).
void ReleaseEntryObject(ReplicaEntry* e) {
    CRefObject* object = e->object;
    if (object && --object->refs_ == 0) ei::VCall<void*>(object, 0, 1u);
}
} // namespace

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

// 004343C0 (entry slot 3): a copy of the entry sharing the object (one more reference).
ReplicaEntry* __fastcall ReplicaEntryClone(const ReplicaEntry* self) {
    auto* copy = static_cast<ReplicaEntry*>(gamecrt::operator_new(sizeof(ReplicaEntry)));
    copy->refHeader = 0;
    if (!copy) return nullptr;
    SetVtableOf(copy, kRefValueVtable);
    copy->version = self->version;
    copy->id = self->id;
    copy->type = self->type;
    copy->flags = self->flags;   // the original merges bit 0 and then the other bits: all of them
    copy->object = self->object;
    if (copy->object) ++copy->object->refs_;
    SetVtableOf(copy, kReplicaEntryVtable);
    return copy;
}
EI_REPLACE(0x004343C0, ReplicaEntryClone);

// 00434430 (entry slot 2): Reset. Releases the object, then constructs again in place (object,
// version and id cleared; type and flags kept).
void __fastcall ReplicaEntryReset(ReplicaEntry* self) {
    SetVtableOf(self, kReplicaEntryVtable);
    ReleaseEntryObject(self);
    SetVtableOf(self, kRefObjectRoot);
    self->object = nullptr;
    SetVtableOf(self, kReplicaEntryVtable);
    self->version = 0;
    self->id = 0;
}
EI_REPLACE(0x00434430, ReplicaEntryReset);

// 004344C0: the destructor body.
void __fastcall ReplicaEntryDestruct(ReplicaEntry* self) {
    SetVtableOf(self, kReplicaEntryVtable);
    ReleaseEntryObject(self);
    SetVtableOf(self, kRefObjectRoot);
}
EI_REPLACE(0x004344C0, ReplicaEntryDestruct);

// 004344A0 (entry slot 0): the deleting destructor.
ReplicaEntry* __fastcall ReplicaEntryDeletingDestructor(ReplicaEntry* self, void*, unsigned flags) {
    ReplicaEntryDestruct(self);
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
EI_REPLACE(0x004344A0, ReplicaEntryDeletingDestructor);


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
