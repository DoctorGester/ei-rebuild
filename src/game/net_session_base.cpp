// The network session base class (vtable 0073BF5C): construction, the message type registry and
// the handler tables. See net_classes.h and docs/notes/unmapped_410_470.md ("Third round").
#include "net_classes.h"

#include "core_refcount.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/stlp.h"

#include <cstring>

RefPtr* __fastcall RefPtrShareReturn(RefPtr* self, void*, CRefObject* object);   // refptr_copies.cpp

namespace net {

namespace {

// STLport hashtable members of the two registry maps (library instantiations).
__declspec(naked) HashTable* __fastcall ByVtableConstruct(HashTable* /*table*/, void*, std::size_t /*n*/,
                                                         const void*, const void*, const void*) EI_ORIGINAL(0x00438B70)
__declspec(naked) HashTable* __fastcall ByTypeConstruct(HashTable* /*table*/, void*, std::size_t /*n*/,
                                                       const void*, const void*, const void*) EI_ORIGINAL(0x00439F20)
__declspec(naked) void __fastcall ByVtableResize(HashTable* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x0042B9C0)
__declspec(naked) void __fastcall ByTypeResize(HashTable* /*table*/, void*, std::size_t /*hint*/) EI_ORIGINAL(0x0042AA90)
// _M_new_node(const pair&): a 16-byte pool node {0, key, value}.
__declspec(naked) TypeNode* __fastcall ByVtableNewNode(HashTable* /*table*/, void*, const std::uint32_t* /*pair*/) EI_ORIGINAL(0x0042B8A0)
__declspec(naked) TypeNode* __fastcall ByTypeNewNode(HashTable* /*table*/, void*, const std::uint32_t* /*pair*/) EI_ORIGINAL(0x0042AA00)
// vector<handler>::_M_insert_overflow(pos, const value&, n) (library).
__declspec(naked) void __fastcall HandlersInsertOverflow(HandlerVector* /*v*/, void*, void** /*pos*/,
                                                         void* const* /*value*/, std::size_t /*n*/) EI_ORIGINAL(0x0043D5F0)
// 004407F0: the socket driver's constructor (WSAStartup, the host's broadcast addresses).
__declspec(naked) SocketDriver* __fastcall SocketDriverConstruct(SocketDriver* /*driver*/) EI_ORIGINAL(0x004407F0)

std::size_t BucketCount(const HashTable& table) {
    return static_cast<std::size_t>(table.bucketsEnd - table.buckets);
}

// The insert half of hash_map::operator[] as 0042A890 inlines it: the node of `key` (a new node
// {key, 0} at the head of its bucket when missing).
template <typename Resize, typename NewNode>
TypeNode* FindOrInsert(HashTable& table, std::uint32_t key, Resize resize, NewNode newNode) {
    std::uint32_t pair[2] = {key, 0};
    resize(&table, nullptr, table.count + 1);
    std::uint32_t bucket = key % BucketCount(table);
    auto* head = static_cast<TypeNode*>(table.buckets[bucket]);
    for (TypeNode* node = head; node; node = node->next)
        if (node->key == key) return node;
    TypeNode* node = newNode(&table, nullptr, pair);
    node->next = head;
    table.buckets[bucket] = node;
    ++table.count;
    return node;
}

void SetVtable(void* object, std::uintptr_t vtable) { std::memcpy(object, &vtable, sizeof(vtable)); }

} // namespace

int TypeRegistry::TypeOf(const std::uint32_t* key) const {
    std::uint32_t k = *key;
    auto* node = static_cast<TypeNode*>(byVtable.buckets[k % BucketCount(byVtable)]);
    while (node && node->key != k) node = node->next;
    return node ? static_cast<int>(node->value) : -1;
}
EI_REPLACE_METHOD(0x00429F40, TypeRegistry::TypeOf);

void TypeRegistry::Register(std::uint32_t type, MessageFactory factory) {
    auto* prototype = reinterpret_cast<CRefObject*>(factory());
    if (prototype) ++prototype->refs_;
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype, sizeof(vtable));   // the original reads it before any null check
    FindOrInsert(byVtable, vtable, ByVtableResize, ByVtableNewNode)->value = type;
    FindOrInsert(byType, type, ByTypeResize, ByTypeNewNode)->value = reinterpret_cast<std::uint32_t>(factory);
    if (prototype && --prototype->refs_ == 0) ei::VCall<void*>(prototype, 0, 1);
}
EI_REPLACE_METHOD(0x0042A890, TypeRegistry::Register);

SessionBase* SessionBase::Construct() {
    SetVtable(this, 0x0073BB68);   // CRefObject root
    // The connection list: an empty STLport list (a 12-byte sentinel linked to itself).
    auto** sentinel = static_cast<void**>(stlp::allocate(0xC));
    clients = sentinel;
    sentinel[0] = sentinel;
    sentinel[1] = sentinel;
    char hash = 0, equal = 0, allocator = 0;   // the empty functors (passed by reference)
    ByVtableConstruct(&types.byVtable, nullptr, 100, &hash, &equal, &allocator);
    ByTypeConstruct(&types.byType, nullptr, 100, &hash, &equal, &allocator);
    SocketDriverConstruct(&socket);
    // 3 x 256 empty handler vectors (the element constructor 00436A30 only zeroes them).
    for (auto& channel : handlers)
        for (HandlerVector& v : channel) v = HandlerVector{};
    reinterpret_cast<std::uint32_t*>(&time)[0] = 0;
    SetVtable(this, kSessionBaseVtable);
    reinterpret_cast<std::uint32_t*>(&time)[1] = 0;
    frame = 1;
    unknown_08 = 0;
    unknown_0C = 0x3C;
    unknown_10 = 5;
    unknown_14 = 0x4B;
    return this;
}
EI_REPLACE_METHOD(0x00436770, SessionBase::Construct);

void SessionBase::AddHandler(int channel, std::uint32_t type, void* handler) {
    if (channel < 0 || channel >= kChannels) return;
    HandlerVector& v = handlers[channel][type & 0xFF];
    if (v.end == v.eos) {
        HandlersInsertOverflow(&v, nullptr, v.end, &handler, 1);
        return;
    }
    if (v.end) *v.end = handler;
    ++v.end;
}
EI_REPLACE_METHOD(0x004377D0, SessionBase::AddHandler);

void SessionBase::AddHandlerFor(std::size_t size, Message* (__fastcall* construct)(Message*), int channel,
                                void* handler) {
    auto* memory = static_cast<Message*>(NewRefObject(size));
    Message* prototype = memory ? construct(memory) : nullptr;
    RefPtr ref;
    RefPtrShareReturn(&ref, nullptr, reinterpret_cast<CRefObject*>(prototype));
    int type = types.TypeOf(reinterpret_cast<const std::uint32_t*>(ref.ptr));
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
        typeFlags[type] = 0;
        types.Register(type, reinterpret_cast<MessageFactory>(kFactories[type - 1]));
    }
}
EI_REPLACE_METHOD(0x004142F0, SessionBase::RegisterMessageTypes);

} // namespace net
