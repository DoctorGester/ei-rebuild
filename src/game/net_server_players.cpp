// The game server's players and their connections: lookups (0041A020, 0041A170, 0041A270,
// 0041A2E0), admission and the password (0041A050 slot 16, 0041A140), kicking and banning
// (0041A550, 0041A620), the departure reports (0041A700 slot 11, 0041A770 slot 12) and the lines to
// every guest (0041A500). See net_classes.h and docs/notes/network.md.
//
// Players (players_) are PlayerMessages, bindings (bindings_) BindingMessages (net_server_join.cpp).
#include "net_messages.h"
#include "net_connection.h"
#include "core_refcount.h"

#include "lib/crt.h"
#include "ei/hook.h"
#include "originals_library.h"

#include <cstring>

namespace net {

namespace {

// The banned addresses (+0x25C4, STLport list<sockaddr_in>; library): end, begin, find, insert.
__declspec(naked) void** __fastcall BannedBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x0041FCC0)
__declspec(naked) void** __fastcall BannedFind(void** /*out*/, void*, void* /*first*/, void* /*last*/, std::uint32_t /*value*/) EI_ORIGINAL(0x00427FF0)

constexpr std::size_t kPasswordSize = 0x14;

bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}
void** First(void* list) { return static_cast<void**>(static_cast<void**>(list)[0]); }
void* Value(void** node) { return node[2]; }
PlayerMessage* PlayerAt(void** node) { return static_cast<PlayerMessage*>(node[2]); }
BindingMessage* BindingAt(void** node) { return static_cast<BindingMessage*>(node[2]); }
PlayerMessage* PlayerOf(BindingMessage* binding) { return static_cast<PlayerMessage*>(binding->PlayerRef().ptr); }
void* ConnectionOf(BindingMessage* binding) { return binding->ConnectionRef().ptr; }

// The connection of the first binding whose player is live and the host, or null.
void* HostConnection(void** bindings) {
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        PlayerMessage* player = PlayerOf(BindingAt(node));
        if (Live(player) && player->host_) return ConnectionOf(BindingAt(node));
    }
    return nullptr;
}

// The common tail of kicking and banning: the client is told to leave and removed (slot 2).
void DropClient(ServerSession* self, void* peer) {
    auto* connection = static_cast<Connection*>(peer);
    self->SendLeave(&connection->address_, connection->id_);
    ClientNode* node;
    self->FindClient(&node, &connection->address_);
    self->RemoveClient(node);
}

} // namespace

// 0041A020: the id of the first host player, -1 without one.
int ServerSession::LeaderId() {
    void** players = static_cast<void**>(players_);
    for (void** node = First(players); node != players; node = static_cast<void**>(node[0]))
        if (PlayerAt(node)->host_) return PlayerAt(node)->id_;
    return -1;
}
EI_REPLACE_METHOD(0x0041A020, ServerSession::LeaderId);

// 0041A050 (slot 16): whether a client from `address` may join. Refused when the address is banned
// (reason 2), when a password is set and the 20 bytes at `*password` differ (reason 3; the pointer
// moves past them either way), or when the clients and the pending joins reach the maximum
// (reason 1).
bool ServerSession::AdmitImpl(std::uint32_t address, const std::uint8_t** password, std::uint8_t* reason) {
    void* banned = &banned_;
    *reason = 2;
    void* sentinel = banned_;
    void* last;
    ListEndIteratorOriginal(banned, nullptr, &last);
    void* first;
    BannedBegin(banned, nullptr, &first);
    void* found;
    if (*BannedFind(&found, nullptr, first, last, address) != sentinel) return false;

    const std::uint8_t* stored = password_;
    if (stored[0]) {
        *reason = 3;
        std::uint8_t given[kPasswordSize] = {};
        std::memcpy(given, *password, kPasswordSize);
        *password += kPasswordSize;
        if (std::memcmp(given, stored, kPasswordSize) != 0) return false;
    }
    *reason = 1;
    std::uint32_t clients = ClientCount();
    std::uint32_t pending = 0;
    void** list = static_cast<void**>(joins_);
    for (void** node = First(list); node != list; node = static_cast<void**>(node[0])) ++pending;
    return pending + clients < maxPlayers_;
}

// 0041A140: the join password (at most 20 bytes, zero-filled).
void ServerSession::SetPassword(const char* text) {
    char* stored = reinterpret_cast<char*>(password_);
    std::memset(stored, 0, kPasswordSize);
    gamecrt::strncpy(stored, text, kPasswordSize);
}
EI_REPLACE_METHOD(0x0041A140, ServerSession::SetPassword);

