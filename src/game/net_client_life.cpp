// The game client's destructor (0041CA10), scalar deleting destructor (0041C8B0, slot 0) and its
// base layer's destructor body (0041C530). The members are torn down in reverse order through
// their STLport destructors and the counted pointers' releases, then the session base. See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "core_refcount.h"
#include "record_lists.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_library.h"
#include "originals_gameplay.h"
#include "record_vectors.h"

namespace net {

namespace {

// The two parties (00661050 class, outside this range).
void __fastcall PartyDestruct(void* a0) { PartyRecordsDestructOriginal(a0); }

// The release a counted pointer's destructor inlines: one reference less, deleted at zero.
void ReleaseInline(void* slot) {
    auto* object = static_cast<CRefObject*>(slot);
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) delete object;
}

} // namespace

// 0041CA10: the destructor body. The client's own vtable is not restored first (the original does
// not either).
void ClientSession::Destruct() {
    PartyDestruct(&parties_[1]);
    PartyDestruct(&parties_[0]);
    if (trade_) static_cast<CRefObject*>(trade_)->Release();
    tradeRecords_.Release<recordlist::TradeElement>();
    offers_.Release();
    party_.Destroy();
    awards_.Release();
    timers_.Release();
    if (auto* local = player_.ptr) {
        if (local->refs_ != 1) {
            local->flags7_ |= CRefObject::kDetached;
            local->Shutdown();
        }
        player_.ptr->Release();
    }
    variables_.Release();
    zones_.Release();
    ReleaseInline(map_);
    ReleaseInline(binding_);
    ReleaseInline(info_);
    fileRequests_.Release();
    chunks_.Release();
    files_.Release();
    lines_.Release();
    players_.Release();
    waiting_.Release();
    recordvec::QueryVector::Destruct(&queries_.list);   // 0041F7F0
    servers_.Release();
    GrowingBufferDestruct(&joinData_);
    SessionBase::Destruct();
}
EI_REPLACE_METHOD(0x0041CA10, ClientSession::Destruct);

// 0041C530: the destructor body of the client's base layer (vtable 0073BE30; reached from its
// scalar deleting destructor 0041C450 and the constructor's unwind funclets): the list +0x25BC,
// the vector of buffers +0x25AC, the list +0x25A8, the buffer +0x2578, then the session base. The
// original inlines the three container destructors (with the same element destructors and node
// frees as the out-of-line ones called here, which ClientSession::Destruct uses too).
void ClientBase::DestructBase() {
    waiting_.Release();
    recordvec::QueryVector::Destruct(&queries_.list);   // 0041F7F0
    servers_.Release();
    GrowingBufferDestruct(&joinData_);
    SessionBase::Destruct();
}
EI_REPLACE_METHOD(0x0041C530, ClientBase::DestructBase);

// 0041C8B0 (slot 0): the destructor body, then the memory freed when bit 0 of `flags` is set.
ClientSession* ClientSession::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x0041C8B0, ClientSession::DeletingDestruct);

} // namespace net
