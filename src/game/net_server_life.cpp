// The game server's destructor, scalar deleting destructor and its
// base layer's destructor body. The members are torn down in reverse order: the lists are cleared where the
// original destroys them, between the counted pointers' releases; then the session base. See net_classes.h and
// docs/notes/network.md.
#include "net_classes.h"
#include "core_refcount.h"

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
    finishedTrades_.clear();
    trades_.clear();
    offers_.clear();
    awards_.clear();
    variables_.clear();
    queuedMaps_.clear();
    DestroyInline(map_);
    bindings_.clear();
    DestroyInline(hostInfo_);
    fileRequests_.clear();
    files_.clear();
    lines_.clear();
    players_.clear();
}

// Also reached from the constructor's unwind funclets.
ServerBase::~ServerBase() {  }

} // namespace net
