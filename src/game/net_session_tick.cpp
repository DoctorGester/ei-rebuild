// The session's receive step, its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net_classes.h and docs/notes/network.md.
#include "console.h"
#include "library_net_lists.h"
#include "net_classes.h"
#include "game_strings.h"
#include "net_coder.h"
#include "net_connection.h"

#include "core_streams.h"
#include "lib/static_object.h"
#include "lib/crt.h"
#include "lib/stlp_list_refs.h"
#include "game_data.h"

#include <cstring>

namespace net {

namespace {


// "<text><address>\n" and a flush to the network log.
void LogAddress(const char* text, const char* address) { console::EndLine(*g_netLog << text << address); }

} // namespace

// The buffer's highest written position follows the current one upwards.
void BufferUpdateHigh(GameBuffer* buffer) {
    if (buffer->cur > buffer->high) buffer->high = buffer->cur;
}

std::string NetAddress::ToString() const { return Format(0); }

// An empty datagram is only logged; the current session is this one meanwhile.
void SessionBase::Tick(double dt) {
    g_currentSession = this;
    time_ = dt + time_;
    NetAddress from{};
    GameBuffer* buffer = ReceiveBuffer();
    while (socket_.Receive(&from, buffer)) {
        BufferUpdateHigh(buffer);
        if (buffer->PositionOf(buffer->high) == 0) {
            if (g_netLog) {
                std::string address = from.ToString();
                const char* text = address.data();
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
                std::string address = connection->address_.ToString();
                const char* text = address.data();
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

} // namespace net

// ---- Session vtable slots ---------------------------------------------------------------

namespace net {

namespace {


// "<text>\n" and a flush, when there is a network log.
void LogText(const char* text) {
    if (console::Stream* log = g_netLog) console::EndLine(*log << text);
}

// The address string of a connection, logged as "<before><address><after>\n", then freed.
void LogConnection(const char* before, Connection* connection, const char* after) {
    std::string peer = connection->address_.ToString();
    const char* text = peer.data();
    console::Stream& log = *g_netLog << before << text;
    if (after) log << after;
    console::EndLine(log);
}

} // namespace

ClientNode** SessionBase::FindClient(ClientNode** out, const NetAddress* address) {
    ClientNode* last = clients_;
    ClientNode* first = clients_->next;
    netlists::ClientsIterator found;
    found.FindByAddress(first, last, address);
    *out = found.node;
    return out;
}

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

} // namespace net

// ---- Byte buffer writes and small socket helpers ---------------------------------------------------

GameBuffer* GameBuffer::BufferWriteRaw(const void* from, unsigned size) {
    std::memcpy(cur, from, size);
    cur += size;
    return this;
}

unsigned GameBuffer::BufferWrite(const void* from, unsigned size) {
    if (cur + size > end) return static_cast<unsigned>(WriteMore(from, size));
    flags |= 1;
    std::memcpy(cur, from, size);
    cur += size;
    return size;
}

namespace net {

int g_remoteRate = /*00783E34*/ 2500;
int g_localRate = /*00783E38*/ 12500;

bool SessionBase::Send(const NetAddress* address, GameBuffer* buffer) { return socket_.SendTo(address, buffer); }

std::uint32_t SessionBase::RateFor(const NetAddress* address) {
    bool local = socket_.IsLocal(address);
    return static_cast<std::uint32_t>(local ? g_localRate : g_remoteRate);
}

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

} // namespace net


namespace net {

namespace {
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
    socket_.Destruct();
    types_.byType.Destroy();     // clear, the buckets
    types_.byVtable.Destroy();   // clear, the buckets
    ClientNode* sentinel = clients_;
    for (ClientNode* node = sentinel->next; node != clients_;) {
        ClientNode* next = node->next;
        DestroyOwned(node->connection);
        stlp::deallocate(node, sizeof(ClientNode));
        node = next;
        if (node == clients_) break;
    }
    clients_->next = clients_;
    clients_->prev = clients_;
    stlp::deallocate(clients_, sizeof(ClientNode));
}

} // namespace net

// ---- Adding a client ---------------------------------------------------------------------------------

namespace net {

namespace {
// The clients list's insert(pos, value) (list<RefPtr>'s with SharedRef): another STLport string destructor.
} // namespace

// The send interval is the constant over the rate clamped to [500, 50000].
void SessionBase::AddClient(const NetAddress* address, std::uint32_t id, int rate) {
    if (g_netLog) {
        std::string peer = address->ToString();
        const char* text = peer.data();
        LogAddress(game::nettexts::AddClient(), text);
    }
    RemoveClient(ClientAt(address));
    Connection* connection = NewCounted<Connection>(id);
    const RefPtr ref{connection};   // stored without a reference
    stlp::list<RefPtr>::Insert<stlp::SharedRef>(stlp::AsRefNode(clients_), ref);
    Connection* added = clients_->prev->connection;
    added->address_ = *address;
    if (rate <= 500) rate = 500;
    else if (rate >= 50000) rate = 50000;
    added->sendInterval_ = static_cast<float>(static_cast<double>(250.0f /*0073BF48*/) / static_cast<double>(rate));
}

} // namespace net
