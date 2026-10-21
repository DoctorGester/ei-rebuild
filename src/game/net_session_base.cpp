// The network session base class (vtable 0073BF5C): construction, the message type registry and
// the handler tables. See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/stlp.h"
#include "originals_library.h"
#include "record_vectors.h"

#include <cstring>

namespace net {

void PushReplicated(ReplicatedVector* v, void* object, std::uint8_t priority) {
    v->PushDefault<recordvec::ReplicatedElement>();
    Replicated* last = v->end() - 1;
    last->object = static_cast<Message*>(object);
    last->priority = priority;
}

void PushReplicatedInline(ReplicatedVector* v, void* object, std::uint8_t priority) {
    if (v->end() != v->EndOfStorage()) {
        ++v->Finish();   // the element's default constructor (00429030) does nothing
    } else {
        Replicated value{};   // the original passes an uninitialised local; it is overwritten below
        v->InsertOverflow<recordvec::ReplicatedElement>(v->end(), value, 1);
    }
    Replicated* last = v->end() - 1;
    last->object = static_cast<Message*>(object);
    last->priority = priority;
}

int TypeRegistry::TypeOf(const std::uint32_t* key) const {
    const ByVtable::Node* node = byVtable.Find(*key);
    return node ? static_cast<int>(node->value) : -1;
}

void TypeRegistry::Register(std::uint32_t type, MessageFactory factory) {
    Message* prototype = factory();
    if (prototype) ++prototype->refs_;
    std::uint32_t vtable;
    std::memcpy(&vtable, prototype, sizeof(vtable));   // the original reads it before any null check
    // operator[] as inlined: find_or_insert of {key, value()} (resize 0042B9C0 / 0042AA90, new
    // node 0042B8A0 / 0042AA00).
    byVtable.FindOrInsert({vtable, 0})->value = type;
    byType.FindOrInsert({type, nullptr})->value = factory;
    if (prototype && --prototype->refs_ == 0) delete prototype;
}
EI_REPLACE_METHOD(0x0042A890, TypeRegistry::Register);

Message* TypeRegistry::Create(std::uint32_t type) {
    MessageFactory factory = byType.FindOrInsert({type, nullptr})->value;
    return factory ? factory() : nullptr;
}
EI_REPLACE_METHOD(0x00438930, TypeRegistry::Create);

SessionBase* SessionBase::Construct() {
    ei::SetVtable(this, CRefCounted::kVtableAddress);
    // The connection list: an empty STLport list (a 12-byte sentinel linked to itself).
    auto* sentinel = static_cast<ClientNode*>(stlp::allocate(sizeof(ClientNode)));
    clients_ = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    // hashtable(100, hash, equal, allocator): 00438B70, 00439F20 (the functors empty, 0 here).
    types_.byVtable.Construct(100);
    types_.byType.Construct(100);
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

void SessionBase::AddHandler(int channel, std::uint32_t type, void* handler) {
    if (channel < 0 || channel >= kChannels) return;
    HandlerVector& v = handlers_[channel][type & 0xFF];
    if (v.end() == v.EndOfStorage()) {
        v.InsertOverflow<stlp::PlainElement<void*>>(v.end(), handler, 1);
        return;
    }
    if (v.end()) *v.Finish() = handler;
    ++v.Finish();
}
EI_REPLACE_METHOD(0x004377D0, SessionBase::AddHandler);

void SessionBase::AddHandlerForPrototype(Message* prototype, int channel, void* handler) {
    RefPtr ref;
    ref.ShareReturn(prototype);
    int type = types_.TypeOf(reinterpret_cast<const std::uint32_t*>(ref.ptr));
    AddHandler(channel, static_cast<std::uint32_t>(type), handler);
    ref.Destroy();
}

void SessionBase::RegisterMessageTypes() {
    // The factories of message types 1..17 (each returns a new message with no references).
    static constexpr MessageFactory kFactories[17] = {
        PlayerMessage::Create, LineMessage::Create, HostMessage::Create, BindingMessage::Create,
        LoginMessage::Create, SayMessage::Create, CommandMessage::Create, ZoneMessage::Create,
        MapMessage::Create, VariableMessage::Create, FileMessage::Create, FileChunkMessage::Create,
        FileRequestMessage::Create, AwardMessage::Create, PartyMessage::Create, PartyMemberMessage::Create,
        TradeMessage::Create};
    for (std::uint32_t type = 1; type <= 17; ++type) {
        reliable_[type] = 0;
        types_.Register(type, kFactories[type - 1]);
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
