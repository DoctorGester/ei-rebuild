// The worlds' player sources (player_source.h): the base's destructor, CWorld::worldHandle_ (CWorldHandle) and
// netSession_ (netlink::DriverSession): their clones, shutdowns and destructors, and CWorld::OpenSession. The
// session's lists: drivers owning (RefPtr::Destroy), holders plain (Release).
#include "core_refcount.h"
#include "core_streams.h"
#include "net_link.h"
#include "world.h"

#include <cstdint>

using netlink::DriverSession;

namespace {

// A counted pointer's release as the helpers inline it: one reference dropped, deleted at zero.
void ReleaseCounted(CRefObject* object) {
    if (object) object->Release();
}

} // namespace

CPlayerSource::~CPlayerSource() {}

// A new helper with the same pending link (counted).
CWorldHandle* CWorldHandle::Clone() {
    auto* copy = new CWorldHandle();
    copy->pendingLink_ = pendingLink_;
    if (copy->pendingLink_) copy->pendingLink_->AddRef();
    return copy;
}

// The helper is destroyed and built again in place (the count and flags kept): the link released, none pending.
int CWorldHandle::OnShutdown() {
    ReleaseCounted(pendingLink_);
    pendingLink_ = nullptr;
    return 0;
}

CWorldHandle::~CWorldHandle() { ReleaseCounted(pendingLink_); }

CClientHandle* CClientHandle::Clone() { return new CClientHandle(); }

void CClientHandle::TakeNew(std::list<netlink::NetLink*>* links) {
    for (auto node = links->begin(); node != links->end(); ++node) ReleaseCounted(*node);
    links->clear();
}

// ---- netlink::DriverSession -----------------------------------------------------------------------

namespace netlink {

DriverSession::DriverSession() {}

// A copy: every driver shared (a copy-on-share driver is cloned), every holder counted.
DriverSession* DriverSession::Clone() {
    auto* copy = new DriverSession();
    copy->drivers_.insert(copy->drivers_.begin(), drivers_.begin(), drivers_.end());
    copy->holders_.insert(copy->holders_.begin(), holders_.begin(), holders_.end());
    return copy;
}

// The session is destroyed and built again in place (the count and flags kept): both lists emptied, the
// holders first.
int DriverSession::OnShutdown() {
    holders_.clear();
    drivers_.clear();
    return 0;
}

// The holders are released before the drivers (members: the reverse order).
DriverSession::~DriverSession() {}

} // namespace netlink

void CWorld::OpenSession() { netSession_.ShareAssign(new DriverSession()); }

void CWorld::CloseSession() {
    RefPtr& session = netSession_;
    if (!session.ptr) return;
    session.Destroy();
    session.ptr = nullptr;
}
