// The network pump. See net_session.h.
#include "game/net/net_session.h"

#include "game/console/console.h"
#include "engine/core/core_refcount.h"
#include "engine/core/core_streams.h"
#include "game/app/game_flow.h"
#include "game/app/singletons.h"
#include "engine/core/coder.h"
#include "game/net/net_connection.h"
#include "game/net/net_link.h"
#include "game/net/net_messages.h"
#include "lib/crt.h"
#include "platform/platform.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace {

// timeGetTime() at the previous pump.
std::uint32_t g_lastPumpTime = /*0079B604*/ {};

// The session pointers' exit functions: the last reference deletes the session, which sends its leave packets.
void ReleaseGameServer() { net::g_gameServer.Reset(); }
void ReleaseGameClient() { net::g_gameClient.Reset(); }

// Milliseconds to the sessions' time unit (1/1024).
constexpr float kMillisecondsToSeconds = 0.0009765625f;

// A session that is there and not shutting down is ticked with `elapsed` milliseconds.
void TickSession(net::SessionBase* session, std::uint32_t elapsed) {
    if (!IsLive(session)) return;
    // At most 5000 milliseconds (Pump): exact in a float, as is the product.
    const float seconds = static_cast<float>(elapsed) * kMillisecondsToSeconds;
    session->Tick(seconds);
}

// A new driver for `player`, put at the end of the owning list (shared there).
netlink::DriverLink* NewDriver(netlink::DriverSession* self, net::Connection* player) {
    auto* driver = new netlink::DriverLink();
    driver->player.Assign(player);
    self->drivers.emplace_back(driver);
    return driver;
}

// "<text><address>\n" and a flush to the network log.
void LogAddress(const char* text, const char* address) { console::EndLine(*net::g_netLog << text << address); }

// "<text>\n" and a flush, when there is a network log.
void LogText(const char* text) {
    if (console::Stream* log = net::g_netLog) console::EndLine(*log << text);
}

// The address string of a connection, logged as "<before><address><after>\n", then freed.
void LogConnection(const char* before, net::Connection* connection, const char* after) {
    std::string peer = connection->address.ToString();
    const char* text = peer.data();
    console::Stream& log = *net::g_netLog << before << text;
    if (after) log << after;
    console::EndLine(log);
}

} // namespace

// Their static initialisers (in the list's order): the counted pointer's constructor clears it, then the exit
// function is registered, so the sessions go in the game's exit order, not at our CRT's (the harness's network pair
// has ended by then: a zone pair's leave packets went to the other game's ports).
void InitGameServerPointer() {
    net::g_gameServer.Reset();
    std::atexit(ReleaseGameServer);
}
void InitGameClientPointer() {
    net::g_gameClient.Reset();
    std::atexit(ReleaseGameClient);
}

