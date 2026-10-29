// The game server's players and their connections: lookups ( ), admission and the password, kicking and
// banning the departure reports and the lines to every guest. See net_classes.h and docs/notes/network.md.
// Players (players) are PlayerMessages, bindings (bindings) BindingMessages (net_server_join.cpp).
#include "core_refcount.h"
#include "net_connection.h"
#include "net_messages.h"

#include "lib/text_case.h"

#include <cstring>

namespace net {

namespace {

constexpr std::size_t kPasswordSize = 0x14;

PlayerMessage* PlayerOf(BindingMessage* binding) { return binding->Player(); }

// The connection of the first binding whose player is live and the host, or null.
Connection* HostConnection(const std::vector<SharedRef<BindingMessage>>& bindings) {
    for (auto& node : bindings) {
        PlayerMessage* player = PlayerOf(node.Get());
        if (IsLive(player) && player->host) return node.Get()->ConnectionRef().Get();
    }
    return nullptr;
}

// The common tail of kicking and banning: the client is told to leave and removed.
void DropClient(ServerSession* self, Connection* connection) {
    self->SendLeave(&connection->address, connection->id);
    self->RemoveClient(self->FindClient(&connection->address));
}

} // namespace

// The id of the first host player, -1 without one.
int ServerSession::LeaderId() {
    const auto& currentPlayers = players;
    for (auto node = currentPlayers.begin(); node != currentPlayers.end(); ++node)
        if (node->Get()->host) return node->Get()->id;
    return -1;
}

// Whether a client from `from` may join. Refused when the address is banned (reason 2), when a password is
// set and the 20 bytes at `*password` differ (reason 3; the pointer moves past them either way), or when the
// clients and the pending joins reach the maximum (reason 1).
bool ServerSession::Admit(const NetAddress* from, std::uint8_t** passwordOut, std::uint8_t* reason) {
    *reason = 2;
    // find_if over the banned addresses: the host compared, not the port.
    auto newBanned = banned.begin();
    while (newBanned != banned.end() && newBanned->ip != from->ip) ++newBanned;
    if (newBanned != banned.end()) return false;

    const char* stored = password;
    if (stored[0]) {
        *reason = 3;
        std::uint8_t given[kPasswordSize] = {};
        std::memcpy(given, *passwordOut, kPasswordSize);
        *passwordOut += kPasswordSize;
        if (std::memcmp(given, stored, kPasswordSize) != 0) return false;
    }
    *reason = 1;
    std::uint32_t clientCount = ClientCount();
    const auto pending = static_cast<std::uint32_t>(joins.size());
    return pending + clientCount < maxPlayers;
}

// The join password (at most 20 bytes, zero-filled).
void ServerSession::SetPassword(const char* text) {
    char* stored = password;
    std::memset(stored, 0, kPasswordSize);
    #pragma warning(suppress : 4996)   // strncpy: the copy padded with zeros
    std::strncpy(stored, text, kPasswordSize);
}

// The connection of the first player whose name starts with `name` (case-insensitive)
// and whose connection is not also the host's; null without one.
Connection* ServerSession::FindGuestConnection(const std::string* guestName) {
    const std::size_t length = guestName->size();
    for (auto& node : players) {
        PlayerMessage* player = node.Get();
        if (text::CompareIgnoringCase(player->name.data(), guestName->data(), length) != 0) continue;
        const auto& currentBindings = bindings;
        auto first = currentBindings.begin();
        for (auto binding = first; binding != currentBindings.end(); ++binding) {
            if (PlayerOf(binding->Get()) != player) continue;
            Connection* connection = binding->Get()->ConnectionRef().Get();
            bool hostsToo = false;
            for (auto other = first; other != currentBindings.end(); ++other) {
                if (other->Get()->ConnectionRef().Get() != connection) continue;
                PlayerMessage* owner = PlayerOf(other->Get());
                if (owner && !(owner->flags7 & RefObject::kDetached) && owner->host) {
                    hostsToo = true;
                    break;
                }
            }
            if (!hostsToo) return binding->Get()->ConnectionRef().Get();
            break;
        }
    }
    return nullptr;
}

// The connection of the player with serial `serial`, or null.
Connection* ServerSession::FindConnectionBySerial(int serial) {
    const auto& currentBindings = bindings;
    for (auto node = currentBindings.begin(); node != currentBindings.end(); ++node)
        if (PlayerOf(node->Get())->serial == serial) return node->Get()->ConnectionRef().Get();
    return nullptr;
}

// The player with id `id`, or null.
PlayerMessage* ServerSession::FindPlayer(int id) {
    const auto& currentPlayers = players;
    for (auto node = currentPlayers.begin(); node != currentPlayers.end(); ++node)
        if (node->Get()->id == id) return node->Get();
    return nullptr;
}

// A line (text `kind` with `text`) for every player that is not the host.
void ServerSession::AnnounceToGuests(int textKind, const char* text) {
    for (auto& node : players) {
        PlayerMessage* player = node.Get();
        if (!player->host) AnnounceText(textKind, text, player);
    }
}

// Kicks the client on `connection` (unless it is the host's): its player is announced
// (text 2), then the client is told to leave and removed.
void ServerSession::Kick(Connection* connection) {
    if (!IsLive(connection)) return;
    const auto& currentBindings = bindings;
    Connection* host = HostConnection(currentBindings);
    if (connection == host) return;
    for (auto& node : currentBindings) {
        if (node.Get()->ConnectionRef().Get() != connection) continue;
        BindingMessage* binding = node.Get();
        if (binding) {
            PlayerMessage* player = PlayerOf(binding);
            if (IsLive(player)) AnnounceText(2, player->name.data(), nullptr);
        }
        break;
    }
    DropClient(this, connection);
}

// Bans the client on `connection` (unless it is the host's): its player is announced
// (text 3), its address added to the banned list, then the client is told to leave and removed.
void ServerSession::Ban(Connection* connection) {
    if (!IsLive(connection)) return;
    const auto& currentBindings = bindings;
    Connection* host = HostConnection(currentBindings);
    if (connection == host) return;
    for (auto& node : currentBindings) {
        if (node.Get()->ConnectionRef().Get() != connection) continue;
        BindingMessage* binding = node.Get();
        if (binding) {
            PlayerMessage* player = PlayerOf(binding);
            if (IsLive(player)) AnnounceText(3, player->name.data(), nullptr);
        }
        break;
    }
    const NetAddress* address = &connection->address;
    banned.push_back(*address);
    DropClient(this, connection);
}

// A client lost and a client left: the client's player is announced (text 4 / text 1).
void ServerSession::ReportDeparture(Connection* connection, int text) {
    if (!IsLive(connection)) return;
    const auto& currentBindings = bindings;
    for (auto& node : currentBindings) {
        if (node.Get()->ConnectionRef().Get() != connection) continue;
        BindingMessage* binding = node.Get();
        if (!IsLive(binding)) return;
        PlayerMessage* player = PlayerOf(binding);
        if (!IsLive(player)) return;
        AnnounceText(text, player->name.data(), nullptr);
        return;
    }
}

// A client lost (text 4) and a client left (text 1).
void ServerSession::OnClientLost(Connection* connection) { ReportDeparture(connection, 4); }
void ServerSession::OnClientLeft(Connection* connection) { ReportDeparture(connection, 1); }

} // namespace net
