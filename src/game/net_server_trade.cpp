// The game server's trades between players: offers arrive as replicated objects (created, updated,
// removed: 0041B7A0, 0041B970, 0041BA30), two matching offers make a trade (a TradeMessage,
// 0041ACF0 finds it), and 0041B720 drops a removed file request. The offers are PartyMessages
// (net_messages.h). See docs/notes/network.md.
#include "net_messages.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_ui.h"
#include "ui.h"
#include "world_data.h"
#include "game_singletons.h"

#include <cstring>

namespace net {

namespace {


constexpr std::uint32_t kFinishedDelay = 0x42700000;   // 60.0f

bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}

// The release a counted pointer's destructor inlines: one reference less, deleted at zero.
void ReleaseInline(CRefObject* object) {
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) delete object;
}

// The offer of `offer`'s partner (its player is our partner and its partner our player), or null.
PartyMessage* FindPartnerOffer(const stlp::CountedRefList& list, PartyMessage* offer) {
    for (RefNode* node = list.First(); node != list.End(); node = node->next) {
        auto* other = static_cast<PartyMessage*>(node->data.ptr);
        if (other->player_.ptr == offer->partner_.ptr && other->partner_.ptr == offer->player_.ptr)
            return static_cast<PartyMessage*>(node->data.ptr);
    }
    return nullptr;
}

// The partner is told a trade was offered (text 10) or withdrawn (text 11), with the player's name.
void TellPartner(ServerSession* self, PartyMessage* offer, int text) {
    CRefObject* partner = offer->partner_.ptr;
    const char* name = static_cast<PlayerMessage*>(offer->player_.ptr)->name_.data();
    std::uint32_t line = 0;   // CString
    void* formatted = TheInterface()->MessageText(&line, text, name);
    self->Announce(*static_cast<const char**>(formatted), partner);
    CStringDestroy(&line);
}

} // namespace

} // namespace net

// 004263B0: a counted pointer copy (a reference added, no copy-on-share handling).
RefPtr* RefPtr::CopyPlain(const RefPtr* other) {
    CRefObject* object = other->ptr;
    ptr = object;
    if (object) ++object->refs_;
    return this;
}

EI_REWRITTEN(0x004263B0, RefPtr::CopyPlain);
EI_REWRITTEN(0x00429910, RefPtr::CopyPlain);
EI_REWRITTEN(0x00609960, RefPtr::CopyPlain);
EI_REWRITTEN(0x005BEF50, RefPtr::CopyPlain);
// 00609D40: the counted pointer's assignment from another holder (the chat window's player list
// 006091C0): RefPtr::Assign of the other's object (a reference added, the old one released, even
// for the same object; no copy-on-share handling). Returns the holder.
RefPtr* RefPtr::AssignCounted(const RefPtr* other) {
    Assign(other->ptr);
    return this;
}

namespace net {
EI_REWRITTEN(0x00609D40, RefPtr::AssignCounted);

// 0041ACF0: the trade between the two offers (in either order), or null.
void* ServerSession::FindTrade(void* offer, void* other) {
    for (RefNode* node = trades_.First(); node != trades_.End(); node = node->next) {
        auto* trade = static_cast<TradeMessage*>(node->data.ptr);
        void* first = trade->offer_.ptr;
        if ((first == offer && trade->partnerOffer_.ptr == other) || (first == other && trade->partnerOffer_.ptr == offer))
            return node->data.ptr;
    }
    return nullptr;
}

// 0041B720 (removed file request): drops `message` from +0x259C.
void __fastcall ServerOnObjectRemoved(ServerSession* self, Message* message) {
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, message);   // 00428060
    RefNode* sentinel = self->fileRequests_.End();
    for (RefNode* node = sentinel->next; node != sentinel;) {
        RefNode* next = node->next;
        if (node->data.ptr == value->ptr) self->fileRequests_.Erase(node);
        node = next;
    }
}

