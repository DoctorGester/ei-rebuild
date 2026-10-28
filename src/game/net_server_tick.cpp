// The game server's tick and a player state query (called by the file chunk sender). Float maths in plain C++
// (rewrite-guide rule 7). See net_classes.h and docs/notes/network.md.
#include "ei/x87.h"
#include "net_classes.h"
#include "net_messages.h"
#include "net_connection.h"
#include "options.h"
#include "game_strings.h"
#include "core_refcount.h"

#include "world_data.h"
#include "lib/nan_audit.h"

#include <cstring>

namespace net {

namespace {

const char* MasterHost() { return options::g_options->masterServerName_.c_str(); }   // the master server's host name

// Timed entries {seconds left (float), object}: each loses `dt`; those below zero are
// erased. The end is read again after every entry.
template <typename List>
void CountDown(List& list, float dt) {
    auto node = list.begin();
    while (node != list.end()) {
        auto current = node;
        ++node;
        float& seconds = current->seconds;
        float left = seconds - dt;
        seconds = left;
        EI_NAN_AUDIT(left);
        if (left < 0.0f) list.erase(current);
    }
}

} // namespace

// The base layer's tick, then the timed entries of the finished trades and the awards count down by `dt` (as
// a float), and when the game is public the announcement timer does too: when it runs out it is reset and,
// with a master server host set, the game is announced. A timer that lands within rounding of zero may expire
// one tick apart from the original (x87 versus plain float maths).
void ServerSession::Tick(double dt) {
    ServerBase::Tick(dt);
    float step = static_cast<float>(dt);
    CountDown(finishedTrades_, step);
    CountDown(awards_, step);
    if (!public_) return;
    announceTimer_ = static_cast<float>(announceTimer_ - dt);
    EI_NAN_AUDIT(announceTimer_);
    if (announceTimer_ < 0.0f) {   // run out
        x87::CopyBits(announceTimer_, worlddata::AnnouncePeriodRef());
        const char* host = MasterHost();
        if (std::strcmp(host, game::kEmptyText) != 0) AnnounceToMaster();
    }
}

// Whether the player bound to `connection` (its first binding) is in state 3.
// False for a dead connection, binding or player.
bool ServerSession::PlayerStateIs3(const Connection* connection) {
    if (!IsLive(connection)) return false;
    for (auto node = bindings_.begin(); node != bindings_.end(); ++node) {
        auto* binding = node->Get();
        if (binding->ConnectionRef().ptr != connection) continue;
        if (!IsLive(binding)) return false;
        auto* player = binding->Player();
        if (!IsLive(player)) return false;
        return player->state_ == 3;
    }
    return false;
}

} // namespace net
