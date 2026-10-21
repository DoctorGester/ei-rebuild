// The game server's tick (004182E0, slot 14) and a player state query (00417E40, called by the
// file chunk sender at 00479EF4). Float maths in plain C++ (rewrite-guide rule 7). See
// net_classes.h and docs/notes/network.md.
#include "net_messages.h"
#include "game_data.h"
#include "game_strings.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "world_data.h"
#include "lib/nan_audit.h"

#include <cstring>

namespace net {

namespace {

// 0041AA10: announces the game to the master server (patched by Rebirth at a call site; called
// through its address).
__declspec(naked) void __fastcall ServerAnnounceToMaster(ServerSession* /*self*/) EI_ORIGINAL(0x0041AA10)

const char* MasterHost() { return ei::GameData<const char*>(0x0079BED8); }   // the master server's host name

bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}

// Timed entries {next, prev, seconds left (float), ...}: each loses `dt`; those below zero are
// erased. The end is read again after every entry, as the original does.
template <typename List>
void CountDown(List& list, float dt) {
    TimedNode* node = list.First();
    while (node != list.End()) {
        TimedNode* current = node;
        node = node->next;
        float& seconds = current->data.seconds;
        float left = seconds - dt;
        seconds = left;
        EI_NAN_AUDIT(left);
        if (left < worlddata::Zero()) list.Erase(current);
    }
}

} // namespace

// 004182E0 (slot 14): the base layer's tick, then the timed entries of the finished trades and the
// awards count down by `dt` (as a float), and when the game is public the announcement timer does
// too: when it runs out it is reset and, with a master server host set, the game is announced.
// A timer that lands within rounding of zero may expire one tick apart from the original (x87
// versus plain float maths).
void ServerSession::TickImpl(double dt) {
    ServerBase::TickImpl(dt);   // 00443590
    float step = static_cast<float>(dt);
    CountDown(finishedTrades_, step);
    CountDown(awards_, step);
    if (!public_) return;
    announceTimer_ = static_cast<float>(announceTimer_ - dt);
    EI_NAN_AUDIT(announceTimer_);
    if (announceTimer_ < worlddata::Zero()) {   // run out
        std::memcpy(&announceTimer_, &worlddata::AnnouncePeriodRef(), 4);
        const char* host = MasterHost();
        if (gamecrt::mbscmp(host, game::EmptyText()) != 0) ServerAnnounceToMaster(this);
    }
}
EI_REPLACE_METHOD(0x004182E0, ServerSession::TickImpl);

// 00417E40: whether the player bound to `connection` (its first binding) is in state 3.
// False for a dead connection, binding or player.
bool ServerSession::PlayerStateIs3(void* connection) {
    if (!Live(connection)) return false;
    for (RefNode* node = bindings_.First(); node != bindings_.End(); node = node->next) {
        auto* binding = static_cast<BindingMessage*>(node->data.ptr);
        if (binding->ConnectionRef().ptr != connection) continue;
        if (!Live(binding)) return false;
        auto* player = static_cast<PlayerMessage*>(binding->PlayerRef().ptr);
        if (!Live(player)) return false;
        return player->state_ == 3;
    }
    return false;
}
EI_REPLACE_METHOD(0x00417E40, ServerSession::PlayerStateIs3);

} // namespace net
