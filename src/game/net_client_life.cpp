// The game client's destructor, scalar deleting destructor and its
// base layer's destructor body. The members are torn down in reverse order through
// their STLport destructors and the counted pointers' releases, then the session base. See
// net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"
#include "core_refcount.h"
#include "party.h"
#include "record_lists.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "record_vectors.h"

namespace net {

namespace {

// The release a counted pointer's destructor inlines: one reference less, deleted at zero.
void ReleaseInline(void* slot) {
    auto* object = static_cast<CRefObject*>(slot);
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) delete object;
}

} // namespace

// The client base's part and the session base follow (their destructors).
ClientSession::~ClientSession() {
    parties_[1].Get()->CParty::~CParty();
    parties_[0].Get()->CParty::~CParty();
    if (trade_) trade_->Release();
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
}
EI_REWRITTEN_SPECIAL(0x0041CA10, ClientSession::~ClientSession);
EI_REWRITTEN_SPECIAL(0x0041C8B0, ClientSession::~ClientSession);

// The destructor body of the client's base layer (vtable 0073BE30; reached from its
// scalar deleting destructor 0041C450 and the constructor's unwind funclets): the list +0x25BC,
// the vector of buffers +0x25AC, the list +0x25A8, the buffer +0x2578, then the session base. The
// original inlines the three container destructors (with the same element destructors and node
// frees as the out-of-line ones called here, which ClientSession::Destruct uses too).
ClientBase::~ClientBase() {
    waiting_.Release();
    recordvec::QueryVector::Destruct(&queries_.list);
    servers_.Release();
}
EI_REWRITTEN_SPECIAL(0x0041C530, ClientBase::~ClientBase);
EI_REWRITTEN_SPECIAL(0x0041C450, ClientBase::~ClientBase);

} // namespace net
