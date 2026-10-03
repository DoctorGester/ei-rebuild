// Checks for net_server.cpp.
//  - ServerSession::Construct: ours builds the real server first (so its socket gets the game's
//    port); then the original builds a second one in a scratch block holding a copy of the real
//    object's bytes before construction (its bind fails: the port is taken; harmless). The two are
//    compared with BuiltCompare (allocations pair up; the socket handles at +0x54 differ), including
//    the handler tables and the type registry, then the scratch server is destroyed by its own
//    deleting destructor (slot 0; nothing is connected, so it only frees).
//    The original's 00418530 rebuilds the game's multiplayer map list (it clears it first), so the
//    second construction leaves that list as the first one did.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/lib/crt.h"

#include "ei/vcall.h"

#include <cstring>

namespace {

using namespace net;

// 00412D80: PUSH -1 (2), PUSH imm32 (5).
ServerSession*(__fastcall* s_construct)(ServerSession*, void*, std::uint32_t);
ServerSession* __fastcall ServerConstructVerify(ServerSession* self, void*, std::uint32_t arg) {
    constexpr std::size_t kSize = sizeof(ServerSession);
    auto* scratch = static_cast<std::uint8_t*>(gamecrt::operator_new(kSize));
    std::memcpy(scratch, self, kSize);
    ServerSession* actualRet = self->Construct(arg);
    ServerSession* expectedRet = s_construct(reinterpret_cast<ServerSession*>(scratch), nullptr, arg);
    auto* actual = reinterpret_cast<const std::uint8_t*>(self);
    netverify::BuiltCompare cmp{scratch, actual, kSize};
    cmp.Range(0, kSize, {0x54});
    netverify::CompareSessionTables(cmp, scratch, actual);
    ei::verify::Check("ServerSession::Construct", true,
                      cmp.ok && expectedRet == reinterpret_cast<ServerSession*>(scratch) && actualRet == self, "%s",
                      cmp.detail);
    ei::VCall<void*>(scratch, 0, 1);   // deleting destructor
    return actualRet;
}

} // namespace

EI_VERIFY(0x00412D80, ServerConstructVerify, 7, s_construct);
