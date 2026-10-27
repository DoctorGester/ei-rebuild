// The session's receive step, its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net_classes.h and docs/notes/network.md.
#include "library_net_lists.h"
#include "net_classes.h"
#include "game_strings.h"
#include "net_coder.h"
#include "net_connection.h"

#include "core_streams.h"
#include "ei/hook.h"
#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/crt_iostream.h"
#include "lib/stlp_list_refs.h"
#include "game_data.h"

#include <cstring>

namespace net {

// The network log's inserters (library), declared in net_classes.h.
LogStream* LogStream::Put(const char* text) { PutText(text); return this; }
LogStream* LogStream::PutChar(char c) { libcrt::Ostream::PutChar(c); return this; }
LogStream* LogStream::PutInt(int a0) { PutUnsigned(static_cast<unsigned>(a0)); return this; }
LogStream* LogStream::Apply(libcrt::Manipulator manipulator) { libcrt::Ostream::Apply(manipulator); return this; }

namespace {


// "<text><address>\n" and a flush to the network log.
void LogAddress(const char* text, const char* address) { g_netLog->Put(text)->Put(address)->PutChar('\n')->Flush(); }

} // namespace

// The buffer's highest written position follows the current one upwards.
void __fastcall BufferUpdateHigh(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
}
EI_REWRITTEN(0x00419CC0, BufferUpdateHigh);

stlp::string* NetAddress::ToString(stlp::string* out) const {
    Format(out, 0);
    return out;
}
EI_REWRITTEN(0x00435010, NetAddress::ToString);

// An empty datagram is only logged; the current session is this one meanwhile.
void SessionBase::Tick(double dt) {
    g_currentSession = this;
    time_ = dt + time_;
    NetAddress from{};
    GameBuffer* buffer = ReceiveBuffer();
    while (socket_.Receive(&from, buffer)) {
        BufferUpdateHigh(buffer);
        if (buffer->high - buffer->begin + buffer->base == 0) {
            if (g_netLog) {
                stlp::string address;
                const char* text = from.ToString(&address)->data();
                LogAddress(game::nettexts::ZeroLength(), text);
            }
            continue;
        }
        char kind;
        if (buffer->cur + 1 > buffer->high) buffer->ReadPastEnd(&kind, 1);
        else if (buffer->cur + 1 > buffer->end) buffer->ReadMore(&kind, 1u);
        else buffer->CopyOut(&kind, 1);
        BufferUpdateHigh(buffer);
        BufferCursor cursor{};
        cursor.p = buffer->begin + 1;
        cursor.bytePtr = nullptr;
        cursor.bits = 0;
        switch (kind) {
        case -1: OnMaster(&from, &cursor); break;
        case 0: OnUpdate(&from, &cursor); break;
        case 1: OnLeave(&from, &cursor); break;
        case 3: OnGetInfo(&from, &cursor); break;
        case 4: OnLogin(&from, &cursor); break;
        case 5: OnInfo(&from, &cursor); break;
        case 6: OnAccepted(&from, &cursor); break;
        case 7: OnRejected(&from, &cursor); break;
        default: break;   // 2 and anything else: ignored
        }
    }
    for (ClientNode* node = clients_->next; node != clients_;) {
        ClientNode* next = node->next;
        Connection* connection = node->connection;
        if (!connection->Tick(dt)) {
            if (g_netLog) {
                stlp::string address;
                const char* text = connection->address_.ToString(&address)->data();
                LogAddress(game::nettexts::Timeout(), text);
            }
            OnClientLost(connection);
            RemoveClient(node);
        }
        node = next;
    }
    ++frame_;
    g_currentSession = nullptr;
}
EI_REWRITTEN(0x00437370, SessionBase::Tick);

} // namespace net

// ---- Session vtable slots ---------------------------------------------------------------

