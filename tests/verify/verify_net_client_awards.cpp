// Checks for net_client_awards.cpp: ours only (both change the player, the lists and the world).
//  - ClientApplyAwards: the awards list is empty afterwards.
//  - ClientTick: no timer is left below zero.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace net {
}

namespace {
void* s_unused;

void __fastcall ApplyAwardsVerify(net::ClientSession* self) {
    self->ApplyAwards();
    void** awards = static_cast<void**>(self->awards_);
    ei::verify::Check("ClientApplyAwards", true, awards[0] == awards && awards[1] == awards, "");
}

void __fastcall TickVerify(net::ClientSession* self, void*, double dt) {
    self->ClientSession::Tick(dt);
    bool ok = true;
    void** sentinel = static_cast<void**>(self->timers_);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        float seconds;
        std::memcpy(&seconds, &node[2], 4);
        if (!(seconds >= 0.0f)) ok = false;
    }
    ei::verify::Check("ClientTick", true, ok, "");
}
} // namespace

EI_VERIFY(0x0041CD80, ApplyAwardsVerify, 0, s_unused);
EI_VERIFY(0x0041CCD0, TickVerify, 0, s_unused);