// 0041A170: the connection of the first player whose name starts with `name` (case-insensitive)
// and whose connection is not also the host's; null without one.
void* ServerSession::FindGuestConnection(const StringRep* name) {
    const std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    for (void** node = First(players_); node != players_; node = static_cast<void**>(node[0])) {
        PlayerMessage* player = PlayerAt(node);
        if (gamecrt::strnicmp(player->name_.start, name->start, length) != 0) continue;
        void** bindings = static_cast<void**>(bindings_);
        void** first = First(bindings);
        for (void** binding = first; binding != bindings; binding = static_cast<void**>(binding[0])) {
            if (PlayerOf(BindingAt(binding)) != Value(node)) continue;
            void* connection = ConnectionOf(BindingAt(binding));
            bool hostsToo = false;
            for (void** other = first; other != bindings; other = static_cast<void**>(other[0])) {
                if (ConnectionOf(BindingAt(other)) != connection) continue;
                PlayerMessage* owner = PlayerOf(BindingAt(other));
                if (owner && !(owner->flags7_ & CRefObject::kDetached) && owner->host_) {
                    hostsToo = true;
                    break;
                }
            }
            if (!hostsToo) return ConnectionOf(BindingAt(binding));
            break;
        }
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x0041A170, ServerSession::FindGuestConnection);

// 0041A270: the connection of the player with serial `serial`, or null.
void* ServerSession::FindConnectionBySerial(int serial) {
    void** bindings = static_cast<void**>(bindings_);
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0]))
        if (PlayerOf(BindingAt(node))->serial_ == serial) return ConnectionOf(BindingAt(node));
    return nullptr;
}
EI_REPLACE_METHOD(0x0041A270, ServerSession::FindConnectionBySerial);

// 0041A2E0: the player with id `id`, or null.
void* ServerSession::FindPlayer(int id) {
    void** players = static_cast<void**>(players_);
    for (void** node = First(players); node != players; node = static_cast<void**>(node[0]))
        if (PlayerAt(node)->id_ == id) return Value(node);
    return nullptr;
}
EI_REPLACE_METHOD(0x0041A2E0, ServerSession::FindPlayer);

// 0041A500: a line (text `kind` with `text`) for every player that is not the host.
void ServerSession::AnnounceToGuests(int kind, const char* text) {
    for (void** node = First(players_); node != players_; node = static_cast<void**>(node[0])) {
        PlayerMessage* player = PlayerAt(node);
        if (!player->host_) AnnounceText(kind, text, player);
    }
}
EI_REPLACE_METHOD(0x0041A500, ServerSession::AnnounceToGuests);

// 0041A550: kicks the client on `connection` (unless it is the host's): its player is announced
// (text 2), then the client is told to leave and removed.
void ServerSession::Kick(void* connection) {
    if (!Live(connection)) return;
    void** bindings = static_cast<void**>(bindings_);
    void* host = HostConnection(bindings);
    if (connection == host) return;
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        if (ConnectionOf(BindingAt(node)) != connection) continue;
        BindingMessage* binding = BindingAt(node);
        if (binding) {
            PlayerMessage* player = PlayerOf(binding);
            if (Live(player)) AnnounceText(2, player->name_.start, nullptr);
        }
        break;
    }
    DropClient(this, connection);
}
EI_REPLACE_METHOD(0x0041A550, ServerSession::Kick);

// 0041A620: bans the client on `connection` (unless it is the host's): its player is announced
// (text 3), its address added to the banned list, then the client is told to leave and removed.
void ServerSession::Ban(void* connection) {
    if (!Live(connection)) return;
    void** bindings = static_cast<void**>(bindings_);
    void* host = HostConnection(bindings);
    if (connection == host) return;
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        if (ConnectionOf(BindingAt(node)) != connection) continue;
        BindingMessage* binding = BindingAt(node);
        if (binding) {
            PlayerMessage* player = PlayerOf(binding);
            if (Live(player)) AnnounceText(3, player->name_.start, nullptr);
        }
        break;
    }
    const NetAddress* address = &static_cast<Connection*>(connection)->address_;
    void* end;
    ListEndIteratorOriginal(&banned_, nullptr, &end);
    void* inserted;
    ListInsert421AF0Original(&banned_, nullptr, &inserted, end, address);
    DropClient(this, connection);
}
EI_REPLACE_METHOD(0x0041A620, ServerSession::Ban);

// 0041A700 (slot 11, a client lost) and 0041A770 (slot 12, a client left): the client's player is
// announced (text 4 / text 1).
void ServerSession::ReportDeparture(void* connection, int text) {
    if (!Live(connection)) return;
    void** bindings = static_cast<void**>(bindings_);
    for (void** node = First(bindings); node != bindings; node = static_cast<void**>(node[0])) {
        if (ConnectionOf(BindingAt(node)) != connection) continue;
        BindingMessage* binding = BindingAt(node);
        if (!Live(binding)) return;
        PlayerMessage* player = PlayerOf(binding);
        if (!Live(player)) return;
        AnnounceText(text, player->name_.start, nullptr);
        return;
    }
}

// Slots 11 (a client lost: text 4) and 12 (a client left: text 1).
void ServerSession::OnClientLostImpl(Connection* connection) { ReportDeparture(connection, 4); }
void ServerSession::OnClientLeftImpl(Connection* connection) { ReportDeparture(connection, 1); }

namespace {
} // namespace
EI_REPLACE_METHOD(0x0041A050, ServerSession::AdmitImpl);
EI_REPLACE_METHOD(0x0041A700, ServerSession::OnClientLostImpl);
EI_REPLACE_METHOD(0x0041A770, ServerSession::OnClientLeftImpl);

} // namespace net