namespace net {

GrowingBuffer g_receiveBuffer;
GrowingBuffer g_sendBuffer;

void Pump() {
    std::uint32_t now = platform::FrameClock();
    std::uint32_t elapsed = now - g_lastPumpTime;
    if (elapsed > 5000) elapsed = 1000;   // after a long stall, pretend one second passed
    g_lastPumpTime = now;
    TickSession(g_gameServer, elapsed);
    TickSession(g_gameClient, elapsed);
    g_gameFlow.UpdateMultiplayerVariable();   // With the game flow: the per-frame network step
}

// --- Base --------------------------------------------------------------------------------------------

// The network session base class.

SessionBase::SessionBase() = default;

std::uint32_t SessionBase::ClientCount() const { return static_cast<std::uint32_t>(clients.size()); }

// --- Tick --------------------------------------------------------------------------------------------

// The session's receive step, its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net/net_session.h and docs/notes/network.md.

// An empty datagram is only logged.
void SessionBase::Tick(double dt) {
    time = static_cast<float>(time) + static_cast<float>(dt);   // a float sum kept in a double
    NetAddress from{};
    GrowingBuffer* buffer = &g_receiveBuffer;
    while (socket.Receive(&from, buffer)) {
        if (buffer->Size() == 0) {
            if (g_netLog) {
                std::string address = from.ToString();
                const char* text = address.data();
                LogAddress("ZERO length packet received from ", text);
            }
            continue;
        }
        std::uint8_t kind;
        buffer->Get(kind);
        BufferCursor cursor;
        cursor.BeginRead(buffer, buffer->Unread());
        switch (static_cast<PacketKind>(kind)) {
        case PacketKind::Master: OnMaster(&from, &cursor); break;
        case PacketKind::Update: OnUpdate(&from, &cursor); break;
        case PacketKind::Leave: OnLeave(&from, &cursor); break;
        case PacketKind::GetInfo: OnGetInfo(&from, &cursor); break;
        case PacketKind::Login: OnLogin(&from, &cursor); break;
        case PacketKind::Info: OnInfo(&from, &cursor); break;
        case PacketKind::Accepted: OnAccepted(&from, &cursor); break;
        case PacketKind::Rejected: OnRejected(&from, &cursor); break;
        default: break;   // 2 and anything else: ignored
        }
    }
    for (auto node = clients.begin(); node != clients.end();) {
        auto next = std::next(node);
        Connection* connection = node->Get();
        if (!connection->Tick(dt)) {
            if (g_netLog) {
                std::string address = connection->address.ToString();
                const char* text = address.data();
                LogAddress("TIMEOUT client, drop ", text);
            }
            OnClientLost(connection);
            RemoveClient(node);
        }
        node = next;
    }
    ++frame;
}

// ---- Session virtuals ---------------------------------------------------------------

std::list<SharedRef<Connection>>::iterator SessionBase::FindClient(const NetAddress* address) {
    auto node = clients.begin();
    while (node != clients.end() && std::memcmp(&node->Get()->address, address, sizeof(NetAddress)) != 0) ++node;
    return node;
}

// Logs it, closes the connection and erases the node.
void SessionBase::RemoveClient(std::list<SharedRef<Connection>>::iterator node) {
    if (node == clients.end()) return;
    if (g_netLog) LogConnection("REMOVE client ", node->Get(), nullptr);
    node->Get()->Close();
    clients.erase(node);
}

// PacketKind::Update: an update from a client. Unknown senders are logged (returns
// false); a client whose update does not read is logged, reported and removed.
bool SessionBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    auto node = FindClient(from);
    if (node == clients.end()) {
        LogText("ERROR: Update from unknown host");
        return false;
    }
    if (!node->Get()->ReadUpdate(cursor)) {
        if (g_netLog) LogConnection("ERROR: client ", node->Get(), " sent incorrect update");
        OnClientLost(node->Get());
        RemoveClient(node);
    }
    return true;
}

// PacketKind::Leave: a client leaves: its connection id follows; when it matches,
// the client is reported and removed.
void SessionBase::OnLeave(const NetAddress* from, BufferCursor* cursor) {
    std::uint32_t id;
    cursor->Take(id);
    auto node = FindClient(from);
    if (node != clients.end() && node->Get()->id == id) {
        OnClientLeft(node->Get());
        RemoveClient(node);
    }
}

// The base session's handlers of the kinds it does not serve: each logs an error.
void SessionBase::OnGetInfo(const NetAddress*, BufferCursor*) { LogText("ERROR: Can`t handle get info"); }
void SessionBase::OnLogin(const NetAddress*, BufferCursor*) { LogText("ERROR: Can`t handle login"); }
void SessionBase::OnInfo(const NetAddress*, BufferCursor*) { LogText("ERROR: Can`t handle info"); }
void SessionBase::OnAccepted(const NetAddress*, BufferCursor*) { LogText("ERROR: Can`t handle accepted"); }
void SessionBase::OnRejected(const NetAddress*, BufferCursor*) { LogText("ERROR: Can`t handle rejected"); }
void SessionBase::OnMaster(const NetAddress*, BufferCursor*) { LogText("ERROR: Can`t handle master packet"); }

