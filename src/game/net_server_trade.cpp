// The game server's trades between players: offers arrive as replicated objects (created, updated,
// removed: 0041B7A0, 0041B970, 0041BA30), two matching offers make a trade (a TradeMessage,
// 0041ACF0 finds it), and 0041B720 drops a removed file request. The offers are PartyMessages
// (net_messages.h). See docs/notes/unmapped_410_470.md ("Seventh round").
#include "net_messages.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "originals_core.h"
#include "originals_library.h"
#include "originals_ui.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall RefNullB(RefPtr* /*ref*/) EI_ORIGINAL(0x0041FFD0)
// Counted pointers from raw ones without a reference, and the STLport lists (library): the
// objects (+0x259C), the offers (+0x25B8), the trades (+0x25BC), the finished trades (+0x25C0).
__declspec(naked) RefPtr* __fastcall ObjectRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428060)
__declspec(naked) RefPtr* __fastcall OfferRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428070)
__declspec(naked) RefPtr* __fastcall TradeRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428080)
__declspec(naked) void** __fastcall ObjectsErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x004293F0)
void** __fastcall OffersInsert(void* a0, void*, void** a2, void* a3, const RefPtr* a4) { return ListInsert423190Original(a0, nullptr, a2, a3, (const void*)a4); }
__declspec(naked) void** __fastcall TradesEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420960)
__declspec(naked) void** __fastcall TradesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x00423410)
__declspec(naked) void __fastcall TradesRemove(void* /*list*/, void*, const RefPtr* /*value*/) EI_ORIGINAL(0x00427320)
__declspec(naked) void __fastcall FinishedPushBack(void* /*list*/, void*, const void* /*entry*/) EI_ORIGINAL(0x00423690)

constexpr std::uintptr_t kTextTable = 0x007C7D50;
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
PartyMessage* FindPartnerOffer(void* list, PartyMessage* offer) {
    void** sentinel = *static_cast<void***>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        auto* other = static_cast<PartyMessage*>(node[2]);
        if (other->player_.ptr == offer->partner_.ptr && other->partner_.ptr == offer->player_.ptr)
            return static_cast<PartyMessage*>(node[2]);
    }
    return nullptr;
}

// The partner is told a trade was offered (text 10) or withdrawn (text 11), with the player's name.
void TellPartner(ServerSession* self, PartyMessage* offer, int text) {
    CRefObject* partner = offer->partner_.ptr;
    const char* name = static_cast<PlayerMessage*>(offer->player_.ptr)->name_.start;
    std::uint32_t line = 0;   // CString
    void* formatted = TextFormat(reinterpret_cast<void*>(kTextTable), nullptr, &line, text, name);
    self->Announce(*static_cast<const char**>(formatted), partner);
    CStringDestroy(&line);
}

} // namespace

// 004263B0: a counted pointer copy (a reference added, no copy-on-share handling).
RefPtr* __fastcall RefPtrCopyPlain(RefPtr* self, void*, const RefPtr* other) {
    CRefObject* object = other->ptr;
    self->ptr = object;
    if (object) ++object->refs_;
    return self;
}
EI_REPLACE(0x004263B0, RefPtrCopyPlain);
EI_REPLACE(0x00429910, RefPtrCopyPlain);   // byte-identical copy

// 0041ACF0: the trade between the two offers (in either order), or null.
void* ServerSession::FindTrade(void* offer, void* other) {
    void** sentinel = static_cast<void**>(trades_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        auto* trade = static_cast<TradeMessage*>(node[2]);
        void* first = trade->offer_.ptr;
        if ((first == offer && trade->partnerOffer_.ptr == other) || (first == other && trade->partnerOffer_.ptr == offer))
            return node[2];
    }
    return nullptr;
}

