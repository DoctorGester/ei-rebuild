// The game server's destructor, scalar deleting destructor and its
// base layer's destructor body. The members are torn down in reverse order through their STLport destructors
// and the counted pointers' releases, then the session base. See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "core_refcount.h"

#include "lib/crt.h"

namespace net {

namespace {

// RefPtr::Destroy as the destructor inlines it (the release is not null-checked after the shutdown).
void DestroyInline(RefPtr& slot) {
    CRefObject*& object = slot.ptr;
    if (!object) return;
    if (object->refs_ != 1) {
        object->flags7_ |= CRefObject::kDetached;
        object->Shutdown();
    }
    object->Release();
}

} // namespace

// The original frees the two names first; ours last (they are members). The pending joins and the session base
// follow (their destructors).
ServerSession::~ServerSession() {
    banned_.Release();
    finishedTrades_.Release();
    trades_.Release();
    offers_.Release();
    awards_.Release();
    variables_.Release();
    queuedMaps_.Release();
    DestroyInline(map_);
    bindings_.Release();
    DestroyInline(hostInfo_);
    fileRequests_.Release();
    files_.Release();
    lines_.Release();
    players_.Release();
}

// Also reached from the constructor's unwind funclets.
ServerBase::~ServerBase() { joins_.Release(); }

} // namespace net