// ---- Byte buffer writes and small socket helpers ---------------------------------------------------

int g_remoteRate = /*00783E34*/ 2500;
int g_localRate = /*00783E38*/ 12500;

bool SessionBase::Send(const NetAddress* address, GameBuffer* buffer) { return socket.SendTo(address, buffer); }

std::uint32_t SessionBase::RateFor(const NetAddress* address) {
    bool local = socket.IsLocal(address);
    return static_cast<std::uint32_t>(local ? g_localRate : g_remoteRate);
}

void SessionBase::SendLeave(const NetAddress* address, std::uint32_t id) {
    GrowingBuffer buffer;
    buffer.Put(static_cast<std::uint8_t>(PacketKind::Leave));
    buffer.Put(id);
    socket.SendTo(address, &buffer);
}

// Every client is sent a leave packet; the members then close the socket, free the type registry and release the
// connections.
SessionBase::~SessionBase() {
    for (auto& node : clients) SendLeave(&node.Get()->address, node.Get()->id);
}

// ---- Adding a client ---------------------------------------------------------------------------------

// The send interval is the constant over the rate clamped to [500, 50000].
void SessionBase::AddClient(const NetAddress* address, std::uint32_t id, int rate) {
    if (g_netLog) {
        std::string peer = address->ToString();
        const char* text = peer.data();
        LogAddress("ADD client ", text);
    }
    RemoveClient(FindClient(address));
    clients.emplace_back(new Connection(*this, id));
    Connection* added = clients.back().Get();
    added->address = *address;
    if (rate <= 500) rate = 500;
    else if (rate >= 50000) rate = 50000;
    added->sendInterval = 250.0f / static_cast<float>(rate);
}

int BaseCompanionLimit() {
    const ServerSession* server = g_gameServer.Get();
    return server ? static_cast<int>(server->companions) : kMostCompanions;
}

} // namespace net

// --- Pump --------------------------------------------------------------------------------------------

// The connection pump of a network session and its start: a driver link (net_link.h) per remote player. The
// session keeps two lists of the drivers (owning, and holders). See docs/notes/network.md.

// A driver for every connection of `session` this one has none for, when the connection has something queued
// and the game server knows it; then each driver pumped, and those whose player is gone or detached erased.
void netlink::DriverSession::PumpDrivers(net::SessionBase* session) {
    if (!IsLive(session)) return;
    for (auto& node : session->clients) {
        net::Connection* player = node.Get();
        bool known = false;
        for (auto& it : drivers) {
            if (it.Get()->player.Get() == player) {
                known = true;
                break;
            }
        }
        if (known) continue;
        if (node.Get()->reliableIn.Size() <= 0) continue;
        net::ServerSession* server = net::g_gameServer;
        if (!IsLive(server)) continue;
        if (!server->IsConnecting(node.Get())) continue;
        player = node.Get();
        DriverLink* driver = NewDriver(this, player);
        holders.emplace_back(driver);
    }
    for (auto it = drivers.begin(); it != drivers.end();) {
        it->Get()->Pump();
        if (IsLive(it->Get()->player.Get())) {
            ++it;
        } else {
            it = drivers.erase(it);
        }
    }
}

// Both lists emptied, then one driver for `player`: started, and its pending output
// drained into throwaway memory streams while its virtual check answers true. Returns the driver.
netlink::DriverLink* netlink::DriverSession::Start(net::Connection* player) {
    drivers.clear();
    holders.clear();
    DriverLink* driver = NewDriver(this, player);
    driver->Pump();
    while (driver->HasIncoming()) {
        MemoryStream stream;
        driver->Receive(&stream);
    }
    return driver;
}
