// The game server's players and their connections: lookups ( ), admission and the password, kicking and
// banning the departure reports and the lines to every guest. See net_classes.h and docs/notes/network.md.
// Players (players_) are PlayerMessages, bindings (bindings_) BindingMessages (net_server_join.cpp).
#include "net_messages.h"
#include "net_connection.h"
#include "core_refcount.h"

#include "lib/text_case.h"

#include <cstring>

namespace net {

namespace {

constexpr std::size_t kPasswordSize = 0x14;

void* Value(RefNode* node) { return node->data.ptr; }
PlayerMessage* PlayerOf(BindingMessage* binding) { return binding->Player(); }
Connection* ConnectionOf(BindingMessage* binding) { return binding->ConnectionRef().Get(); }

// The connection of the first binding whose player is live and the host, or null.
Connection* HostConnection(const stlp::SharedRefListOf<BindingMessage>& bindings) {
    for (RefNode* node = bindings.First(); node != bindings.End(); node = node->next) {
        PlayerMessage* player = PlayerOf(bindings.At(node));
        if (IsLive(player) && player->host_) return ConnectionOf(bindings.At(node));
    }
    return nullptr;
}

// The common tail of kicking and banning: the client is told to leave and removed.
void DropClient(ServerSession* self, Connection* connection) {
    self->SendLeave(&connection->address_, connection->id_);
    ClientNode* node;
    self->FindClient(&node, &connection->address_);
    self->RemoveClient(node);
}

} // namespace

// The id of the first host player, -1 without one.
int ServerSession::LeaderId() {
    const auto& players = players_;
    for (RefNode* node = players.First(); node != players.End(); node = node->next)
        if (players.At(node)->host_) return players.At(node)->id_;
    return -1;
}

// Whether a client from `from` may join. Refused when the address is banned (reason 2), when a password is
// set and the 20 bytes at `*password` differ (reason 3; the pointer moves past them either way), or when the
// clients and the pending joins reach the maximum (reason 1).
bool ServerSession::Admit(const NetAddress* from, std::uint8_t** password, std::uint8_t* reason) {
    *reason = 2;
    // find_if over the banned addresses: the host compared, not the port.
    auto banned = banned_.begin();
    while (banned != banned_.end() && banned->ip != from->ip) ++banned;
    if (banned != banned_.end()) return false;

    const char* stored = password_;
    if (stored[0]) {
        *reason = 3;
        std::uint8_t given[kPasswordSize] = {};
        std::memcpy(given, *password, kPasswordSize);
        *password += kPasswordSize;
        if (std::memcmp(given, stored, kPasswordSize) != 0) return false;
    }
    *reason = 1;
    std::uint32_t clients = ClientCount();
    const auto pending = static_cast<std::uint32_t>(joins_.size());
    return pending + clients < maxPlayers_;
}

// The join password (at most 20 bytes, zero-filled).
void ServerSession::SetPassword(const char* text) {
    char* stored = password_;
    std::memset(stored, 0, kPasswordSize);
    #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
    std::strncpy(stored, text, kPasswordSize);
}

// The connection of the first player whose name starts with `name` (case-insensitive)
// and whose connection is not also the host's; null without one.
Connection* ServerSession::FindGuestConnection(const std::string* name) {
    const std::size_t length = name->size();
    for (RefNode* node = players_.First(); node != players_.End(); node = node->next) {
        PlayerMessage* player = players_.At(node);
        if (text::CompareIgnoringCase(player->name_.data(), name->data(), length) != 0) continue;
        const auto& bindings = bindings_;
        RefNode* first = bindings.First();
        for (RefNode* binding = first; binding != bindings.End(); binding = binding->next) {
            if (PlayerOf(bindings.At(binding)) != Value(node)) continue;
            Connection* connection = ConnectionOf(bindings.At(binding));
            bool hostsToo = false;
            for (RefNode* other = first; other != bindings.End(); other = other->next) {
                if (ConnectionOf(bindings.At(other)) != connection) continue;
                PlayerMessage* owner = PlayerOf(bindings.At(other));
                if (owner && !(owner->flags7_ & CRefObject::kDetached) && owner->host_) {
                    hostsToo = true;
                    break;
                }
            }
            if (!hostsToo) return ConnectionOf(bindings.At(binding));
            break;
        }
    }
    return nullptr;
}

// The connection of the player with serial `serial`, or null.
Connection* ServerSession::FindConnectionBySerial(int serial) {
    const auto& bindings = bindings_;
    for (RefNode* node = bindings.First(); node != bindings.End(); node = node->next)
        if (PlayerOf(bindings.At(node))->serial_ == serial) return ConnectionOf(bindings.At(node));
    return nullptr;
}

// The player with id `id`, or null.
PlayerMessage* ServerSession::FindPlayer(int id) {
    const auto& players = players_;
    for (RefNode* node = players.First(); node != players.End(); node = node->next)
        if (players.At(node)->id_ == id) return players.At(node);
    return nullptr;
}

// A line (text `kind` with `text`) for every player that is not the host.
void ServerSession::AnnounceToGuests(int kind, const char* text) {
    for (RefNode* node = players_.First(); node != players_.End(); node = node->next) {
        PlayerMessage* player = players_.At(node);
        if (!player->host_) AnnounceText(kind, text, player);
    }
}

// Kicks the client on `connection` (unless it is the host's): its player is announced
// (text 2), then the client is told to leave and removed.
void ServerSession::Kick(Connection* connection) {
    if (!IsLive(connection)) return;
    const auto& bindings = bindings_;
    Connection* host = HostConnection(bindings);
    if (connection == host) return;
    for (RefNode* node = bindings.First(); node != bindings.End(); node = node->next) {
        if (ConnectionOf(bindings.At(node)) != connection) continue;
        BindingMessage* binding = bindings.At(node);
        if (binding) {
            PlayerMessage* player = PlayerOf(binding);
            if (IsLive(player)) AnnounceText(2, player->name_.data(), nullptr);
        }
        break;
    }
    DropClient(this, connection);
}

// Bans the client on `connection` (unless it is the host's): its player is announced
// (text 3), its address added to the banned list, then the client is told to leave and removed.
void ServerSession::Ban(Connection* connection) {
    if (!IsLive(connection)) return;
    const auto& bindings = bindings_;
    Connection* host = HostConnection(bindings);
    if (connection == host) return;
    for (RefNode* node = bindings.First(); node != bindings.End(); node = node->next) {
        if (ConnectionOf(bindings.At(node)) != connection) continue;
        BindingMessage* binding = bindings.At(node);
        if (binding) {
            PlayerMessage* player = PlayerOf(binding);
            if (IsLive(player)) AnnounceText(3, player->name_.data(), nullptr);
        }
        break;
    }
    const NetAddress* address = &connection->address_;
    banned_.push_back(*address);
    DropClient(this, connection);
}

// A client lost and a client left: the client's player is announced (text 4 / text 1).
void ServerSession::ReportDeparture(Connection* connection, int text) {
    if (!IsLive(connection)) return;
    const auto& bindings = bindings_;
    for (RefNode* node = bindings.First(); node != bindings.End(); node = node->next) {
        if (ConnectionOf(bindings.At(node)) != connection) continue;
        BindingMessage* binding = bindings.At(node);
        if (!IsLive(binding)) return;
        PlayerMessage* player = PlayerOf(binding);
        if (!IsLive(player)) return;
        AnnounceText(text, player->name_.data(), nullptr);
        return;
    }
}

// Slots 11 (a client lost: text 4) and 12 (a client left: text 1).
void ServerSession::OnClientLost(Connection* connection) { ReportDeparture(connection, 4); }
void ServerSession::OnClientLeft(Connection* connection) { ReportDeparture(connection, 1); }

} // namespace net
