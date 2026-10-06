// Checks for player_client.cpp (scenario: every one; the local client talks to the server over the
// loopback link every frame). All four use the link, the coders' windows or run the message
// handlers, so they run ours only:
// - SendToServer: the result is 0 or 1, the queue is empty after (when there is a link).
// - DispatchMessages: 1 (the errors are fatal).
// - ReceiveMessages: 0 or 1.
// - Pump: 0 or 1; a connecting client with a connected link is logged in after.
#include "verify.h"

#include "../../src/game/net_link.h"
#include "../../src/game/player_client.h"

namespace {
void* s_unused;

int __fastcall SendToServerVerify(CPlayerClient* self) {
    bool linked = self->Owner()->link_ != nullptr;
    int result = self->SendToServer();
    bool ok = (result == 0 || result == 1) && (!linked || self->out_.size_ == 0);
    ei::verify::Check("CPlayerClient::SendToServer", true, ok, "client %p: result %d", self, result);
    return result;
}

int __fastcall DispatchVerify(CPlayerClient* self) {
    int result = self->DispatchMessages();
    ei::verify::Check("CPlayerClient::DispatchMessages", 1, result, "client %p", self);
    return result;
}

int __fastcall ReceiveVerify(CPlayerClient* self) {
    int result = self->ReceiveMessages();
    ei::verify::Check("CPlayerClient::ReceiveMessages", true, result == 0 || result == 1, "client %p: result %d", self,
                      result);
    return result;
}

int __fastcall PumpVerify(CPlayerClient* self) {
    netlink::NetLink* link = self->Owner()->link_;
    bool loggingIn = self->state_ == 0 && link != nullptr && link->Connected();
    int result = self->Pump();
    bool ok = (result == 0 || result == 1) && (!loggingIn || self->state_ == 1);
    ei::verify::Check("CPlayerClient::Pump", true, ok, "client %p: result %d state %d", self, result, self->state_);
    return result;
}

} // namespace

EI_VERIFY(0x00667810, SendToServerVerify, 0, s_unused);
EI_VERIFY(0x00667970, DispatchVerify, 0, s_unused);
EI_VERIFY(0x00667A30, ReceiveVerify, 0, s_unused);
EI_VERIFY(0x00667680, PumpVerify, 0, s_unused);
