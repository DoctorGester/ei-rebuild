// Checks for net_client_party.cpp.
//  - FindSameMessage: side by side (it only reads; the stream sizes come through slot 4).
//  - ClientPublishParty: ours only (it allocates messages and moves counted references). After it
//    the object holds one message per party member and is no longer dirty.
//  - ClientFindParty: side by side (it only reads).
//  - ClientApplyParty: ours only (it creates objects and fills a party).
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/core_refcount.h"

#include <cstring>

namespace net {
RefPtr* __fastcall FindSameMessage(RefPtr* first, RefPtr* last, Message* message, std::uint32_t unused);
void __fastcall ClientPublishParty(ClientSession* self);
void* __fastcall ClientFindParty(ClientSession* self);
void __fastcall ClientApplyParty(ClientSession* self);
}

namespace {
void* s_unused;

// 00429FC0: PUSH ECX, PUSH EBX, PUSH EBP (1 each), MOV EBP,ECX (2).
RefPtr*(__fastcall* s_find)(RefPtr*, RefPtr*, net::Message*, std::uint32_t);
RefPtr* __fastcall FindVerify(RefPtr* first, RefPtr* last, net::Message* message, std::uint32_t unused) {
    RefPtr* expected = s_find(first, last, message, unused);
    RefPtr* actual = net::FindSameMessage(first, last, message, unused);
    ei::verify::Check("FindSameMessage", expected, actual, "");
    return actual;
}

void __fastcall PublishPartyVerify(net::ClientSession* self) {
    net::ClientPublishParty(self);
    auto* state = static_cast<const std::uint8_t*>(self->unknown_260C);
    if (!state) {
        ei::verify::Check("ClientPublishParty", true, true, "");
        return;
    }
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(self);
    std::int32_t members;
    std::memcpy(&members, base + 0x2664, 4);
    RefPtr* start;
    RefPtr* finish;
    std::memcpy(&start, state + 0xC, 4);
    std::memcpy(&finish, state + 0x10, 4);
    std::int32_t messages = static_cast<std::int32_t>(finish - start);
    ei::verify::Check("ClientPublishParty", true, messages == (members > 0 ? members : 0) && state[0x1C] == 0,
                      "members %d, messages %d", members, messages);
}

// 0041E220: PUSH EBP, PUSH ESI (1 each), MOV ESI,[ECX+0x260C] (6).
void*(__fastcall* s_findParty)(net::ClientSession*);
void* __fastcall FindPartyVerify(net::ClientSession* self) {
    void* expected = s_findParty(self);
    void* actual = net::ClientFindParty(self);
    ei::verify::Check("ClientFindParty", expected, actual, "");
    return actual;
}

void __fastcall ApplyPartyVerify(net::ClientSession* self) {
    net::ClientApplyParty(self);
    ei::verify::Check("ClientApplyParty", true, true, "");
}
} // namespace

EI_VERIFY(0x00429FC0, FindVerify, 5, s_find);
EI_VERIFY(0x0041E220, FindPartyVerify, 8, s_findParty);
EI_VERIFY(0x0041DDC0, ApplyPartyVerify, 0, s_unused);
EI_VERIFY(0x0041DA50, PublishPartyVerify, 0, s_unused);
