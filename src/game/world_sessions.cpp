// The worlds' player sources (0055F190-0055F7B0, A-U8; player_source.h): the base's destructor, the
// counted helper at CWorld + 0x5C (CWorldHandle) and the network session at CWorld + 0x60
// (netlink::DriverSession): their clones, shutdowns and destructors, and CWorld::OpenSession, which
// makes a session. The session's lists are STLport list<RefPtr>: drivers owning (RefPtr::Destroy),
// holders plain (Release); their range inserts are the instantiations' own (library code).
#include "world.h"

#include "core_refcount.h"
#include "core_streams.h"
#include "net_link.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/stlp.h"
#include "lib/stlp_list_refs.h"

#include <cstdint>

using netlink::DriverNode;
using netlink::DriverSession;

namespace {

// list<RefPtr>::insert(position, first, last) of the session's two lists (00560430 drivers: each
// element shared, 0047A450; 00560460 holders: each counted, 0047A480).
template <typename Policy>
void InsertRange(DriverNode* position, DriverNode* first, DriverNode* last) {
    stlp::list<RefPtr>::InsertRange<Policy>(stlp::AsRefNode(position), stlp::AsRefNode(first), stlp::AsRefNode(last));
}

// An empty list's sentinel from the node pool (the lists' constructors, inlined).
DriverNode* NewSentinel() {
    auto* node = stlp::allocate_as<DriverNode>();
    node->next = node;
    node->prev = node;
    return node;
}

// The lists' clear (005603B0 drivers, 005603F0 holders): each element destroyed / released, its node
// back to the pool, the sentinel linked to itself.
void ClearDrivers(DriverNode* sentinel) {
    for (DriverNode* node = sentinel->next; node != sentinel;) {
        DriverNode* current = node;
        node = node->next;
        current->value.Destroy();
        stlp::deallocate(current, sizeof(DriverNode));
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}
void ClearHolders(DriverNode* sentinel) {
    for (DriverNode* node = sentinel->next; node != sentinel;) {
        DriverNode* current = node;
        node = node->next;
        if (CRefObject* object = current->value.ptr) object->Release();
        stlp::deallocate(current, sizeof(DriverNode));
    }
    sentinel->next = sentinel;
    sentinel->prev = sentinel;
}

// A counted pointer's release as the helpers inline it: one reference dropped, deleted at zero.
void ReleaseCounted(CRefObject* object) {
    if (object) object->Release();
}

} // namespace

// ---- CPlayerSource ---------------------------------------------------------------------------------

CPlayerSource::~CPlayerSource() {}
EI_REWRITTEN_SPECIAL(0x0055F2E0, CPlayerSource::~CPlayerSource);
EI_REWRITTEN_SPECIAL(0x0055F2F0, CPlayerSource::~CPlayerSource);

// ---- CWorldHandle ---------------------------------------------------------------------------------

// A new helper with the same pending link (counted).
CWorldHandle* CWorldHandle::Clone() {
    auto* copy = new CWorldHandle();
    if (!copy) return nullptr;
    copy->pendingLink_ = pendingLink_;
    if (copy->pendingLink_) ++copy->pendingLink_->refs_;
    return copy;
}
EI_REWRITTEN(0x0055F1A0, CWorldHandle::Clone);

// The original destroys the helper and builds it again in place (the count and flags kept): the
// link released, none pending.
int CWorldHandle::OnShutdown() {
    ReleaseCounted(pendingLink_);
    pendingLink_ = nullptr;
    return 0;
}
EI_REWRITTEN(0x0055F1E0, CWorldHandle::OnShutdown);

CWorldHandle::~CWorldHandle() { ReleaseCounted(pendingLink_); }
EI_REWRITTEN_SPECIAL(0x0055F280, CWorldHandle::~CWorldHandle);
EI_REWRITTEN_SPECIAL(0x0055F260, CWorldHandle::~CWorldHandle);

// ---- CClientHandle --------------------------------------------------------------------------------

CClientHandle* CClientHandle::Clone() { return new CClientHandle(); }
EI_REWRITTEN(0x005626B0, CClientHandle::Clone);
EI_REWRITTEN(0x005626D0, CClientHandle::OnShutdown);

void CClientHandle::TakeNew(JoinedLinks* links) {
    for (auto* node = links->head; node; node = node->next) ReleaseCounted(node->data);
    links->RemoveAll();
}
EI_REWRITTEN(0x00562700, CClientHandle::TakeNew);

// ---- netlink::DriverSession -----------------------------------------------------------------------

namespace netlink {

DriverSession::DriverSession() {
    drivers_ = NewSentinel();
    holders_ = NewSentinel();
}

// A copy: every driver shared (a copy-on-share driver is cloned), every holder counted.
DriverSession* DriverSession::Clone() {
    auto* copy = new DriverSession();
    if (!copy) return nullptr;
    InsertRange<stlp::SharedRef>(copy->drivers_->next, drivers_->next, drivers_);
    InsertRange<stlp::CountedRef>(copy->holders_->next, holders_->next, holders_);
    return copy;
}
EI_REWRITTEN(0x0055F550, DriverSession::Clone);

// The original destroys the session and builds it again in place (the count and flags kept): both
// lists emptied and their sentinels freed, then new sentinels.
int DriverSession::OnShutdown() {
    ClearHolders(holders_);
    stlp::deallocate(holders_, sizeof(DriverNode));
    ClearDrivers(drivers_);
    stlp::deallocate(drivers_, sizeof(DriverNode));
    drivers_ = NewSentinel();
    holders_ = NewSentinel();
    return 0;
}
EI_REWRITTEN(0x0055F6B0, DriverSession::OnShutdown);

DriverSession::~DriverSession() {
    ClearHolders(holders_);
    stlp::deallocate(holders_, sizeof(DriverNode));
    ClearDrivers(drivers_);
    stlp::deallocate(drivers_, sizeof(DriverNode));
}
EI_REWRITTEN_SPECIAL(0x0055F7B0, DriverSession::~DriverSession);
EI_REWRITTEN_SPECIAL(0x0055F790, DriverSession::~DriverSession);

} // namespace netlink

// ---- CWorld::OpenSession (0055F410) ---------------------------------------------------------------

void CWorld::OpenSession() { netSession_.ShareAssign(new DriverSession()); }
EI_REWRITTEN(0x0055F410, CWorld::OpenSession);

// ---- CWorld::CloseSession (0055F890) --------------------------------------------------------------

void CWorld::CloseSession() {
    RefPtr& session = netSession_;
    if (!session.ptr) return;
    session.Destroy();
    session.ptr = nullptr;
}
EI_REWRITTEN(0x0055F890, CWorld::CloseSession);
