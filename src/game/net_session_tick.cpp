// The session's receive step, its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net_classes.h and docs/notes/network.md.
#include "console.h"
#include "core_streams.h"
#include "game_data.h"
#include "game_strings.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_connection.h"

#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/static_object.h"

#include <cstring>

namespace net {

namespace {


// "<text><address>\n" and a flush to the network log.
void LogAddress(const char* text, const char* address) { console::EndLine(*g_netLog << text << address); }

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
    GameBuffer* buffer = &g_receiveBuffer;
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
    for (auto node = clients_.begin(); node != clients_.end();) {
        auto next = std::next(node);
        Connection* connection = node->Get();
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

// ---- Session virtuals ---------------------------------------------------------------

namespace net {

std::list<SharedRefTo<Connection>>::iterator SessionBase::FindClient(const NetAddress* address) {
    auto node = clients_.begin();
    while (node != clients_.end() && std::memcmp(&node->Get()->address_, address, sizeof(NetAddress)) != 0) ++node;
    return node;
}

// Logs it, closes the connection and erases the node.
void SessionBase::RemoveClient(std::list<SharedRefTo<Connection>>::iterator node) {
    if (node == clients_.end()) return;
    if (g_netLog) LogConnection(game::nettexts::RemoveClient(), node->Get(), nullptr);
    node->Get()->Close();
    clients_.erase(node);
}

// Packet kind 0: an update from a client. Unknown senders are logged (returns
// false); a client whose update does not read is logged, reported and removed.
bool SessionBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    auto node = FindClient(from);
    if (node == clients_.end()) {
        LogText(game::nettexts::UnknownHost());
        return false;
    }
    if (!node->Get()->ReadUpdate(cursor)) {
        if (g_netLog) LogConnection(game::nettexts::BadClient(), node->Get(), game::nettexts::BadUpdate());
        OnClientLost(node->Get());
        RemoveClient(node);
    }
    return true;
}

// Packet kind 1: a client leaves: its connection id follows; when it matches,
// the client is reported and removed.
void SessionBase::OnLeave(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t id;
    lib::LoadBytes(id, cursor->p);
    cursor->p += 4;
    auto node = FindClient(from);
    if (node != clients_.end() && node->Get()->id_ == id) {
        OnClientLeft(node->Get());
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
        lib::StoreBytes(buffer->cur, id);
        buffer->cur += 4;
    } else {
        buffer->WriteMore(&id, 4u);
    }
    socket_.SendTo(address, buffer);
}

} // namespace net


namespace net {

// Every client is sent a leave packet; the members then close the socket, free the type registry and release the
// connections.
SessionBase::~SessionBase() {
    for (auto& node : clients_) {
        Connection* connection = node.Get();
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
}

} // namespace net

// ---- Adding a client ---------------------------------------------------------------------------------

namespace net {

// The send interval is the constant over the rate clamped to [500, 50000].
void SessionBase::AddClient(const NetAddress* address, std::uint32_t id, int rate) {
    if (g_netLog) {
        std::string peer = address->ToString();
        const char* text = peer.data();
        LogAddress(game::nettexts::AddClient(), text);
    }
    RemoveClient(FindClient(address));
    clients_.emplace_back(NewCounted<Connection>(id));
    Connection* added = clients_.back().Get();
    added->address_ = *address;
    if (rate <= 500) rate = 500;
    else if (rate >= 50000) rate = 50000;
    added->sendInterval_ = static_cast<float>(static_cast<double>(250.0f) / static_cast<double>(rate));
}

} // namespace net
