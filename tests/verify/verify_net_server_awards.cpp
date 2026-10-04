// Checks for net_server_awards.cpp: ours only (each takes references, allocates or queues).
//  - RefPtrCopyConstruct: null for a missing or detached object, the object itself unless it is
//    copied on share, and the result marked copy-on-share.
//  - QueueAward: the pending awards end with {60.0, the award (a copy: it is shared)}, the award
//    holding the kind and the value.
//  - AwardHost, CompleteZone, LeaveZone: ours only (the last two leave the queued messages empty
//    and no map message held).
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/core_refcount.h"

#include <cstring>

namespace net {
RefPtr* __fastcall RefPtrCopyConstruct(RefPtr* self, void*, const RefPtr* other);
}

namespace {
void* s_unused;

RefPtr* __fastcall CopyVerify(RefPtr* self, void*, const RefPtr* other) {
    CRefObject* source = other->ptr;
    bool live = source && !(source->flags7_ & CRefObject::kDetached);
    bool copied = live && (source->flags6_ & CRefObject::kCopyOnShare);
    RefPtr* result = net::RefPtrCopyConstruct(self, nullptr, other);
    bool ok = result == self;
    if (!live) ok = ok && self->ptr == nullptr;
    else ok = ok && self->ptr && (copied || self->ptr == source) && (self->ptr->flags6_ & CRefObject::kCopyOnShare);
    ei::verify::Check("RefPtrCopyConstruct", true, ok, "");
    return result;
}

void __fastcall QueueAwardVerify(net::ServerSession* self, void*, CRefObject* connection, int kind, float value,
                                 net::StringRep text) {
    self->QueueAward(connection, kind, value, text);
    void** sentinel = static_cast<void**>(self->list_25B4);
    auto** last = static_cast<void**>(sentinel[1]);
    std::uint32_t delay;
    std::memcpy(&delay, &last[2], 4);
    auto* award = static_cast<const std::uint8_t*>(last[3]);
    bool ok = last != sentinel && delay == 0x42700000u && award;
    if (ok) {
        std::int32_t heldKind;
        float heldValue;
        std::memcpy(&heldKind, award + 0x10, 4);
        std::memcpy(&heldValue, award + 0x14, 4);
        ok = heldKind == kind && std::memcmp(&heldValue, &value, 4) == 0;
    }
    ei::verify::Check("ServerSession::QueueAward", true, ok, "kind %d", kind);
}

void __fastcall AwardHostVerify(net::ServerSession* self, void*, const std::uint8_t* entry, bool withFinal) {
    self->AwardHost(entry, withFinal);
    ei::verify::Check("ServerSession::AwardHost", true, true, "");
}

void __fastcall CompleteZoneVerify(net::ServerSession* self, void*, const net::StringRep* zone) {
    self->CompleteZone(zone);
    void** messages = static_cast<void**>(self->list_25AC);
    ei::verify::Check("ServerSession::CompleteZone", true, messages[0] == messages && messages[1] == messages, "");
}

void __fastcall LeaveZoneVerify(net::ServerSession* self) {
    self->LeaveZone();
    void** messages = static_cast<void**>(self->list_25AC);
    ei::verify::Check("ServerSession::LeaveZone", true,
                      messages[0] == messages && messages[1] == messages && self->unknown_25A8 == 0, "");
}
} // namespace

EI_VERIFY(0x00418CC0, LeaveZoneVerify, 0, s_unused);
EI_VERIFY(0x004263F0, CopyVerify, 0, s_unused);
EI_VERIFY(0x004250A0, CopyVerify, 0, s_unused);
EI_VERIFY(0x00419D90, QueueAwardVerify, 0, s_unused);
EI_VERIFY(0x004191C0, AwardHostVerify, 0, s_unused);
EI_VERIFY(0x00418ED0, CompleteZoneVerify, 0, s_unused);
