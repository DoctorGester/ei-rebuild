// The game server's players and their connections: lookups ( ), admission and the password, kicking and
// banning the departure reports and the lines to every guest. See net_classes.h and docs/notes/network.md.
// Players (players) are PlayerMessages, bindings (bindings) BindingMessages (net_server_players.cpp).
#include "core_refcount.h"

#include "core_streams.h"
#include "game_singletons.h"
#include "net_connection.h"
#include "net_messages.h"
#include "ui.h"
#include "world_data.h"
#include "lib/nan_audit.h"
#include "lib/text_case.h"

#include <bit>
#include <cstring>
#include <string>

namespace net {

namespace {

constexpr std::size_t kPasswordSize = 0x14;

// The connection of the first binding whose player is live and the host, or null.
Connection* HostConnection(const std::vector<SharedRef<BindingMessage>>& bindings) {
    for (auto& node : bindings) {
        PlayerMessage* player = node.Get()->PlayerInfo();
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
            if (binding->Get()->PlayerInfo() != player) continue;
            Connection* connection = binding->Get()->ConnectionRef().Get();
            bool hostsToo = false;
            for (auto other = first; other != currentBindings.end(); ++other) {
                if (other->Get()->ConnectionRef().Get() != connection) continue;
                PlayerMessage* owner = other->Get()->PlayerInfo();
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
        if (node->Get()->PlayerInfo()->serial == serial) return node->Get()->ConnectionRef().Get();
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
            PlayerMessage* player = binding->PlayerInfo();
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
            PlayerMessage* player = binding->PlayerInfo();
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
        PlayerMessage* player = binding->PlayerInfo();
        if (!IsLive(player)) return;
        AnnounceText(text, player->name.data(), nullptr);
        return;
    }
}

// A client lost (text 4) and a client left (text 1).
void ServerSession::OnClientLost(Connection* connection) { ReportDeparture(connection, 4); }
void ServerSession::OnClientLeft(Connection* connection) { ReportDeparture(connection, 1); }

} // namespace net

// --- Net server join ---------------------------------------------------------------------------------

// The game server's handlers of the login message (LoginMessage, registered by the server constructor):
// a client joined (a player record and a connection binding for it), its record changed or it left. See
// net_classes.h and docs/notes/network.md.

namespace net {

namespace {
// The serial the next player record gets.
std::uint32_t g_playerSerial = /*0079B60C*/ {};

// Every element of `list` holding `value` erased.
template <typename List>
void EraseValue(List& list, const void* value) {
    std::erase_if(list, [value](const auto& element) { return element.Get() == value; });
}

} // namespace

// A client joined (a LoginMessage). Appends a player record (the login's texts and
// values, the lowest free player number, the next serial, the server's next line number) and a
// binding (the player and the current connection) the login keeps. The player is the host when
// the login asks (with a local client) or when it is the only binding. Announces the player's name.
void ServerOnJoin(ServerSession* self, LoginMessage* msg) {
    auto* player = NewCounted<PlayerMessage>();
    self->players.emplace_back(player);
    PlayerMessage* record = self->players.back().Get();

    auto* binding = NewCounted<BindingMessage>();
    self->bindings.emplace_back(binding);
    BindingMessage* bound = self->bindings.back().Get();

    std::string* name = &record->name;
    *name = msg->name;
    record->state = msg->state;
    record->text = msg->text;
    record->id = msg->id;

    // The lowest player number no player has yet.
    int number = 1;
    for (;;) {
        bool used = false;
        for (auto& it : self->players) {
            if (it.Get()->number == number) {
                used = true;
                break;
            }
        }
        if (!used) break;
        ++number;
    }
    record->number = number;
    record->serial = static_cast<std::int32_t>(g_playerSerial);
    ++g_playerSerial;
    record->firstLine = self->nextLine;
    bound->PlayerRef().Assign(record);
    bound->ConnectionRef().Assign(g_currentConnection);
    msg->binding.Assign(bound);

    ClientSession* client = g_gameClient;
    bool host;
    if (client && !(client->flags7 & RefObject::kDetached)) {
        host = msg->host != 0;
    } else {
        host = self->bindings.size() == 1;
    }
    if (host) record->host = 1;

    self->Announce(g_interface.MessageText(0, name->data()), nullptr);
}

// Channel 1: the client's login changed: its player record takes the state and id, and its version goes up.
void ServerOnPlayerChanged(ServerSession*, LoginMessage* message) {
    message->binding.Get()->PlayerInfo()->state = message->state;
    message->binding.Get()->PlayerInfo()->id = message->id;
    PlayerMessage* player = message->binding.Get()->PlayerInfo();
    ++player->version;
}

// Channel 2: the client left. With a live binding: its live player is removed from the
// players list, and when it was the host the first remaining player becomes the host (its version
// goes up); then the binding is removed from the bindings list.
void ServerOnPlayerGone(ServerSession* self, LoginMessage* message) {
    auto* binding = message->binding.Get();
    if (!IsLive(binding)) return;
    auto* player = binding->PlayerInfo();
    if (IsLive(player)) {
        std::uint8_t wasHost = player->host;
        EraseValue(self->players, player);
        if (wasHost) {
            if (!self->players.empty()) {
                auto* first = self->players.front().Get();
                first->host = 1;
                ++first->version;
            }
        }
    }
    EraseValue(self->bindings, message->binding.Get());
}

} // namespace net

// --- Net server chat ---------------------------------------------------------------------------------

// The game server's chat and system lines: kept on lines for the clients, at most 50. See net_classes.h and
// docs/notes/network.md.

namespace net {

namespace {
constexpr std::uint32_t kMaxLines = 50;

} // namespace

// Appends a line (the next line number, the text, the connection of `player`'s binding,
// null for the server's own lines) and drops the oldest past 50.
void ServerSession::Announce(std::string_view text, RefObject* player) {
    auto* line = NewCounted<LineMessage>();
    lines.emplace_back(line);

    auto* added = lines.back().Get();
    added->text.assign(text);
    added->number = nextLine;
    ++nextLine;

    RefObject* connection = nullptr;
    if (player && !(player->flags7 & RefObject::kDetached)) {
        for (auto& node : bindings) {
            auto* binding = node.Get();
            if (binding->PlayerRef().Get() == player) {
                connection = binding->ConnectionRef().Get();
                break;
            }
        }
    }
    added->connection.Assign(connection);

    if (lines.size() > kMaxLines) lines.erase(lines.begin());
}

// Announce with text `kind` of the text table formatted with `text`.
void ServerSession::AnnounceText(int textKind, const char* text, RefObject* player) {
    Announce(g_interface.MessageText(textKind, text), player);
}

} // namespace net

// --- Net server vars ---------------------------------------------------------------------------------

// The game server's script variables: named floats the script reads and writes in a
// network game, replicated to the clients as VariableMessages. See net_messages.h and docs/notes/network.md.

namespace net {

// The search the original inlines in each function: the first variable whose name has the same bytes as
// `name`.
std::vector<SharedRef<VariableMessage>>::iterator ServerSession::FindVariable(const std::string* variableName) {
    auto node = variables.begin();
    for (; node != variables.end(); ++node)
        if (node->Get()->name == *variableName) break;
    return node;
}

std::vector<SharedRef<VariableMessage>>::iterator ServerSession::FindOrAddVariable(const std::string* variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) return node;