// 0041B7A0 (offer created): lists the offer; when the partner's offer is listed, a trade of the two
// is made (both marked paired), else the partner is told of the offer.
void __fastcall ServerOnTradeOffer(ServerSession* self, PartyMessage* message) {
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, message);   // 00428070
    self->offers_.Insert(self->offers_.End(), *value);

    PartyMessage* other = FindPartnerOffer(self->offers_, message);
    if (!other) {
        TellPartner(self, message, 10);
        return;
    }
    auto* trade = NewMessageMemory<TradeMessage>();
    if (trade) {
        MessageConstructBase(trade);
        trade->player_.ptr = nullptr;   // RefPtr() (0041FF80)
        trade->partner_.ptr = nullptr;   // RefPtr() (0041FF80)
        trade->offer_.ptr = nullptr;   // RefPtr() (0041FFD0)
        trade->partnerOffer_.ptr = nullptr;   // RefPtr() (0041FFD0)
        ei::SetVtable(trade, TradeMessage::kVtableAddress);
        trade->done_ = 0;
        trade->revision_ = 0;
        trade->partnerRevision_ = 0;
    }
    trade->offer_.Assign(message);
    trade->partnerOffer_.Assign(other);
    trade->player_.Assign(message->player_.ptr);
    trade->partner_.Assign(message->partner_.ptr);
    RefPtr tradeRef;
    value = RefPtrFromRaw(&tradeRef, trade);   // 00428080
    self->trades_.Insert(self->trades_.End(), *value);
    message->trading_ = 1;
    other->trading_ = 1;
}

// 0041B970 (offer updated): the offer's version moves on; when both offers of its trade are live,
// accepted and agree crosswise (each one accepted the other's current revision), the trade is
// done with the two revisions.
void __fastcall ServerOnTradeUpdate(ServerSession* self, PartyMessage* message) {
    ++message->version_;
    PartyMessage* other = FindPartnerOffer(self->offers_, message);
    auto* trade = static_cast<TradeMessage*>(self->FindTrade(message, other));
    if (!Live(trade)) return;
    auto* first = static_cast<PartyMessage*>(trade->offer_.ptr);
    if (!Live(first)) return;
    auto* second = static_cast<PartyMessage*>(trade->partnerOffer_.ptr);
    if (!Live(second)) return;
    if (!first->accepted_ || first->acceptedRevision_ != second->revision_) return;
    if (!second->accepted_ || second->acceptedRevision_ != first->revision_) return;
    trade->done_ = 1;
    trade->revision_ = first->revision_;
    trade->partnerRevision_ = second->revision_;
    ++trade->version_;
}

// 0041BA30 (offer removed): drops the offer; an unpaired offer's partner is told it was withdrawn.
// Its trade, when live, is dropped, a done one first kept on +0x25C0 for 60 seconds.
void __fastcall ServerOnTradeCancel(ServerSession* self, PartyMessage* message) {
    RefPtr ref;
    const RefPtr* value = RefPtrFromRaw(&ref, message);   // 00428070
    RefNode* sentinel = self->offers_.End();
    for (RefNode* node = sentinel->next; node != sentinel;) {
        RefNode* next = node->next;
        if (node->data.ptr == value->ptr) self->offers_.Erase(node);
        node = next;
    }
    PartyMessage* other = FindPartnerOffer(self->offers_, message);
    auto* trade = static_cast<TradeMessage*>(self->FindTrade(message, other));
    if (!message->trading_) TellPartner(self, message, 11);
    if (!Live(trade)) return;
    if (trade->done_) {
        RefPtr held;
        held.ptr = trade;
        ++trade->refs_;
        TimedEntry entry;
        entry.key = kFinishedDelay;
        entry.ref.CopyPlain(&held);
        self->finishedTrades_.PushBack(entry);
        if (entry.ref.ptr) entry.ref.ptr->Release();
        ReleaseInline(held.ptr);
    }
    RefPtr removing;
    removing.ptr = trade;
    ++trade->refs_;
    self->trades_.Remove(removing, stlp::SameObject());
    ReleaseInline(removing.ptr);
}

EI_REWRITTEN(0x0041ACF0, ServerSession::FindTrade);
EI_REPLACE(0x0041B720, ServerOnObjectRemoved);
EI_REPLACE(0x0041B7A0, ServerOnTradeOffer);
EI_REPLACE(0x0041B970, ServerOnTradeUpdate);
EI_REPLACE(0x0041BA30, ServerOnTradeCancel);

} // namespace net
