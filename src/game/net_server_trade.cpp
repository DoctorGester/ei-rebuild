// The game server's trades between players: offers arrive as replicated objects (created, updated,
// removed: 0041B7A0, 0041B970, 0041BA30), two matching offers make a trade (vtable 0073BD60,
// 0041ACF0 finds it), and 0041B720 drops a removed 0073BC58 object. See net_classes.h and
// docs/notes/unmapped_410_470.md ("Seventh round").
//
// An offer: +0x08 version, +0x1C accepted (byte), +0x20 / +0x24 the two values the partners must
// agree on, +0x28 its player, +0x2C the partner, +0x30 paired (byte).
// A trade (0x28 bytes): +0x0C / +0x10 the two players, +0x14 / +0x18 the two offers (counted),
// +0x1C done (byte), +0x20 / +0x24 the agreed values.
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

__declspec(naked) void __fastcall RefNullA(void* /*ref*/) EI_ORIGINAL(0x0041FF80)
__declspec(naked) void __fastcall RefNullB(void* /*ref*/) EI_ORIGINAL(0x0041FFD0)
// Counted pointers from raw ones without a reference, and the STLport lists (library): the
// objects (+0x259C), the offers (+0x25B8), the trades (+0x25BC), the finished trades (+0x25C0).
__declspec(naked) RefPtr* __fastcall ObjectRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428060)
__declspec(naked) RefPtr* __fastcall OfferRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428070)
__declspec(naked) RefPtr* __fastcall TradeRefFromRaw(RefPtr* /*ref*/, void* /*object*/) EI_ORIGINAL(0x00428080)
__declspec(naked) void** __fastcall ObjectsErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x004293F0)
__declspec(naked) void** __fastcall OffersEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x004208A0)
__declspec(naked) void** __fastcall OffersInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x00423190)
__declspec(naked) void** __fastcall OffersErase(void* /*list*/, void*, void** /*out*/, void* /*position*/) EI_ORIGINAL(0x004294D0)
__declspec(naked) void** __fastcall TradesEnd(void* /*list*/, void*, void** /*out*/) EI_ORIGINAL(0x00420960)
__declspec(naked) void** __fastcall TradesInsert(void* /*list*/, void*, void** /*out*/, void* /*pos*/, const RefPtr* /*value*/) EI_ORIGINAL(0x00423410)
__declspec(naked) void __fastcall TradesRemove(void* /*list*/, void*, const RefPtr* /*value*/) EI_ORIGINAL(0x00427320)
__declspec(naked) void __fastcall FinishedPushBack(void* /*list*/, void*, const void* /*entry*/) EI_ORIGINAL(0x00423690)
__declspec(naked) void* __fastcall TextFormat(void* /*table*/, void*, void* /*out CString*/, int /*kind*/, const char* /*text*/) EI_ORIGINAL(0x005F8C90)
__declspec(naked) void __fastcall CStringDestroy(void* /*s*/) EI_ORIGINAL(0x00700970)

constexpr std::uintptr_t kTradeVtable = 0x0073BD60;
constexpr std::uintptr_t kTextTable = 0x007C7D50;
constexpr std::uint32_t kFinishedDelay = 0x42700000;   // 60.0f

template <typename T>
T& At(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}
bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}

// The release a counted pointer's destructor inlines: one reference less, deleted at zero.
void ReleaseInline(CRefObject* object) {
    if (!object) return;
    --object->refs_;
    if (object->refs_ == 0 && object) ei::VCall<void>(object, 0, 1);
}

// The offer of `offer`'s partner (its player is our partner and its partner our player), or null.
void* FindPartnerOffer(void* list, void* offer) {
    void** sentinel = *static_cast<void***>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        void* other = node[2];
        if (At<void*>(other, 0x28) == At<void*>(offer, 0x2C) && At<void*>(other, 0x2C) == At<void*>(offer, 0x28))
            return node[2];
    }
    return nullptr;
}

// The partner is told a trade was offered (text 10) or withdrawn (text 11), with the player's name.
void TellPartner(ServerSession* self, void* offer, int text) {
    auto* partner = At<CRefObject*>(offer, 0x2C);
    const char* name = At<const char*>(At<void*>(offer, 0x28), 0xC);
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
    void** sentinel = static_cast<void**>(list_25BC);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        void* trade = node[2];
        void* first = At<void*>(trade, 0x14);
        if ((first == offer && At<void*>(trade, 0x18) == other) || (first == other && At<void*>(trade, 0x18) == offer))
            return node[2];
    }
    return nullptr;
}

