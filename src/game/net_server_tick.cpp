// The game server's tick and a player state query (called by the file chunk sender). Float maths in plain C++
// (rewrite-guide rule 7). See net_classes.h and docs/notes/network.md.
#include "core_refcount.h"
#include "game_strings.h"
#include "net_classes.h"
#include "net_connection.h"
#include "net_messages.h"
#include "options.h"
#include "world_data.h"

#include "lib/nan_audit.h"


#include <cstring>

namespace net {

namespace {

const char* MasterHost() { return options::g_options.masterServerName.c_str(); }    // the master server's host name

// Timed entries {seconds left (float), object}: each loses `dt`; those below zero are erased.
template <typename List>
void CountDown(List& list, float dt) {
    for (auto node = list.begin(); node != list.end();) {
        float& seconds = node->seconds;
        float left = seconds - dt;
        seconds = left;
        EI_NAN_AUDIT(left);
        if (left < 0.0f) {
            node = list.erase(node);
        } else {
            ++node;
        }
    }
}

} // namespace

// The base layer's tick, then the timed entries of the finished trades and the awards count down by `dt` (as
// a float), and when the game is public the announcement timer does too: when it runs out it is reset and,
// with a master server host set, the game is announced. `dt` holds a float value (net::Pump), so the float
// subtractions are the original's at 24-bit precision.
void ServerSession::Tick(double dt) {
    ServerBase::Tick(dt);
    float step = static_cast<float>(dt);
    CountDown(finishedTrades, step);
    CountDown(awards, step);
    if (!announced) return;
    announceTimer -= step;
    EI_NAN_AUDIT(announceTimer);
    if (announceTimer < 0.0f) {    // run out
        announceTimer = worlddata::AnnouncePeriodRef();
        const char* host = MasterHost();
        if (std::strcmp(host, game::kEmptyText) != 0) AnnounceToMaster();
    }
}

// Whether the player bound to `connection` (its first binding) is in state 3.
// False for a dead connection, binding or player.
bool ServerSession::PlayerStateIs3(const Connection* connection) {
    if (!IsLive(connection)) return false;
    for (auto& node : bindings) {
        auto* binding = node.Get();
        if (binding->ConnectionRef().Get() != connection) continue;
        if (!IsLive(binding)) return false;
        auto* player = binding->PlayerInfo();
        if (!IsLive(player)) return false;
        return player->state == 3;
    }
    return false;
}

} // namespace net
