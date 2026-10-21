// The game server's destructor (00417BC0), scalar deleting destructor (00417A10, slot 0) and its
// base layer's destructor body (004132F0). The members are torn down in reverse order through
// their STLport destructors and the counted pointers' releases, then the session base. See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"

namespace net {

namespace {

// RefPtr::Destroy as the destructor inlines it (the release is not null-checked after the
// shutdown).
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

// 00417BC0: the destructor body. The server's own vtable is not restored first (the original does
// not either).
void ServerSession::Destruct() {
    mapName_.~string();
    name_.~string();
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
    joins_.Release();
    SessionBase::Destruct();
}
EI_REWRITTEN(0x00417BC0, ServerSession::Destruct);

// 004132F0: the destructor body of the server's base layer (reached from its scalar deleting
// destructor 004132D0 and the constructor's unwind funclets): the pending joins, then the session
// base. The same steps end ServerSession::Destruct.
void ServerBase::DestructBase() {
    joins_.Release();
    SessionBase::Destruct();
}
EI_REWRITTEN(0x004132F0, ServerBase::DestructBase);

// 00417A10 (slot 0): the destructor body, then the memory freed when bit 0 of `flags` is set.
ServerSession* ServerSession::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00417A10, ServerSession::DeletingDestruct);

} // namespace net
