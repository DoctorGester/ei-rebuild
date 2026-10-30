// The network pump. See net_session.h.
#include "net_session.h"

#include "console.h"
#include "core_refcount.h"
#include "core_streams.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "net_classes.h"
#include "net_coder.h"
#include "net_connection.h"
#include "net_link.h"
#include "net_messages.h"
#include "world.h"
#include "lib/bytes.h"
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
    if (!session) return;
    if (session->flags7 & RefCounted::kDetached) return;
    // At most 5000 milliseconds (Pump): exact in a float, as is the product.
    const float seconds = static_cast<float>(elapsed) * kMillisecondsToSeconds;
    session->Tick(seconds);
}

} // namespace

// Their static initialisers (in the list's order): the counted pointer's constructor clears it, then the exit
// function is registered, so the sessions go in the game's exit order, not at our CRT's (the harness's network pair
// has ended by then: net_pair_zone's leave packets went to the other game's ports).
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
    std::uint32_t now = platform::Milliseconds();
    std::uint32_t elapsed = now - g_lastPumpTime;
    if (elapsed > 5000) elapsed = 1000;   // after a long stall, pretend one second passed
    g_lastPumpTime = now;
    TickSession(g_gameServer, elapsed);
    TickSession(g_gameClient, elapsed);
    g_gameFlow.UpdateMultiplayerVariable();   // With the game flow: the per-frame network step
}

} // namespace net

// --- Base --------------------------------------------------------------------------------------------

// The network session base class: construction, the message type registry and
// the handler tables. See net_classes.h and docs/notes/network.md.

namespace net {

int TypeRegistry::TypeOf(const std::uintptr_t* key) const {
    auto node = byVtable.find(*key);
    return node != byVtable.end() ? static_cast<int>(node->second) : -1;
}

void TypeRegistry::Register(std::uint32_t type, MessageFactory factory) {
    Message* prototype = factory();
    if (prototype) prototype->AddRef();
    byVtable[KeyOf(prototype)] = type;
    byType[type] = factory;
    if (prototype) prototype->Release();
}

Message* TypeRegistry::Create(std::uint32_t type) {
    MessageFactory factory = byType[type];
    return factory ? factory() : nullptr;
}

SessionBase::SessionBase() {
    time = 0.0;
    frame = 1;
    magic = 0;
    timeout = 0x3C;
    maxRoundTrip = 5;
    idReuseTime = 0x4B;
}

void SessionBase::AddHandler(int channel, std::uint32_t type, MessageHandler handler) {
    if (channel < 0 || channel >= kChannels) return;
    handlers[channel][type & 0xFF].push_back(handler);
}

void SessionBase::AddHandlerForPrototype(Message* prototype, int channel, MessageHandler handler) {
    SharedRef<Message> ref(prototype);
    const std::uintptr_t key = TypeRegistry::KeyOf(ref);
    int type = types.TypeOf(&key);
    AddHandler(channel, static_cast<std::uint32_t>(type), handler);
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
        reliable[type] = 0;
        types.Register(type, kFactories[type - 1]);
    }
}

std::uint32_t SessionBase::ClientCount() const { return static_cast<std::uint32_t>(clients.size()); }

} // namespace net

// --- Pump --------------------------------------------------------------------------------------------

// The connection pump of a network session and its start: a driver link (net_link.h) per remote player. The
// session keeps two lists of the drivers (owning, and holders). See docs/notes/network.md.

namespace netlink {

namespace {

// A new driver for `player`, put at the end of the owning list (shared there).
DriverLink* NewDriver(DriverSession* self, net::Connection* player) {
    auto* driver = net::NewCounted<DriverLink>();
    driver->player.Assign(player);
    self->drivers.emplace_back(driver);
    return driver;
}

} // namespace

// A driver for every connection of `session` this one has none for, when the connection has something queued
// and the game server knows it; then each driver pumped, and those whose player is gone or detached erased.
void DriverSession::PumpDrivers(net::SessionBase* session) {
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
        GameBuffer* in = &node.Get()->reliableIn;
        net::BufferUpdateHigh(in);
        if (in->PositionOf(in->high) <= 0) continue;
        net::ServerSession* server = net::g_gameServer;
        if (!IsLive(server)) continue;
        if (!server->PlayerStateIs3(node.Get())) continue;
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
DriverLink* DriverSession::Start(net::Connection* player) {
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

} // namespace netlink

// --- Tick --------------------------------------------------------------------------------------------

// The session's receive step, its packet handlers, sends and destruction,
// and the byte buffer helpers they use. See net_classes.h and docs/notes/network.md.

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
    std::string peer = connection->address.ToString();
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
    time = static_cast<float>(time) + static_cast<float>(dt);   // a float sum kept in a double
    NetAddress from{};
    GrowingBuffer* buffer = &g_receiveBuffer;
    while (socket.Receive(&from, buffer)) {
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
    for (auto node = clients.begin(); node != clients.end();) {
        auto next = std::next(node);
        Connection* connection = node->Get();
        if (!connection->Tick(dt)) {
            if (g_netLog) {
                std::string address = connection->address.ToString();
                const char* text = address.data();
                LogAddress(game::nettexts::Timeout(), text);
            }
            OnClientLost(connection);
            RemoveClient(node);
        }
        node = next;
    }
    ++frame;
    g_currentSession = nullptr;
}

} // namespace net

// ---- Session virtuals ---------------------------------------------------------------

namespace net {

std::list<SharedRef<Connection>>::iterator SessionBase::FindClient(const NetAddress* address) {
    auto node = clients.begin();
    while (node != clients.end() && std::memcmp(&node->Get()->address, address, sizeof(NetAddress)) != 0) ++node;
    return node;
}

// Logs it, closes the connection and erases the node.
void SessionBase::RemoveClient(std::list<SharedRef<Connection>>::iterator node) {
    if (node == clients.end()) return;
    if (g_netLog) LogConnection(game::nettexts::RemoveClient(), node->Get(), nullptr);
    node->Get()->Close();
    clients.erase(node);
}

// Packet kind 0: an update from a client. Unknown senders are logged (returns
// false); a client whose update does not read is logged, reported and removed.
bool SessionBase::OnUpdate(const NetAddress* from, BufferCursor* cursor) {
    auto node = FindClient(from);
    if (node == clients.end()) {
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
    if (node != clients.end() && node->Get()->id == id) {
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

namespace net {

int g_remoteRate = /*00783E34*/ 2500;
int g_localRate = /*00783E38*/ 12500;

bool SessionBase::Send(const NetAddress* address, GameBuffer* buffer) { return socket.SendTo(address, buffer); }

std::uint32_t SessionBase::RateFor(const NetAddress* address) {
    bool local = socket.IsLocal(address);
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
    socket.SendTo(address, buffer);
}

} // namespace net

namespace net {

// Every client is sent a leave packet; the members then close the socket, free the type registry and release the
// connections.
SessionBase::~SessionBase() {
    for (auto& node : clients) {
        Connection* connection = node.Get();
        std::uint32_t id = connection->id;
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
        socket.SendTo(&connection->address, buffer);
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
    clients.emplace_back(NewCounted<Connection>(id));
    Connection* added = clients.back().Get();
    added->address = *address;
    if (rate <= 500) rate = 500;
    else if (rate >= 50000) rate = 50000;
    added->sendInterval = 250.0f / static_cast<float>(rate);
}

} // namespace net
