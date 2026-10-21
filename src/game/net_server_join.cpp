// The game server's handlers of the login message (0073BBF8, registered by the server constructor):
// a client joined (0041ADB0: a player record and a connection binding for it), its record changed
// (0041AD80) or it left (0041B0C0).
// See net_classes.h and docs/notes/network.md.
#include "net_messages.h"
#include "net_connection.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "ui.h"
#include "world_data.h"
#include "game_data.h"

#include <cstring>

namespace net {

namespace {
// The record just appended to a list of counted pointers.
template <typename List>
CRefObject* LastObject(const List& list) { return list.End()->prev->data.ptr; }
// 0079B60C: the serial the next player record gets.
std::uint32_t& PlayerCounter() { return ei::GameData<std::uint32_t>(0x0079B60C); }

} // namespace

// 0041ADB0: a client joined (a LoginMessage). Appends a player record (the login's texts and
// values, the lowest free player number, the next serial, the server's next line number) and a
// binding (the player and the current connection) the login keeps. The player is the host when
// the login asks (with a local client) or when it is the only binding. Announces the player's name.
void __fastcall ServerOnJoin(ServerSession* self, LoginMessage* msg) {
    auto* player = NewMessageMemory<PlayerMessage>();
    if (player) {
        MessageConstructBase(player);
        stlp::StringElement::Default(&player->name_);
        stlp::StringElement::Default(&player->text_);
        ei::SetVtable(player, PlayerMessage::kVtableAddress);
        player->host_ = 0;
        player->state_ = 1;
        player->id_ = -1;
        player->number_ = -1;
        player->serial_ = -1;
    }
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, player);   // 00428040
    self->players_.Insert(self->players_.End(), *value);
    ref.ptr = nullptr;
    ref.Destroy();
    auto* record = static_cast<PlayerMessage*>(LastObject(self->players_));

    auto* binding = NewMessageMemory<BindingMessage>();
    if (binding) {
        MessageConstructBase(binding);
        binding->PlayerRef().ptr = nullptr;   // RefPtr() (0041FF80)
        binding->ConnectionRef().ptr = nullptr;   // RefPtr() (0041FF70)
        ei::SetVtable(binding, BindingMessage::kVtableAddress);
    }
    value = RefPtrFromRaw(&ref, binding);   // 00428050
    self->bindings_.Insert(self->bindings_.End(), *value);
    ref.ptr = nullptr;
    ref.Destroy();
    auto* bound = static_cast<BindingMessage*>(LastObject(self->bindings_));

    stlp::string* name = &record->name_;
    if (&msg->name_ != name) name->AssignFrom(msg->name_);
    record->state_ = msg->state_;
    if (&msg->text_ != &record->text_) record->text_.AssignFrom(msg->text_);
    record->id_ = msg->id_;

    // The lowest player number no player has yet.
    int number = 1;
    for (;;) {
        bool used = false;
        for (RefNode* it = self->players_.First(); it != self->players_.End(); it = it->next) {
            if (static_cast<PlayerMessage*>(it->data.ptr)->number_ == number) {
                used = true;
                break;
            }
        }
        if (!used) break;
        ++number;
    }
    record->number_ = number;
    record->serial_ = static_cast<std::int32_t>(PlayerCounter());
    ++PlayerCounter();
    record->firstLine_ = self->nextLine_;
    bound->PlayerRef().Assign(record);
    bound->ConnectionRef().Assign(CurrentConnection());
    msg->binding_.Assign(bound);

    ClientSession* client = GameClient();
    bool host;
    if (client && !(client->flags7_ & CRefObject::kDetached)) {
        host = msg->host_ != 0;
    } else {
        int count = 0;
        stlp::SharedRefList::Distance(self->bindings_.First(), self->bindings_.End(), count);
        host = count == 1;
    }
    if (host) record->host_ = 1;

    std::uint32_t announcement = 0;   // CString
    void* formatted = worlddata::InterfaceManager()->MessageText(&announcement, 0, name->data());
    self->Announce(*static_cast<const char**>(formatted), nullptr);
    CStringDestroy(&announcement);
}
EI_REPLACE(0x0041ADB0, ServerOnJoin);

namespace {
bool Live(const CRefObject* object) { return object && !(object->flags7_ & CRefObject::kDetached); }
// Every node of `list` holding `value` erased (the next node taken first).
void EraseValue(stlp::SharedRefList& list, const void* value) {
    RefNode* sentinel = list.End();
    for (RefNode* node = sentinel->next; node != sentinel;) {
        RefNode* next = node->next;
        if (node->data.ptr == value) list.Erase(node);
        node = next;
    }
}
} // namespace

// The player record of a login, through its binding.
PlayerMessage* PlayerOf(LoginMessage* login) {
    return static_cast<PlayerMessage*>(static_cast<BindingMessage*>(login->binding_.ptr)->PlayerRef().ptr);
}

// 0041AD80 (channel 1): the client's login changed: its player record takes the state and id,
// and its version goes up.
void __fastcall ServerOnPlayerChanged(ServerSession*, LoginMessage* message) {
    PlayerOf(message)->state_ = message->state_;
    PlayerOf(message)->id_ = message->id_;
    PlayerMessage* player = PlayerOf(message);
    ++player->version_;
}
EI_REPLACE(0x0041AD80, ServerOnPlayerChanged);

// 0041B0C0 (channel 2): the client left. With a live binding: its live player is removed from the
// players list, and when it was the host the first remaining player becomes the host (its version
// goes up); then the binding is removed from the bindings list.
void __fastcall ServerOnPlayerGone(ServerSession* self, LoginMessage* message) {
    auto* binding = static_cast<BindingMessage*>(message->binding_.ptr);
    if (!Live(binding)) return;
    auto* player = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
    if (Live(player)) {
        std::uint8_t wasHost = player->host_;
        EraseValue(self->players_, player);
        if (wasHost) {
            int count = 0;
            stlp::SharedRefList::Distance(self->players_.First(), self->players_.End(), count);
            if (count != 0) {
                auto* first = static_cast<PlayerMessage*>(self->players_.First()->data.ptr);
                first->host_ = 1;
                ++first->version_;
            }
        }
    }
    EraseValue(self->bindings_, message->binding_.ptr);
}
EI_REPLACE(0x0041B0C0, ServerOnPlayerGone);

} // namespace net