namespace net {

namespace {


// "<text>\n" and a flush, when there is a network log.
void LogText(const char* text) {
    if (LogStream* log = g_netLog) log->Put(text)->PutChar('\n')->Flush();
}

// The address string of a connection, logged as "<before><address><after>\n", then freed.
void LogConnection(const char* before, Connection* connection, const char* after) {
    stlp::string peer;
    const char* text = connection->address_.ToString(&peer)->data();
    LogStream* log = g_netLog->Put(before)->Put(text);
    if (after) log = log->Put(after);
    log->PutChar('\n')->Flush();
}

} // namespace

ClientNode** SessionBase::FindClient(ClientNode** out, const NetAddress* address) {
    ClientNode* last = clients_;
    ClientNode* first = clients_->next;
    using netlists::ClientsNode;
    reinterpret_cast<netlists::ClientsIterator*>(out)->FindByAddress(reinterpret_cast<ClientsNode*>(first),
                                                                      reinterpret_cast<ClientsNode*>(last), address);
    return out;
}
EI_REWRITTEN(0x00436CC0, SessionBase::FindClient);

// Logs it, closes the connection and erases the node.
void SessionBase::RemoveClient(ClientNode* node) {
    if (node == clients_) return;
    if (g_netLog) LogConnection(game::nettexts::RemoveClient(), node->connection, nullptr);
    node->connection->Close();
    stlp::list<RefPtr>::Erase<stlp::SharedRef>(stlp::AsRefNode(node));
}

// Packet kind 0: an update from a client. Unknown senders are logged (returns
// false); a client whose update does not read is logged, reported and removed.
bool SessionBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    ClientNode* node = ClientAt(from);
    if (node == clients_) {
        LogText(game::nettexts::UnknownHost());
        return false;
    }
    if (!node->connection->ReadUpdate(cursor)) {
        if (g_netLog) LogConnection(game::nettexts::BadClient(), node->connection, game::nettexts::BadUpdate());
        OnClientLost(node->connection);
        RemoveClient(node);
    }
    return true;
}

// Packet kind 1: a client leaves: its connection id follows; when it matches,
// the client is reported and removed.
void SessionBase::OnLeave(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t id;
    std::memcpy(&id, cursor->p, 4);
    cursor->p += 4;
    ClientNode* node = ClientAt(from);
    if (node != clients_ && node->connection->id_ == id) {
        OnClientLeft(node->connection);
        RemoveClient(node);
    }
}

// The base session's handlers of the kinds it does not serve: each logs an error.
void SessionBase::OnGetInfo(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleGetInfo()); }
void SessionBase::OnLogin(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleLogin()); }
void SessionBase::OnInfo(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleInfo()); }
void SessionBase::OnAccepted(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleAccepted()); }
void SessionBase::OnRejected(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleRejected()); }
void SessionBase::OnMaster(const NetAddress*, BufferCursor*) { LogText(game::nettexts::CantHandleMaster()); }

EI_REWRITTEN(0x00436CF0, SessionBase::RemoveClient);
EI_REWRITTEN(0x00437080, SessionBase::OnUpdate);
EI_REWRITTEN(0x00437240, SessionBase::OnLeave);
EI_REWRITTEN(0x004371E0, SessionBase::OnGetInfo);
EI_REWRITTEN(0x00437210, SessionBase::OnLogin);
EI_REWRITTEN(0x004372B0, SessionBase::OnInfo);
EI_REWRITTEN(0x004372E0, SessionBase::OnAccepted);
EI_REWRITTEN(0x00437310, SessionBase::OnRejected);
EI_REWRITTEN(0x00437340, SessionBase::OnMaster);

} // namespace net

// ---- Byte buffer writes and small socket helpers ---------------------------------------------------

GameBuffer* GameBuffer::BufferWriteRaw(const void* from, unsigned size) {
    std::memcpy(cur, from, size);
    cur += size;
    return this;
}
EI_REWRITTEN(0x00436C30, GameBuffer::BufferWriteRaw);

unsigned GameBuffer::BufferWrite(const void* from, unsigned size) {
    if (cur + size > end) return static_cast<unsigned>(WriteMore(from, size));
    flags |= 1;
    std::memcpy(cur, from, size);
    cur += size;
    return size;
}
EI_REWRITTEN(0x00436C60, GameBuffer::BufferWrite);

