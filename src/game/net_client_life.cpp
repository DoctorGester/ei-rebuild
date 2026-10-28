// The game client's destructor, scalar deleting destructor and its
// base layer's destructor body. The members are torn down in reverse order through their STLport destructors
// and the counted pointers' releases, then the session base. See net_classes.h and docs/notes/network.md.
#include "net_classes.h"
#include "net_coder.h"
#include "net_messages.h"
#include "core_refcount.h"
#include "party.h"
#include "record_lists.h"

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

ClientBase::~ClientBase() {
    recordvec::QueryVector::Destruct(&queries_.list);
    servers_.Release();
}

} // namespace net
