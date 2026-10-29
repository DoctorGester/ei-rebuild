// The worlds' player sources (player_source.h): the base's destructor, CWorld::worldHandle (CWorldHandle) and
// netSession (netlink::DriverSession): their clones, shutdowns and destructors, and CWorld::OpenSession. The
// session's lists: drivers owning (SharedRef: DestroyOwned), holders plain (HeldRef: Release).
#include "core_refcount.h"
#include "core_streams.h"
#include "net_link.h"
#include "world.h"

#include <cstdint>

using netlink::DriverSession;

CPlayerSource::~CPlayerSource() {}

// A new helper with the same pending link (counted).
CWorldHandle* CWorldHandle::Clone() {
    auto* copy = new CWorldHandle();
    copy->pendingLink.Assign(pendingLink);
    return copy;
}

// The helper is destroyed and built again in place (the count and flags kept): the link released, none pending.
void CWorldHandle::OnShutdown() {
    pendingLink.Reset();
}

CWorldHandle::~CWorldHandle() { pendingLink.Reset(); }

CClientHandle* CClientHandle::Clone() { return new CClientHandle(); }

void CClientHandle::TakeNew(std::list<HeldRef<netlink::NetLink>>* links) { links->clear(); }

// ---- netlink::DriverSession -----------------------------------------------------------------------

namespace netlink {

DriverSession::DriverSession() {}

// A copy: every driver shared (a copy-on-share driver is cloned), every holder counted.
DriverSession* DriverSession::Clone() {
    auto* copy = new DriverSession();
    copy->drivers.insert(copy->drivers.begin(), drivers.begin(), drivers.end());
    copy->holders.insert(copy->holders.begin(), holders.begin(), holders.end());
    return copy;
}

// The session is destroyed and built again in place (the count and flags kept): both lists emptied, the
// holders first.
void DriverSession::OnShutdown() {
    holders.clear();
    drivers.clear();
}

// The holders are released before the drivers (members: the reverse order).
DriverSession::~DriverSession() {}

} // namespace netlink

void CWorld::OpenSession() { ShareAssignOwned(netSession, new DriverSession()); }

void CWorld::CloseSession() { DestroyOwned(netSession); }
