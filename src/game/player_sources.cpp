// The player sources' accept virtual: the server world's AcceptNewPlayers
// asks a source for its new connections, which the source appends to a list of
// counted links. The first is the counted helper's (CWorldHandle: its pending link, handed over
// once), the second the network session's (netlink::DriverSession: the link of every live holder,
// then the holders list is emptied). Main thread (the world's update). See docs/notes/network.md.
#include "world.h"

#include "core_refcount.h"
#include "net_link.h"

void CWorldHandle::TakeNew(std::list<HeldRef<netlink::NetLink>>* joined) {
    if (pendingLink_) joined->emplace_back(pendingLink_.Get());
    pendingLink_.Reset();
}

namespace netlink {

void DriverSession::TakeNew(std::list<HeldRef<netlink::NetLink>>* list) {
    for (const auto& holder : holders_) {
        DriverLink* link = holder.Get();
        if (link && !(link->flags7_ & CRefCounted::kDetached)) list->emplace_back(link);
    }
    // Each reference dropped.
    holders_.clear();
}

} // namespace netlink