namespace net {

int g_remoteRate = /*00783E34*/ 2500;
int g_localRate = /*00783E38*/ 12500;

bool SessionBase::Send(const NetAddress* address, GameBuffer* buffer) { return socket_.SendTo(address, buffer); }
EI_REWRITTEN(0x00436F60, SessionBase::Send);

std::uint32_t SessionBase::RateFor(const NetAddress* address) {
    bool local = socket_.IsLocal(address);
    return static_cast<std::uint32_t>(local ? g_localRate : g_remoteRate);
}
EI_REWRITTEN(0x00436F80, SessionBase::RateFor);

void SessionBase::SendLeave(const NetAddress* address, std::uint32_t id) {
    GrowingBuffer storage;
    auto* buffer = &storage;
    std::uint8_t kind = 1;
    if (buffer->cur + 1 <= buffer->end) {
        buffer->flags |= 1;
        *buffer->cur = kind;
        ++buffer->cur;
    } else {
        buffer->WriteMore(&kind, 1u);
    }
    if (buffer->cur + 4 <= buffer->end) {
        buffer->flags |= 1;
        std::memcpy(buffer->cur, &id, 4);
        buffer->cur += 4;
    } else {
        buffer->WriteMore(&id, 4u);
    }
    socket_.SendTo(address, buffer);
}
EI_REWRITTEN(0x00436FA0, SessionBase::SendLeave);

} // namespace net


namespace net {

namespace {
// The handler vectors' destructor (the vector base's: the storage freed), as the CRT's eh vector
// destructor iterator takes it.
constexpr auto HandlerVectorDestructor = libcrt::Element<stlp::VectorAbi<void*>::DestroyBase>;
// The clients list: its nodes and sentinel go back to the pool.
} // namespace

SessionBase::~SessionBase() {
    for (ClientNode* node = clients_->next; node != clients_; node = node->next) {
        Connection* connection = node->connection;
        std::uint32_t id = connection->id_;
        GrowingBuffer storage;
        auto* buffer = &storage;
        std::uint8_t kind = 1;
        buffer->BufferWrite(&kind, 1);
        if (buffer->cur + 4 <= buffer->end) {
            buffer->flags |= 1;
            buffer->BufferWriteRaw(&id, 4);
        } else {
            buffer->WriteMore(&id, 4u);
        }
        socket_.SendTo(&connection->address_, buffer);
    }
    libcrt::DestroyElements(handlers_[2], sizeof(HandlerVector), kMessageTypes, HandlerVectorDestructor);
    libcrt::DestroyElements(handlers_[1], sizeof(HandlerVector), kMessageTypes, HandlerVectorDestructor);
    libcrt::DestroyElements(handlers_[0], sizeof(HandlerVector), kMessageTypes, HandlerVectorDestructor);
    socket_.Destruct();
    types_.byType.Destroy();     // clear, the buckets
    types_.byVtable.Destroy();   // clear, the buckets
    ClientNode* sentinel = clients_;
    for (ClientNode* node = sentinel->next; node != clients_;) {
        ClientNode* next = node->next;
        reinterpret_cast<RefPtr*>(&node->connection)->Destroy();
        stlp::deallocate(node, sizeof(ClientNode));
        node = next;
        if (node == clients_) break;
    }
    clients_->next = clients_;
    clients_->prev = clients_;
    stlp::deallocate(clients_, sizeof(ClientNode));
}
EI_REWRITTEN_SPECIAL(0x00436A50, SessionBase::~SessionBase);
EI_REWRITTEN_SPECIAL(0x004368D0, SessionBase::~SessionBase);

} // namespace net

// ---- Adding a client ---------------------------------------------------------------------------------

namespace net {

namespace {
// The clients list's insert(pos, value) (list<RefPtr>'s with SharedRef): another STLport string destructor.
} // namespace

// The send interval is the constant over the rate clamped to [500, 50000].
void SessionBase::AddClient(const NetAddress* address, std::uint32_t id, int rate) {
    if (g_netLog) {
        stlp::string peer;
        const char* text = address->ToString(&peer)->data();
        LogAddress(game::nettexts::AddClient(), text);
    }
    RemoveClient(ClientAt(address));
    Connection* connection = NewCounted<Connection>(id);
    Connection* ref;
    netlists::InitClientRef(&ref, connection);
    stlp::list<RefPtr>::Insert<stlp::SharedRef>(stlp::AsRefNode(clients_), reinterpret_cast<const RefPtr&>(ref));
    Connection* added = clients_->prev->connection;
    added->address_ = *address;
    if (rate <= 500) rate = 500;
    else if (rate >= 50000) rate = 50000;
    added->sendInterval_ = static_cast<float>(static_cast<double>(250.0f /*0073BF48*/) / static_cast<double>(rate));
}
EI_REWRITTEN(0x00436DD0, SessionBase::AddClient);

} // namespace net
