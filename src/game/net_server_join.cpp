// The game server's handlers of the login message (LoginMessage, registered by the server constructor):
// a client joined (a player record and a connection binding for it), its record changed or it left. See
// net_classes.h and docs/notes/network.md.
#include "net_messages.h"
#include "net_connection.h"
#include "core_refcount.h"

#include "ui.h"
#include "world_data.h"
#include "game_singletons.h"

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
        if (node->ptr == value) list.erase(node);
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
    self->players_.emplace_back(player);
    PlayerMessage* record = self->players_.back().Get();

    auto* binding = NewCounted<BindingMessage>();
    self->bindings_.emplace_back(binding);
    BindingMessage* bound = self->bindings_.back().Get();

    std::string* name = &record->name_;
    *name = msg->name_;
    record->state_ = msg->state_;
    record->text_ = msg->text_;
    record->id_ = msg->id_;

    // The lowest player number no player has yet.
    int number = 1;
    for (;;) {
        bool used = false;
        for (auto& it : self->players_) {
            if (it.Get()->number_ == number) {
                used = true;
                break;
            }
        }
        if (!used) break;
        ++number;
    }
    record->number_ = number;
    record->serial_ = static_cast<std::int32_t>(g_playerSerial);
    ++g_playerSerial;
    record->firstLine_ = self->nextLine_;
    bound->PlayerRef().Assign(record);
    bound->ConnectionRef().Assign(g_currentConnection);
    msg->binding_.Assign(bound);

    ClientSession* client = g_gameClient;
    bool host;
    if (client && !(client->flags7_ & CRefObject::kDetached)) {
        host = msg->host_ != 0;
    } else {
        host = self->bindings_.size() == 1;
    }
    if (host) record->host_ = 1;

    std::string announcement;
    std::string* formatted = g_interface->MessageText(&announcement, 0, name->data());
    self->Announce(*formatted, nullptr);
}

// The player record of a login, through its binding.
PlayerMessage* PlayerOf(LoginMessage* login) {
    return login->binding_.Get()->Player();
}

// Channel 1: the client's login changed: its player record takes the state and id, and its version goes up.
void ServerOnPlayerChanged(ServerSession*, LoginMessage* message) {
    PlayerOf(message)->state_ = message->state_;
    PlayerOf(message)->id_ = message->id_;
    PlayerMessage* player = PlayerOf(message);
    ++player->version_;
}

// Channel 2: the client left. With a live binding: its live player is removed from the
// players list, and when it was the host the first remaining player becomes the host (its version
// goes up); then the binding is removed from the bindings list.
void ServerOnPlayerGone(ServerSession* self, LoginMessage* message) {
    auto* binding = message->binding_.Get();
    if (!IsLive(binding)) return;
    auto* player = binding->Player();
    if (IsLive(player)) {
        std::uint8_t wasHost = player->host_;
        EraseValue(self->players_, player);
        if (wasHost) {
            if (!self->players_.empty()) {
                auto* first = self->players_.front().Get();
                first->host_ = 1;
                ++first->version_;
            }
        }
    }
    EraseValue(self->bindings_, message->binding_.ptr);
}

} // namespace net
