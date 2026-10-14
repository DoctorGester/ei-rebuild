// Checks for net_client_party.cpp.
//  - FindSameMessage: side by side (it only reads; the stream sizes come through slot 4).
//  - ClientPublishParty: ours only (it allocates messages and moves counted references). After it
//    the object holds one message per party member and is no longer dirty.
//  - ClientFindParty: side by side (it only reads).
//  - ClientApplyParty: ours only (it creates objects and fills a party).
#include "net_verify.h"

#include "../../src/game/net_messages.h"
#include "../../src/game/core_refcount.h"

#include <cstring>

namespace net {
RefPtr* __fastcall FindSameMessage(RefPtr* first, RefPtr* last, PartyMemberMessage* message, std::uint32_t unused);
void __fastcall ClientPublishParty(ClientSession* self);
void* __fastcall ClientFindParty(ClientSession* self);
void __fastcall ClientApplyParty(ClientSession* self);
void __fastcall ClientCommitParty(ClientSession* self);
}

namespace {
void* s_unused;

// 00429FC0: PUSH ECX, PUSH EBX, PUSH EBP (1 each), MOV EBP,ECX (2).
RefPtr*(__fastcall* s_find)(RefPtr*, RefPtr*, net::PartyMemberMessage*, std::uint32_t);
RefPtr* __fastcall FindVerify(RefPtr* first, RefPtr* last, net::PartyMemberMessage* message, std::uint32_t unused) {
    RefPtr* expected = s_find(first, last, message, unused);
    RefPtr* actual = net::FindSameMessage(first, last, message, unused);
    ei::verify::Check("FindSameMessage", expected, actual, "");
    return actual;
}

void __fastcall PublishPartyVerify(net::ClientSession* self) {
    net::ClientPublishParty(self);
    auto* state = static_cast<const net::PartyMessage*>(self->party_.ptr);
    if (!state) {
        ei::verify::Check("ClientPublishParty", true, true, "");
        return;
    }
    const std::uint8_t* base = reinterpret_cast<const std::uint8_t*>(self);
    std::int32_t members;
    std::memcpy(&members, base + 0x2664, 4);
    std::int32_t messages = static_cast<std::int32_t>(state->members_.size());
    ei::verify::Check("ClientPublishParty", true, messages == (members > 0 ? members : 0) && state->accepted_ == 0,
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

// 0041E180: MOV EAX,[ECX+0x260C] (6). Side by side on a copy of the description's 0x30 bytes.
void(__fastcall* s_commit)(net::ClientSession*);
void __fastcall CommitVerify(net::ClientSession* self) {
    auto* state = reinterpret_cast<std::uint8_t*>(self->party_.ptr);
    if (!state) {
        s_commit(self);
        net::ClientCommitParty(self);
        ei::verify::Check("ClientCommitParty", true, true, "");
        return;
    }
    alignas(4) std::uint8_t before[0x30];
    std::memcpy(before, state, sizeof(before));
    s_commit(self);
    alignas(4) std::uint8_t expected[0x30];
    std::memcpy(expected, state, sizeof(expected));
    std::memcpy(state, before, sizeof(before));
    net::ClientCommitParty(self);
    ei::verify::Check("ClientCommitParty", true, std::memcmp(expected, state, sizeof(expected)) == 0, "");
}
} // namespace

EI_VERIFY(0x0041E180, CommitVerify, 6, s_commit);
EI_VERIFY(0x00429FC0, FindVerify, 5, s_find);
EI_VERIFY(0x0041E220, FindPartyVerify, 8, s_findParty);
EI_VERIFY(0x0041DDC0, ApplyPartyVerify, 0, s_unused);
EI_VERIFY(0x0041DA50, PublishPartyVerify, 0, s_unused);