// 0041B720 (removed 0073BC58 object): drops `message` from +0x259C.
void __fastcall ServerOnObjectRemoved(ServerSession* self, Message* message) {
    RefPtr ref;
    const RefPtr* value = ObjectRefFromRaw(&ref, message);
    void* objects = &self->list_259C;
    void** sentinel = static_cast<void**>(self->list_259C);
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
void __fastcall ServerOnTradeOffer(ServerSession* self, Message* message) {
    void* offers = &self->list_25B8;
    RefPtr ref;
    const RefPtr* value = OfferRefFromRaw(&ref, message);
    void* end;
    OffersEnd(offers, nullptr, &end);
    void* inserted;
    OffersInsert(offers, nullptr, &inserted, end, value);

    void* other = FindPartnerOffer(offers, message);
    if (!other) {
        TellPartner(self, message, 10);
        return;
    }
    auto* trade = static_cast<std::uint8_t*>(gamecrt::operator_new(0x28));
    At<std::uint32_t>(trade, 4) = 0;
    if (trade) {
        MessageConstructBase(reinterpret_cast<Message*>(trade));
        RefNullA(trade + 0xC);
        RefNullA(trade + 0x10);
        RefNullB(trade + 0x14);
        RefNullB(trade + 0x18);
        std::uintptr_t vtable = kTradeVtable;
        std::memcpy(trade, &vtable, 4);
        trade[0x1C] = 0;
        At<std::uint32_t>(trade, 0x20) = 0;
        At<std::uint32_t>(trade, 0x24) = 0;
    }
    reinterpret_cast<RefPtr*>(trade + 0x14)->Assign(reinterpret_cast<CRefObject*>(message));
    reinterpret_cast<RefPtr*>(trade + 0x18)->Assign(static_cast<CRefObject*>(other));
    reinterpret_cast<RefPtr*>(trade + 0xC)->Assign(At<CRefObject*>(message, 0x28));
    reinterpret_cast<RefPtr*>(trade + 0x10)->Assign(At<CRefObject*>(message, 0x2C));
    void* trades = &self->list_25BC;
    RefPtr tradeRef;
    value = TradeRefFromRaw(&tradeRef, trade);
    TradesEnd(trades, nullptr, &end);
    TradesInsert(trades, nullptr, &inserted, end, value);
    At<std::uint8_t>(message, 0x30) = 1;
    At<std::uint8_t>(other, 0x30) = 1;
}

// 0041B970 (offer updated): the offer's version moves on; when both offers of its trade are live,
// accepted and agree crosswise (each one's +0x20 is the other's +0x24), the trade is done with the
// two values.
void __fastcall ServerOnTradeUpdate(ServerSession* self, Message* message) {
    ++At<std::uint32_t>(message, 8);
    void* other = FindPartnerOffer(&self->list_25B8, message);
    void* trade = self->FindTrade(message, other);
    if (!Live(trade)) return;
    void* first = At<void*>(trade, 0x14);
    if (!Live(first)) return;
    void* second = At<void*>(trade, 0x18);
    if (!Live(second)) return;
    if (!At<std::uint8_t>(first, 0x1C) || At<std::uint32_t>(first, 0x20) != At<std::uint32_t>(second, 0x24)) return;
    if (!At<std::uint8_t>(second, 0x1C) || At<std::uint32_t>(second, 0x20) != At<std::uint32_t>(first, 0x24)) return;
    At<std::uint8_t>(trade, 0x1C) = 1;
    At<std::uint32_t>(trade, 0x20) = At<std::uint32_t>(first, 0x24);
    At<std::uint32_t>(trade, 0x24) = At<std::uint32_t>(second, 0x24);
    ++At<std::uint32_t>(trade, 8);
}

// 0041BA30 (offer removed): drops the offer; an unpaired offer's partner is told it was withdrawn.
// Its trade, when live, is dropped, a done one first kept on +0x25C0 for 60 seconds.
void __fastcall ServerOnTradeCancel(ServerSession* self, Message* message) {
    RefPtr ref;
    const RefPtr* value = OfferRefFromRaw(&ref, message);
    void* offers = &self->list_25B8;
    void** sentinel = static_cast<void**>(self->list_25B8);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel;) {
        void** next = static_cast<void**>(node[0]);
        if (node[2] == value->ptr) {
            void* erased;
            OffersErase(offers, nullptr, &erased, node);
        }
        node = next;
    }
    void* other = FindPartnerOffer(offers, message);
    auto* trade = static_cast<CRefObject*>(self->FindTrade(message, other));
    if (!At<std::uint8_t>(message, 0x30)) TellPartner(self, message, 11);
    if (!Live(trade)) return;
    if (At<std::uint8_t>(trade, 0x1C)) {
        RefPtr held;
        held.ptr = trade;
        ++trade->refs_;
        struct {
            std::uint32_t delay;
            RefPtr trade;
        } entry;
        entry.delay = kFinishedDelay;
        RefPtrCopyPlain(&entry.trade, nullptr, &held);
        FinishedPushBack(&self->list_25C0, nullptr, &entry);
        if (entry.trade.ptr) entry.trade.ptr->Release();
        ReleaseInline(held.ptr);
    }
    RefPtr removing;
    removing.ptr = trade;
    ++trade->refs_;
    TradesRemove(&self->list_25BC, nullptr, &removing);
    ReleaseInline(removing.ptr);
}

namespace {
void* __fastcall FindTradeHook(ServerSession* self, void*, void* offer, void* other) { return self->FindTrade(offer, other); }
} // namespace
EI_REPLACE(0x0041ACF0, FindTradeHook);
EI_REPLACE(0x0041B720, ServerOnObjectRemoved);
EI_REPLACE(0x0041B7A0, ServerOnTradeOffer);
EI_REPLACE(0x0041B970, ServerOnTradeUpdate);
EI_REPLACE(0x0041BA30, ServerOnTradeCancel);

} // namespace net
