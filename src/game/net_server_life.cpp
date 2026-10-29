// The game server's destructor, scalar deleting destructor and its
// base layer's destructor body. The members are torn down in reverse order: the lists are cleared where the
// original destroys them, between the counted pointers' releases; then the session base. See net_classes.h and
// docs/notes/network.md.
#include "net_classes.h"
#include "net_messages.h"
#include "core_refcount.h"

namespace net {

// The original frees the two names first; ours last (they are members). The pending joins and the session base
// follow (their destructors).
ServerSession::~ServerSession() {
    finishedTrades_.clear();
    trades_.clear();
    offers_.clear();
    awards_.clear();
    variables_.clear();
    queuedMaps_.clear();
    DestroyOwned(map_);
    bindings_.clear();
    DestroyOwned(hostInfo_);
    fileRequests_.clear();
    files_.clear();
    lines_.clear();
    players_.clear();
}

// Also reached from the constructor's unwind funclets.
ServerBase::~ServerBase() {  }

} // namespace net
