// Checks for net_server_tick.cpp.
//  - ServerTick: ours only (it ticks the base layer and erases entries); afterwards no timed entry
//    of +0x25C0 / +0x25B4 is below zero (or NaN). An entry within rounding of zero may expire one
//    tick apart from the original (plain float maths, rewrite-guide rule 7).
//  - ServerPlayerStateIs3: side by side (it only reads).
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace net {
void __fastcall ServerTick(ServerSession* self, void*, double dt);
bool __fastcall ServerPlayerStateIs3(ServerSession* self, void*, void* connection);
}

namespace {
void* s_unused;

bool AllRunning(void* list) {
    void** sentinel = static_cast<void**>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        float left;
        std::memcpy(&left, node + 2, 4);
        if (!(left >= 0.0f)) return false;
    }
    return true;
}

void __fastcall TickVerify(net::ServerSession* self, void*, double dt) {
    net::ServerTick(self, nullptr, dt);
    ei::verify::Check("ServerTick", true, AllRunning(self->list_25C0) && AllRunning(self->list_25B4), "");
}

// 00417E40: MOV EDX,[ESP+4] (4), TEST EDX,EDX (2).
bool(__fastcall* s_stateIs3)(net::ServerSession*, void*, void*);
bool __fastcall StateIs3Verify(net::ServerSession* self, void*, void* connection) {
    bool expected = s_stateIs3(self, nullptr, connection);
    bool actual = net::ServerPlayerStateIs3(self, nullptr, connection);
    ei::verify::Check("ServerPlayerStateIs3", expected, actual, "");
    return actual;
}
} // namespace

EI_VERIFY(0x004182E0, TickVerify, 0, s_unused);
EI_VERIFY(0x00417E40, StateIs3Verify, 6, s_stateIs3);
