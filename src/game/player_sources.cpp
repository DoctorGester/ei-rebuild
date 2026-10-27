// The player sources' slot 6 (player_source.h; A-E1): the server world's AcceptNewPlayers
// asks a source for its new connections, which the source appends to an MFC list of
// counted links. 00478880 is the counted helper's (CWorldHandle: its pending link, handed over
// once), 0047A230 the network session's (netlink::DriverSession: the link of every live holder,
// then the holders list is emptied). Main thread (the world's update). See docs/notes/network.md.
#include "world.h"

#include "core_refcount.h"
#include "net_link.h"

#include "ei/hook.h"
#include "lib/mfc.h"
#include "lib/stlp.h"

namespace {

// AddTail of a counted link as both slots inline it: a new node after the tail (its element
// zeroed, so nothing is released), the link stored with one more reference.
void AppendCounted(JoinedLinks* list, netlink::NetLink* link) {
    list->AddTail(link);
    ++link->refs_;
}

} // namespace

void CWorldHandle::TakeNew(JoinedLinks* joined) {
    if (pendingLink_) AppendCounted(joined, pendingLink_);
    netlink::NetLink* link = pendingLink_;
    pendingLink_ = nullptr;
    if (link) link->Release();
}

namespace netlink {

void DriverSession::TakeNew(JoinedLinks* list) {
    for (DriverNode* node = holders_->next; node != holders_; node = node->next) {
        DriverLink* link = node->Driver();
        if (link && !(link->flags7_ & CRefCounted::kDetached)) AppendCounted(list, link);
    }
    // The holders list cleared: each reference dropped, the node pushed on the allocator's list of
    // 12-byte nodes, the sentinel linked to itself.
    for (DriverNode* node = holders_->next; node != holders_;) {
        DriverNode* current = node;
        node = node->next;
        if (CRefObject* link = current->value.ptr) link->Release();
        stlp::PushSmallNode(current);
    }
    holders_->next = holders_;
    holders_->prev = holders_;
}

} // namespace netlink

EI_REWRITTEN(0x00478880, CWorldHandle::TakeNew);
EI_REWRITTEN(0x0047A230, netlink::DriverSession::TakeNew);
