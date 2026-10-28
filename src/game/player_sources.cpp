// The player sources' accept virtual: the server world's AcceptNewPlayers
// asks a source for its new connections, which the source appends to a list of
// counted links. The first is the counted helper's (CWorldHandle: its pending link, handed over
// once), the second the network session's (netlink::DriverSession: the link of every live holder,
// then the holders list is emptied). Main thread (the world's update). See docs/notes/network.md.
#include "world.h"

#include "core_refcount.h"
#include "net_link.h"

namespace {

// AddTail of a counted link as both slots inline it: a new node after the tail (its element
// zeroed, so nothing is released), the link stored with one more reference.
void AppendCounted(std::list<netlink::NetLink*>* list, netlink::NetLink* link) {
    list->push_back(link);
    ++link->refs_;
}

} // namespace

void CWorldHandle::TakeNew(std::list<netlink::NetLink*>* joined) {
    if (pendingLink_) AppendCounted(joined, pendingLink_);
    netlink::NetLink* link = pendingLink_;
    pendingLink_ = nullptr;
    if (link) link->Release();
}

namespace netlink {

void DriverSession::TakeNew(std::list<netlink::NetLink*>* list) {
    for (const auto& holder : holders_) {
        DriverLink* link = holder.Get();
        if (link && !(link->flags7_ & CRefCounted::kDetached)) AppendCounted(list, link);
    }
    // Each reference dropped.
    holders_.clear();
}

} // namespace netlink