// 0041B720 (removed file request): drops `message` from +0x259C.
void __fastcall ServerOnObjectRemoved(ServerSession* self, Message* message) {
    RefPtr ref;
    const RefPtr* value = ObjectRefFromRaw(&ref, message);
    void* objects = &self->fileRequests_;
    void** sentinel = static_cast<void**>(self->fileRequests_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        if (node[2] == value->ptr) {
            void* erased;
            ObjectsErase(objects, nullptr, &erased, node);
        }
        node = next;
    }
}

// 0041B7A0 (offer created): lists the offer; when the partner's offer is listed, a trade of the two
// is made (both marked paired), else the partner is told of the offer.
void __fastcall ServerOnTradeOffer(ServerSession* self, PartyMessage* message) {
    void* offers = &self->offers_;
    RefPtr ref;
    const RefPtr* value = OfferRefFromRaw(&ref, message);
    void* end;
    PartiesEnd(offers, nullptr, &end);
    void* inserted;
    OffersInsert(offers, nullptr, &inserted, end, value);

    PartyMessage* other = FindPartnerOffer(offers, message);
    if (!other) {
        TellPartner(self, message, 10);
        return;
    }
    auto* trade = NewMessageMemory<TradeMessage>();
    if (trade) {
        MessageConstructBase(trade);
        RefNullA(&trade->player_);
        RefNullA(&trade->partner_);
        RefNullB(&trade->offer_);
        RefNullB(&trade->partnerOffer_);
        ei::SetVtable(trade, TradeMessage::kVtableAddress);
        trade->done_ = 0;
        trade->revision_ = 0;
        trade->partnerRevision_ = 0;
    }
    trade->offer_.Assign(message);
    trade->partnerOffer_.Assign(other);
    trade->player_.Assign(message->player_.ptr);
    trade->partner_.Assign(message->partner_.ptr);
    void* trades = &self->trades_;
    RefPtr tradeRef;
    value = TradeRefFromRaw(&tradeRef, trade);
    TradesEnd(trades, nullptr, &end);
    TradesInsert(trades, nullptr, &inserted, end, value);
    message->trading_ = 1;
    other->trading_ = 1;
}

// 0041B970 (offer updated): the offer's version moves on; when both offers of its trade are live,
// accepted and agree crosswise (each one accepted the other's current revision), the trade is
// done with the two revisions.
void __fastcall ServerOnTradeUpdate(ServerSession* self, PartyMessage* message) {
    ++message->version_;
    PartyMessage* other = FindPartnerOffer(&self->offers_, message);
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
    const RefPtr* value = OfferRefFromRaw(&ref, message);
    void* offers = &self->offers_;
    void** sentinel = static_cast<void**>(self->offers_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        if (node[2] == value->ptr) {
            void* erased;
            PartiesErase(offers, nullptr, &erased, node);
        }
        node = next;
    }
    PartyMessage* other = FindPartnerOffer(offers, message);
    auto* trade = static_cast<TradeMessage*>(self->FindTrade(message, other));
    if (!message->trading_) TellPartner(self, message, 11);
    if (!Live(trade)) return;
    if (trade->done_) {
        RefPtr held;
        held.ptr = trade;
        ++trade->refs_;
        struct {
            std::uint32_t delay;
            RefPtr trade;
        } entry;
        entry.delay = kFinishedDelay;
        RefPtrCopyPlain(&entry.trade, nullptr, &held);
        FinishedPushBack(&self->finishedTrades_, nullptr, &entry);
        if (entry.trade.ptr) entry.trade.ptr->Release();
        ReleaseInline(held.ptr);
    }
    RefPtr removing;
    removing.ptr = trade;
    ++trade->refs_;
    TradesRemove(&self->trades_, nullptr, &removing);
    ReleaseInline(removing.ptr);
}

EI_REPLACE_METHOD(0x0041ACF0, ServerSession::FindTrade);
EI_REPLACE(0x0041B720, ServerOnObjectRemoved);
EI_REPLACE(0x0041B7A0, ServerOnTradeOffer);
EI_REPLACE(0x0041B970, ServerOnTradeUpdate);
EI_REPLACE(0x0041BA30, ServerOnTradeCancel);

} // namespace net