    auto first = variables.emplace(variables.begin(), NewCounted<VariableMessage>());

    // The name, assigned in place (string::operator= inlined: overwrite, then cut or append).
    std::string* to = &first->Get()->name;
    if (variableName != to) *to = *variableName;
    return first;
}

// Sets the variable; a zero value removes it instead. A set bumps the version and takes the next serial.
void ServerSession::SetVariable(const std::string* newName, float value) {
    EI_NAN_AUDIT(value);
    if (value != 0.0f) {
        VariableMessage* variable = FindOrAddVariable(newName)->Get();
        variable->value = value;
        variable->serial = g_variableSerial;
        ++g_variableSerial;
        ++variable->version;
        return;
    }
    auto found = FindVariable(newName);
    if (found != variables.end()) variables.erase(found);
}

// Raises the variable to `value` (the larger of the two; `value` when either is NaN),
// made when missing; a result of zero (or NaN) removes it.
void ServerSession::RaiseVariable(const std::string* variableName, float value) {
    auto found = FindOrAddVariable(variableName);
    VariableMessage* variable = found->Get();
    float current = variable->value;
    std::uint32_t currentBits, valueBits;
    currentBits = std::bit_cast<std::uint32_t>(current);
    valueBits = std::bit_cast<std::uint32_t>(value);
    std::uint32_t bits = current >= value ? currentBits : valueBits;
    float selected;
    selected = std::bit_cast<float>(bits);
    volatile double loaded = selected;   // reloaded through memory, as the original does
    found->Get()->value = static_cast<float>(loaded);
    found->Get()->serial = g_variableSerial;
    ++g_variableSerial;
    ++found->Get()->version;
    EI_NAN_AUDIT(found->Get()->value);
    if (found->Get()->value == 0.0f) {
        auto node = FindVariable(variableName);
        if (node != variables.end()) variables.erase(node);
    }
}

// Removes the variable when it exists.
void ServerSession::RemoveVariable(const std::string* variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) variables.erase(node);
}

// The variable's value, 0 when missing (returned unrounded).
double ServerSession::GetVariable(const std::string* variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) return node->Get()->value;
    return 0.0f;
}

} // namespace net
