// The network session base class (vtable 0073BF5C): construction, the message type registry and
// the handler tables. See net_classes.h and docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/stlp.h"

#include <cstring>

RefPtr* __fastcall RefPtrShareReturn(RefPtr* self, void*, CRefObject* object);   // refptr_copies.cpp

namespace net {

namespace {

// STLport hashtable members of the two registry maps (library instantiations).
__declspec(naked) TypeRegistry::ByVtable* __fastcall ByVtableConstruct(TypeRegistry::ByVtable* /*table*/, void*, std::size_t /*n*/,
                                                                      const void*, const void*, const void*) EI_ORIGINAL(0x00438B70)
__declspec(naked) TypeRegistry::ByType* __fastcall ByTypeConstruct(TypeRegistry::ByType* /*table*/, void*, std::size_t /*n*/,
                                                                  const void*, const void*, const void*) EI_ORIGINAL(0x00439F20)
__declspec(naked) void __fastcall ByVtableResize(TypeRegistry::ByVtable* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x0042B9C0)
__declspec(naked) void __fastcall ByTypeResize(TypeRegistry::ByType* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x0042AA90)
// _M_new_node(const pair&): a pool node {0, key, value}.
__declspec(naked) TypeRegistry::ByVtable::Node* __fastcall ByVtableNewNode(TypeRegistry::ByVtable* /*table*/, void*,
                                                                          const TypeRegistry::ByVtable::Pair* /*pair*/) EI_ORIGINAL(0x0042B8A0)
__declspec(naked) TypeRegistry::ByType::Node* __fastcall ByTypeNewNode(TypeRegistry::ByType* /*table*/, void*,
                                                                      const TypeRegistry::ByType::Pair* /*pair*/) EI_ORIGINAL(0x0042AA00)
// vector<handler>::_M_insert_overflow(pos, const value&, n) (library).
__declspec(naked) void __fastcall HandlersInsertOverflow(HandlerVector* /*v*/, void*, void** /*pos*/,
                                                         void* const* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x0043D5F0)

// vector<Replicated>: push_back of a default element (0041F790), _M_insert_overflow (00426B10).
__declspec(naked) void __fastcall ReplicatedPushDefault(ReplicatedVector* /*v*/) EI_ORIGINAL(0x0041F790)
__declspec(naked) void __fastcall ReplicatedInsertOverflow(ReplicatedVector* /*v*/, void*, Replicated* /*pos*/,
                                                           const Replicated* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x00426B10)

// The insert half of hash_map::operator[] as 0042A890 inlines it, through the game's own resize
// and node allocation: the node of `key` (a new node {key, value()} at the head of its bucket when
// missing).
template <typename Map, typename Resize, typename NewNode>
typename Map::Node* FindOrInsert(Map& table, std::uint32_t key, Resize resize, NewNode newNode) {
    typename Map::Pair pair{key, {}};
    resize(&table, nullptr, table.Size() + 1);
    std::size_t bucket = key % table.BucketCount();
    for (typename Map::Node* node = table.Bucket(bucket); node; node = node->next)
        if (node->key == key) return node;
    typename Map::Node* node = newNode(&table, nullptr, &pair);
    table.LinkAtHead(bucket, node);
    return node;
}

} // namespace

void PushReplicated(ReplicatedVector* v, void* object, std::uint8_t priority) {
    ReplicatedPushDefault(v);
    Replicated* last = v->end - 1;
    last->object = static_cast<Message*>(object);
    last->priority = priority;
}

void PushReplicatedInline(ReplicatedVector* v, void* object, std::uint8_t priority) {
    if (v->end != v->eos) {
        ++v->end;   // the element's default constructor (00429030) does nothing
    } else {
        Replicated value{};   // the original passes an uninitialised local; it is overwritten below
        ReplicatedInsertOverflow(v, nullptr, v->end, &value, 1);
    }
    Replicated* last = v->end - 1;
    last->object = static_cast<Message*>(object);
    last->priority = priority;
}

int TypeRegistry::TypeOf(const std::uint32_t* key) const {
    const ByVtable::Node* node = byVtable.Find(*key);
    return node ? static_cast<int>(node->value) : -1;
}
EI_REPLACE_METHOD(0x00429F40, TypeRegistry::TypeOf);

void TypeRegistry::Register(std::uint32_t type, MessageFactory factory) {
    Message* prototype = factory();
    if (prototype) ++prototype->refs_;
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype, sizeof(vtable));   // the original reads it before any null check
    FindOrInsert(byVtable, vtable, ByVtableResize, ByVtableNewNode)->value = type;
    FindOrInsert(byType, type, ByTypeResize, ByTypeNewNode)->value = factory;
    if (prototype && --prototype->refs_ == 0) delete prototype;
}
EI_REPLACE_METHOD(0x0042A890, TypeRegistry::Register);

