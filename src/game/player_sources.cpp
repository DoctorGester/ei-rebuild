// The player sources' slot 6 (player_source.h; A-E1): the server world's AcceptNewPlayers
// (0058DAF0) asks a source for its new connections, which the source appends to an MFC list of
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

using LinkList = mfc::CTypedPtrList<CRefObject>;

// AddTail of a counted link as both slots inline it: a new node after the tail (its element
// zeroed, so nothing is released), the link stored with one more reference.
void AppendCounted(LinkList* list, CRefObject* link) {
    LinkList::Node* node = list->NewNode(list->tail, nullptr);
    node->data = link;
    ++link->refs_;
    if (list->tail) list->tail->next = node;
    else list->head = node;
    list->tail = node;
}

} // namespace

void CWorldHandle::TakeNewImpl(void* joined) {
    if (pendingLink_) AppendCounted(static_cast<LinkList*>(joined), pendingLink_);
    CRefObject* link = pendingLink_;
    pendingLink_ = nullptr;
    if (link) link->Release();
}

namespace netlink {

void DriverSession::TakeNewImpl(void* joined) {
    auto* list = static_cast<LinkList*>(joined);
    for (DriverNode* node = holders_->next; node != holders_; node = node->next) {
        CRefObject* link = node->value.ptr;
        if (link && !(link->flags7_ & CRefCounted::kDetached)) AppendCounted(list, link);
    }
    // The holders list cleared: each reference dropped, the node pushed on the allocator's list of
    // 12-byte nodes, the sentinel linked to itself.
    for (DriverNode* node = holders_->next; node != holders_;) {
        DriverNode* current = node;
        node = node->next;
        if (CRefObject* link = current->value.ptr) link->Release();
        current->next = static_cast<DriverNode*>(stlp::SmallNodeFreeList());
        stlp::SmallNodeFreeList() = current;
    }
    holders_->next = holders_;
    holders_->prev = holders_;
}

} // namespace netlink

EI_REPLACE_METHOD(0x00478880, CWorldHandle::TakeNewImpl);
EI_REPLACE_METHOD(0x0047A230, netlink::DriverSession::TakeNewImpl);
