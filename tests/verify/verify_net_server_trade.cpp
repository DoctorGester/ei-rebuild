// Checks for net_server_trade.cpp.
//  - FindTrade: side by side (it only reads).
//  - RefPtrCopyPlain: ours only (it adds a reference); the copy points at the same object.
//  - The four handlers: ours only (they change the lists and allocate).
#include "net_verify.h"

#include "../../src/game/net_messages.h"
#include "../../src/game/core_refcount.h"

namespace net {
void __fastcall ServerOnObjectRemoved(ServerSession* self, Message* message);
void __fastcall ServerOnTradeOffer(ServerSession* self, PartyMessage* message);
void __fastcall ServerOnTradeUpdate(ServerSession* self, PartyMessage* message);
void __fastcall ServerOnTradeCancel(ServerSession* self, PartyMessage* message);
}

namespace {
void* s_unused;

// 0041ACF0: PUSH EBX, PUSH ESI, PUSH EDI (1 each), MOV EDI,[ECX+0x25BC] (6).
void*(__fastcall* s_findTrade)(net::ServerSession*, void*, void*, void*);
void* __fastcall FindTradeVerify(net::ServerSession* self, void*, void* offer, void* other) {
    void* expected = s_findTrade(self, nullptr, offer, other);
    void* actual = self->FindTrade(offer, other);
    ei::verify::Check("ServerSession::FindTrade", expected, actual, "");
    return actual;
}

RefPtr* __fastcall CopyPlainVerify(RefPtr* self, void*, const RefPtr* other) {
    RefPtr* result = self->CopyPlain(other);
    ei::verify::Check("RefPtrCopyPlain", true, result == self && self->ptr == other->ptr, "");
    return result;
}

// 00609D40: ours only (it changes reference counts); the holder points at the other's object,
// which gained a reference (unless it is the old one).
RefPtr* __fastcall AssignCountedVerify(RefPtr* self, void*, const RefPtr* other) {
    CRefObject* object = other->ptr;
    CRefObject* old = self->ptr;
    int refs = object ? object->refs_ : 0;
    RefPtr* result = self->AssignCounted(other);
    bool ok = result == self && self->ptr == object && (!object || object == old || object->refs_ == refs + 1);
    ei::verify::Check("RefPtrAssignCounted", true, ok, "");
    return result;
}

void __fastcall ObjectRemovedVerify(net::ServerSession* self, net::Message* message) {
    net::ServerOnObjectRemoved(self, message);
    ei::verify::Check("ServerOnObjectRemoved", true, true, "");
}
void __fastcall TradeOfferVerify(net::ServerSession* self, net::PartyMessage* message) {
    net::ServerOnTradeOffer(self, message);
    ei::verify::Check("ServerOnTradeOffer", true, true, "");
}
void __fastcall TradeUpdateVerify(net::ServerSession* self, net::PartyMessage* message) {
    net::ServerOnTradeUpdate(self, message);
    ei::verify::Check("ServerOnTradeUpdate", true, true, "");
}
void __fastcall TradeCancelVerify(net::ServerSession* self, net::PartyMessage* message) {
    net::ServerOnTradeCancel(self, message);
    ei::verify::Check("ServerOnTradeCancel", true, true, "");
}
} // namespace

EI_VERIFY(0x0041ACF0, FindTradeVerify, 9, s_findTrade);
EI_VERIFY(0x004263B0, CopyPlainVerify, 0, s_unused);
EI_VERIFY(0x00429910, CopyPlainVerify, 0, s_unused);
EI_VERIFY(0x00609960, CopyPlainVerify, 0, s_unused);
EI_VERIFY(0x005BEF50, CopyPlainVerify, 0, s_unused);   // A-W12 copy
EI_VERIFY(0x00609D40, AssignCountedVerify, 0, s_unused);
EI_VERIFY(0x0041B720, ObjectRemovedVerify, 0, s_unused);
EI_VERIFY(0x0041B7A0, TradeOfferVerify, 0, s_unused);
EI_VERIFY(0x0041B970, TradeUpdateVerify, 0, s_unused);
EI_VERIFY(0x0041BA30, TradeCancelVerify, 0, s_unused);
