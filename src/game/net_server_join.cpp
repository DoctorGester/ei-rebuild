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

#include <cstring>

namespace net {

namespace {
// The players list (+0x2590) and the bindings list (+0x25A4): end, insert(pos, value), begin (the
// counted pointers come from RefPtrFromRaw, without a reference; the list insert adds one).
__declspec(naked) void** __fastcall PlayersEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420390)
__declspec(naked) void** __fastcall PlayersInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x004223D0)
__declspec(naked) void** __fastcall PlayersBegin(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420380)
__declspec(naked) void** __fastcall BindingsEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004206E0)
__declspec(naked) void** __fastcall BindingsInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x00422BB0)
__declspec(naked) void** __fastcall BindingsEnd2(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00422BA0)
__declspec(naked) void** __fastcall BindingsBegin2(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00422B90)
__declspec(naked) void __fastcall CountNodes(int* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x00429110)
// erase(position) of the players and bindings lists, distance(first, last) (library).
__declspec(naked) void** __fastcall PlayersErase(void* /*list*/, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x004293B0)
__declspec(naked) void** __fastcall BindingsErase(void* /*list*/, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x00429490)
__declspec(naked) void __fastcall CountPlayers(std::uint32_t* /*count*/, void*, void* /*first*/, void* /*last*/) EI_ORIGINAL(0x004290D0)


void* LastObject(void* list) {
    void** sentinel = *static_cast<void***>(list);
    return static_cast<void**>(sentinel[1])[2];
}
// 0079B60C: the serial the next player record gets.
std::uint32_t& PlayerCounter() { return *reinterpret_cast<std::uint32_t*>(0x0079B60C); }

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
    void* players = &self->players_;
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, player);   // 00428040
    void* end;
    PlayersEnd(players, nullptr, &end);
    void* inserted;
    PlayersInsert(players, nullptr, &inserted, end, value);
    ref.ptr = nullptr;
    ref.Destroy();
    auto* record = static_cast<PlayerMessage*>(LastObject(players));

    auto* binding = NewMessageMemory<BindingMessage>();
    if (binding) {
        MessageConstructBase(binding);
        RefNullA(&binding->PlayerRef());
        RefNullBOriginal(&binding->ConnectionRef());
        ei::SetVtable(binding, BindingMessage::kVtableAddress);
    }
    void* bindings = &self->bindings_;
    value = RefPtrFromRaw(&ref, binding);   // 00428050
    BindingsEnd(bindings, nullptr, &end);
    BindingsInsert(bindings, nullptr, &inserted, end, value);
    ref.ptr = nullptr;
    ref.Destroy();
    auto* bound = static_cast<BindingMessage*>(LastObject(bindings));

    stlp::string* name = &record->name_;
    if (&msg->name_ != name) name->AssignFrom(msg->name_);
    record->state_ = msg->state_;
    if (&msg->text_ != &record->text_) record->text_.AssignFrom(msg->text_);
    record->id_ = msg->id_;

    // The lowest player number no player has yet.
    int number = 1;
    for (;;) {
        void* it;
        PlayersBegin(players, nullptr, &it);
        bool used = false;
        while (it != *static_cast<void**>(players)) {
            auto* other = static_cast<PlayerMessage*>(static_cast<void**>(it)[2]);
            if (other->number_ == number) {
                used = true;
                break;
            }
            it = *static_cast<void**>(it);
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
        void* last;
        void* lastValue = *BindingsEnd2(bindings, nullptr, &last);
        void* first;
        void* firstValue = *BindingsBegin2(bindings, nullptr, &first);
        CountNodes(&count, nullptr, firstValue, lastValue);
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
template <typename Erase>
void EraseValue(void* list, Erase erase, const void* value) {
    void** sentinel = *static_cast<void***>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        if (node[2] == value) {
            void* erased;
            erase(list, nullptr, &erased, node);
        }
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
        EraseValue(&self->players_, PlayersErase, player);
        if (wasHost) {
            std::uint32_t count = 0;
            void** sentinel = static_cast<void**>(self->players_);
            CountPlayers(&count, nullptr, sentinel[0], sentinel);
            if (count > 0) {
                auto* first = static_cast<PlayerMessage*>(static_cast<void**>(static_cast<void**>(self->players_)[0])[2]);
                first->host_ = 1;
                first = static_cast<PlayerMessage*>(static_cast<void**>(static_cast<void**>(self->players_)[0])[2]);
                ++first->version_;
            }
        }
    }
    EraseValue(&self->bindings_, BindingsErase, message->binding_.ptr);
}
EI_REPLACE(0x0041B0C0, ServerOnPlayerGone);

} // namespace net
