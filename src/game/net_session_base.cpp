// The network session base class: construction, the message type registry and
// the handler tables. See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "lib/stlp.h"
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
        ++v->Finish();   // the element's default constructor does nothing
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
    // operator[] as inlined: find_or_insert of {key, value()} (resize, new node).
    byVtable.FindOrInsert({KeyOf(prototype), 0})->value = type;
    byType.FindOrInsert({type, nullptr})->value = factory;
    if (prototype && --prototype->refs_ == 0) delete prototype;
}

Message* TypeRegistry::Create(std::uint32_t type) {
    MessageFactory factory = byType.FindOrInsert({type, nullptr})->value;
    return factory ? factory() : nullptr;
}

SessionBase::SessionBase() {
    // The connection list: an empty STLport list (a 12-byte sentinel linked to itself).
    auto* sentinel = stlp::allocate_as<ClientNode>();
    clients_ = sentinel;
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
    // hashtable(100, hash, equal, allocator) (the functors empty, 0 here).
    types_.byVtable.Construct(100);
    types_.byType.Construct(100);
    socket_.Construct();
    time_ = 0.0;
    frame_ = 1;
    magic_ = 0;
    timeout_ = 0x3C;
    maxRoundTrip_ = 5;
    idReuseTime_ = 0x4B;
}

void SessionBase::AddHandler(int channel, std::uint32_t type, MessageHandler handler) {
    if (channel < 0 || channel >= kChannels) return;
    handlers_[channel][type & 0xFF].push_back(handler);
}

void SessionBase::AddHandlerForPrototype(Message* prototype, int channel, MessageHandler handler) {
    RefPtr ref;
    ref.ShareReturn(prototype);
    const std::uint32_t key = TypeRegistry::KeyOf(ref.ptr);
    int type = types_.TypeOf(&key);
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
