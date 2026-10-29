// The game client's destructor, scalar deleting destructor and its
// base layer's destructor. The members are torn down in reverse order: the lists are cleared where the original
// destroys them, between the counted pointers' releases; then the session base. See net_classes.h and
// docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"
#include "core_refcount.h"
#include "party.h"

namespace net {

// The client base's part and the session base follow (their destructors).
ClientSession::~ClientSession() {
    parties[1].Get()->CParty::~CParty();
    parties[0].Get()->CParty::~CParty();
    trade.Reset();
    tradeRecords.clear();    // here, where the original destroys it
    offers.clear();
    DestroyOwned(party);
    awards.clear();
    timers.clear();
    DestroyOwned(player);
    variables.clear();
    zones.clear();
    map.Reset();
    binding.Reset();
    info.Reset();
    fileRequests.clear();
    chunks.clear();
    files.clear();
    lines.clear();
    players.clear();
}

ClientBase::~ClientBase() {}

} // namespace net