Message* TypeRegistry::Create(std::uint32_t type) {
    MessageFactory factory = FindOrInsert(byType, type, ByTypeResize, ByTypeNewNode)->value;
    return factory ? factory() : nullptr;
}
EI_REPLACE_METHOD(0x00438930, TypeRegistry::Create);

SessionBase* SessionBase::Construct() {
    ei::SetVtable(this, kRefObjectRootVtable);
    // The connection list: an empty STLport list (a 12-byte sentinel linked to itself).
    auto* sentinel = static_cast<ClientNode*>(stlp::allocate(sizeof(ClientNode)));
    clients_ = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    char hash = 0, equal = 0, allocator = 0;   // the empty functors (passed by reference)
    ByVtableConstruct(&types_.byVtable, nullptr, 100, &hash, &equal, &allocator);
    ByTypeConstruct(&types_.byType, nullptr, 100, &hash, &equal, &allocator);
    socket_.Construct();
    // 3 x 256 empty handler vectors (the element constructor 00436A30 only zeroes them).
    for (auto& channel : handlers_)
        for (HandlerVector& v : channel) v = HandlerVector{};
    reinterpret_cast<std::uint32_t*>(&time_)[0] = 0;
    ei::SetVtable(this, kVtableAddress);
    reinterpret_cast<std::uint32_t*>(&time_)[1] = 0;
    frame_ = 1;
    magic_ = 0;
    timeout_ = 0x3C;
    maxRoundTrip_ = 5;
    idReuseTime_ = 0x4B;
    return this;
}
EI_REPLACE_METHOD(0x00436770, SessionBase::Construct);

// 00436A30 (no Ghidra function): the handler vector's element constructor (an empty vector; the
// session constructor inlines it). Leaves 0 in EAX, as the original.
void* __fastcall HandlerVectorConstruct(HandlerVector* self) {
    self->begin = nullptr;
    self->end = nullptr;
    self->eos = nullptr;
    return nullptr;
}
EI_REPLACE(0x00436A30, HandlerVectorConstruct);

void SessionBase::AddHandler(int channel, std::uint32_t type, void* handler) {
    if (channel < 0 || channel >= kChannels) return;
    HandlerVector& v = handlers_[channel][type & 0xFF];
    if (v.end == v.eos) {
        HandlersInsertOverflow(&v, nullptr, v.end, &handler, 1);
        return;
    }
    if (v.end) *v.end = handler;
    ++v.end;
}
EI_REPLACE_METHOD(0x004377D0, SessionBase::AddHandler);

void SessionBase::AddHandlerForPrototype(Message* prototype, int channel, void* handler) {
    RefPtr ref;
    RefPtrShareReturn(&ref, nullptr, prototype);
    int type = types_.TypeOf(reinterpret_cast<const std::uint32_t*>(ref.ptr));
    AddHandler(channel, static_cast<std::uint32_t>(type), handler);
    ref.Destroy();
}

void SessionBase::RegisterMessageTypes() {
    // The factories of message types 1..17 (each returns a new message with no references).
    static constexpr std::uintptr_t kFactories[17] = {
        0x00414420, 0x00414960, 0x00414DB0, 0x00415320, 0x004170A0, 0x004171A0, 0x00417250, 0x00415570,
        0x00415D50, 0x00415980, 0x00416160, 0x004166F0, 0x004179E0, 0x00416940, 0x00417650, 0x00417300,
        0x00416DF0};
    for (std::uint32_t type = 1; type <= 17; ++type) {
        reliable_[type] = 0;
        types_.Register(type, reinterpret_cast<MessageFactory>(kFactories[type - 1]));
    }
}
EI_REPLACE_METHOD(0x004142F0, SessionBase::RegisterMessageTypes);

ClientNode* SessionBase::ClientAt(const NetAddress* address) {
    ClientNode* found;
    FindClient(&found, address);
    return found;
}

std::uint32_t SessionBase::ClientCount() const {
    std::uint32_t count = 0;
    for (ClientNode* node = clients_->next; node != clients_; node = node->next) ++count;
    return count;
}

} // namespace net
