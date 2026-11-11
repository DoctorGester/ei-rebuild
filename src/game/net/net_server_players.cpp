// The game server's players and their connections: lookups ( ), admission and the password, kicking and
// banning the departure reports and the lines to every guest. See net/net_session.h and docs/notes/network.md.
// Players (players) are PlayerMessages, bindings (bindings) BindingMessages (net_server_players.cpp).
#include "engine/core/core_refcount.h"

#include "engine/core/core_streams.h"
#include "game/app/singletons.h"
#include "game/net/net_connection.h"
#include "game/net/net_messages.h"
#include "game/ui/ui.h"
#include "lib/float_select.h"
#include "lib/text_case.h"
#include "platform/log.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <string>

namespace net {

namespace {

constexpr std::size_t kPasswordSize = 0x14;

// The connection of the first binding whose player is live and the host, or null.
Connection* HostConnection(const std::vector<SharedRef<BindingMessage>>& bindings) {
    for (auto& node : bindings) {
        PlayerMessage* player = node.Get()->player.Get();
        if (IsLive(player) && player->host) return node.Get()->connection.Get();
    }
    return nullptr;
}

// The common tail of kicking and banning: the client is told to leave and removed.
void DropClient(ServerSession* self, Connection* connection) {
    self->SendLeave(&connection->address, connection->id);
    self->RemoveClient(self->FindClient(&connection->address));
}

// The serial the next player record gets.
std::uint32_t g_playerSerial = /*0079B60C*/ {};

// Every element of `list` holding `value` erased.
template <typename List>
void EraseValue(List& list, const void* value) {
    std::erase_if(list, [value](const auto& element) { return element.Get() == value; });
}

constexpr std::uint32_t kMaxLines = 50;

} // namespace

// The id of the first host player, -1 without one.
int ServerSession::LeaderId() {
    const auto& currentPlayers = players;
    for (auto node = currentPlayers.begin(); node != currentPlayers.end(); ++node)
        if (node->Get()->host) return node->Get()->id;
    return -1;
}

// Whether a client from `from` may join. Refused when the address is banned (Banned), when the client's mod (our
// mark after the 20 password bytes; none from an original client, which counts as playing no mod) is not this game's
// (ModMismatch, ours: a line for the players, once per join `joinId`), when a password is set and the 20 bytes at the
// cursor differ (WrongPassword; read either way), or when the clients and the pending joins reach the maximum (Full).
bool ServerSession::Admit(const NetAddress* from, std::uint32_t joinId, BufferCursor* given, RefusalReason* reason) {
    *reason = RefusalReason::Banned;
    // find_if over the banned addresses: the host compared, not the port.
    auto newBanned = banned.begin();
    while (newBanned != banned.end() && newBanned->ip != from->ip) ++newBanned;
    if (newBanned != banned.end()) return false;

    if (!g_originalPeer) {
        BufferCursor mark = *given;
        std::uint8_t skipped[kPasswordSize];
        mark.Take(skipped);
        std::optional<modidentity::Identity> theirs = ReadModMark(&mark);
        if (!modidentity::Agrees(theirs)) {
            *reason = RefusalReason::ModMismatch;
            if (std::memcmp(&modRefusedFrom, from, sizeof(NetAddress)) != 0 || modRefusedJoin != joinId) {
                ++modRefusals;
                modRefusedFrom = *from;
                modRefusedJoin = joinId;
                ei::Log("mod: refused the join of %s: it plays %s, this game %s", from->ToString(),
                        modidentity::Described(theirs), modidentity::Described(modidentity::Played()));
                Announce(modidentity::RefusedLine(from->ToString(), theirs), nullptr);
            }
            modRefused = std::move(theirs);
            return false;
        }
    }

    const char* stored = password;
    if (stored[0]) {
        *reason = RefusalReason::WrongPassword;
        std::uint8_t sent[kPasswordSize];
        given->Take(sent);
        if (std::memcmp(sent, stored, kPasswordSize) != 0) return false;
    }
    *reason = RefusalReason::Full;
    std::uint32_t clientCount = ClientCount();
    const auto pending = static_cast<std::uint32_t>(joins.size());
    return pending + clientCount < maxPlayers;
}

// The join password (at most 20 bytes, zero-filled).
void ServerSession::SetPassword(std::string_view text) {
    char* stored = password;
    std::memset(stored, 0, kPasswordSize);
    std::memcpy(stored, text.data(), std::min(text.size(), kPasswordSize));
}

// The connection of the first player whose name starts with `name` (case-insensitive)
// and whose connection is not also the host's; null without one.
Connection* ServerSession::FindGuestConnection(const std::string& guestName) {
    const std::size_t length = guestName.size();
    for (auto& node : players) {
        PlayerMessage* player = node.Get();
        if (text::CompareIgnoringCase(player->name.data(), guestName.data(), length) != 0) continue;
        const auto& currentBindings = bindings;
        auto first = currentBindings.begin();
        for (auto binding = first; binding != currentBindings.end(); ++binding) {
            if (binding->Get()->player.Get() != player) continue;
            Connection* connection = binding->Get()->connection.Get();
            bool hostsToo = false;
            for (auto other = first; other != currentBindings.end(); ++other) {
                if (other->Get()->connection.Get() != connection) continue;
                PlayerMessage* owner = other->Get()->player.Get();
                if (IsLive(owner) && owner->host) {
                    hostsToo = true;
                    break;
                }
            }
            if (!hostsToo) return binding->Get()->connection.Get();
            break;
        }
    }
    return nullptr;
}

// The connection of the player with serial `serial`, or null.
Connection* ServerSession::FindConnectionBySerial(int serial) {
    const auto& currentBindings = bindings;
    for (auto node = currentBindings.begin(); node != currentBindings.end(); ++node)
        if (node->Get()->player.Get()->serial == serial) return node->Get()->connection.Get();
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
void ServerSession::AnnounceToGuests(int textKind, std::string_view text) {
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
        if (node.Get()->connection.Get() != connection) continue;
        BindingMessage* binding = node.Get();
        if (binding) {
            PlayerMessage* player = binding->player.Get();
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
        if (node.Get()->connection.Get() != connection) continue;
        BindingMessage* binding = node.Get();
        if (binding) {
            PlayerMessage* player = binding->player.Get();
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
        if (node.Get()->connection.Get() != connection) continue;
        BindingMessage* binding = node.Get();
        if (!IsLive(binding)) return;
        PlayerMessage* player = binding->player.Get();
        if (!IsLive(player)) return;
        AnnounceText(text, player->name.data(), nullptr);
        return;
    }
}

// A client lost (text 4) and a client left (text 1).
void ServerSession::OnClientLost(Connection* connection) { ReportDeparture(connection, 4); }
void ServerSession::OnClientLeft(Connection* connection) { ReportDeparture(connection, 1); }

// --- Net server join ---------------------------------------------------------------------------------

// The game server's handlers of the login message (LoginMessage, from OnMessage):
// a client joined (a player record and a connection binding for it), its record changed or it left. See
// net/net_session.h and docs/notes/network.md.

// A client joined (a LoginMessage). Appends a player record (the login's texts and
// values, the lowest free player number, the next serial, the server's next line number) and a
// binding (the player and `from`) the login keeps. The player is the host when
// the login asks (with a local client) or when it is the only binding. Announces the player's name.
void ServerSession::OnJoin(Connection* from, LoginMessage* msg) {
    auto* player = new PlayerMessage();
    players.emplace_back(player);
    PlayerMessage* record = players.back().Get();

    auto* binding = new BindingMessage();
    bindings.emplace_back(binding);
    BindingMessage* bound = bindings.back().Get();

    std::string* playerName = &record->name;
    *playerName = msg->name;
    record->state = msg->state;
    record->text = msg->text;
    record->id = msg->id;

    // The lowest player number no player has yet.
    int number = 1;
    for (;;) {
        bool used = false;
        for (auto& it : players) {
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
    record->firstLine = nextLine;
    bound->player.Assign(record);
    bound->connection.Assign(from);
    msg->binding.Assign(bound);

    ClientSession* client = g_gameClient;
    bool host;
    if (IsLive(client)) {
        host = msg->host != 0;
    } else {
        host = bindings.size() == 1;
    }
    if (host) record->host = 1;

    Announce(g_interface.MessageText(0, playerName->data()), nullptr);
}

// The client's login changed: its player record takes the state and id, and its version goes up.
void ServerSession::OnPlayerChanged(LoginMessage* message) {
    message->binding.Get()->player.Get()->state = message->state;
    message->binding.Get()->player.Get()->id = message->id;
    PlayerMessage* player = message->binding.Get()->player.Get();
    ++player->version;
}

// The client left (its login removed). With a live binding: its live player is removed from the
// players list, and when it was the host the first remaining player becomes the host (its version
// goes up); then the binding is removed from the bindings list.
void ServerSession::OnPlayerGone(LoginMessage* message) {
    auto* binding = message->binding.Get();
    if (!IsLive(binding)) return;
    auto* player = binding->player.Get();
    if (IsLive(player)) {
        std::uint8_t wasHost = player->host;
        EraseValue(players, player);
        if (wasHost) {
            if (!players.empty()) {
                auto* first = players.front().Get();
                first->host = 1;
                ++first->version;
            }
        }
    }
    EraseValue(bindings, message->binding.Get());
}

// --- Net server chat ---------------------------------------------------------------------------------

// The game server's chat and system lines: kept on lines for the clients, at most 50. See net/net_session.h and
// docs/notes/network.md.

// Appends a line (the next line number, the text, the connection of `player`'s binding,
// null for the server's own lines) and drops the oldest past 50.
void ServerSession::Announce(std::string_view text, RefObject* player) {
    auto* line = new LineMessage();
    lines.emplace_back(line);

    auto* added = lines.back().Get();
    added->text.assign(text);
    added->number = nextLine;
    ++nextLine;

    RefObject* connection = nullptr;
    if (IsLive(player)) {
        for (auto& node : bindings) {
            auto* binding = node.Get();
            if (binding->player.Get() == player) {
                connection = binding->connection.Get();
                break;
            }
        }
    }
    added->connection.Assign(connection);

    if (lines.size() > kMaxLines) lines.erase(lines.begin());
}

// Announce with text `kind` of the text table formatted with `text`.
void ServerSession::AnnounceText(int textKind, std::string_view text, RefObject* player) {
    Announce(g_interface.MessageText(textKind, text), player);
}

// --- Net server vars ---------------------------------------------------------------------------------

// The game server's script variables: named floats the script reads and writes in a
// network game, replicated to the clients as VariableMessages. See net_messages.h and docs/notes/network.md.

// The search the original inlines in each function: the first variable whose name has the same bytes as
// `name`.
std::vector<SharedRef<VariableMessage>>::iterator ServerSession::FindVariable(const std::string& variableName) {
    auto node = variables.begin();
    for (; node != variables.end(); ++node)
        if (node->Get()->name == variableName) break;
    return node;
}

std::vector<SharedRef<VariableMessage>>::iterator ServerSession::FindOrAddVariable(const std::string& variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) return node;

    auto first = variables.emplace(variables.begin(), new VariableMessage());

    first->Get()->name = variableName;
    return first;
}

// Sets the variable; a zero value removes it instead. A set bumps the version and takes the next serial.
void ServerSession::SetVariable(const std::string& newName, float value) {
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
void ServerSession::RaiseVariable(const std::string& variableName, float value) {
    auto found = FindOrAddVariable(variableName);
    VariableMessage* variable = found->Get();
    float current = variable->value;
    found->Get()->value = MaxKeepB(current, value);
    found->Get()->serial = g_variableSerial;
    ++g_variableSerial;
    ++found->Get()->version;
    if (found->Get()->value == 0.0f) {
        auto node = FindVariable(variableName);
        if (node != variables.end()) variables.erase(node);
    }
}

// Removes the variable when it exists. Unreached in the shipped game: only GSDelVar in a network game calls it.
void ServerSession::RemoveVariable(const std::string& variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) variables.erase(node);
}

// The variable's value, 0 when missing.
double ServerSession::GetVariable(const std::string& variableName) {
    auto node = FindVariable(variableName);
    if (node != variables.end()) return node->Get()->value;
    return 0.0f;
}

} // namespace net
