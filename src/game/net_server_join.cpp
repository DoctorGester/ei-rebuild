// The game server's handlers of the login message (LoginMessage, registered by the server constructor):
// a client joined (a player record and a connection binding for it), its record changed or it left. See
// net_classes.h and docs/notes/network.md.
#include "core_refcount.h"
#include "game_singletons.h"
#include "net_connection.h"
#include "net_messages.h"
#include "ui.h"
#include "world_data.h"

#include <cstring>
#include <string>

namespace net {

namespace {
// The serial the next player record gets.
std::uint32_t g_playerSerial = /*0079B60C*/ {};

// Every node of `list` holding `value` erased (the next node taken first).
template <typename List>
void EraseValue(List& list, const void* value) {
    for (auto node = list.begin(); node != list.end();) {
        auto next = std::next(node);
        if (node->Get() == value) list.erase(node);
        node = next;
    }
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
    message->binding.Get()->Player()->state = message->state;
    message->binding.Get()->Player()->id = message->id;
    PlayerMessage* player = message->binding.Get()->Player();
    ++player->version;
}

// Channel 2: the client left. With a live binding: its live player is removed from the
// players list, and when it was the host the first remaining player becomes the host (its version
// goes up); then the binding is removed from the bindings list.
void ServerOnPlayerGone(ServerSession* self, LoginMessage* message) {
    auto* binding = message->binding.Get();
    if (!IsLive(binding)) return;
    auto* player = binding->Player();
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
