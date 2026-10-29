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
    parties_[1].Get()->CParty::~CParty();
    parties_[0].Get()->CParty::~CParty();
    trade_.Reset();
    tradeRecords_.clear();   // here, where the original destroys it
    offers_.clear();
    DestroyOwned(party_);
    awards_.clear();
    timers_.clear();
    DestroyOwned(player_);
    variables_.clear();
    zones_.clear();
    map_.Reset();
    binding_.Reset();
    info_.Reset();
    fileRequests_.clear();
    chunks_.clear();
    files_.clear();
    lines_.clear();
    players_.clear();
}

ClientBase::~ClientBase() {}

} // namespace net
