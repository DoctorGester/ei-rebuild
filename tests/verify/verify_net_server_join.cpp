// Checks for net_server_join.cpp: ours only (it appends to the server's lists, bumps the player
// serial and announces the player). Afterwards the message's binding must be the last binding and
// point at the last player record, which carries the message's name.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace net {
void __fastcall ServerOnJoin(ServerSession* self, Message* message);
}

namespace {
void* s_unused;

void* LastObject(const std::uint8_t* list) {
    void** sentinel = *reinterpret_cast<void** const*>(list);
    return static_cast<void**>(sentinel[1])[2];
}

void __fastcall ServerOnJoinVerify(net::ServerSession* self, net::Message* message) {
    net::ServerOnJoin(self, message);
    auto* base = reinterpret_cast<const std::uint8_t*>(self);
    auto* msg = reinterpret_cast<const std::uint8_t*>(message);
    auto* player = static_cast<const std::uint8_t*>(LastObject(base + 0x2590));
    auto* binding = static_cast<const std::uint8_t*>(LastObject(base + 0x25A4));
    void* kept;
    void* bound;
    std::memcpy(&kept, msg + 0x30, 4);
    std::memcpy(&bound, binding + 0xC, 4);
    auto* name = reinterpret_cast<const net::StringRep*>(player + 0xC);
    auto* from = reinterpret_cast<const net::StringRep*>(msg + 0xC);
    std::size_t length = static_cast<std::size_t>(name->finish - name->start);
    bool sameName = length == static_cast<std::size_t>(from->finish - from->start) &&
                    std::memcmp(name->start, from->start, length) == 0;
    ei::verify::Check("ServerOnJoin", true, kept == binding && bound == player && sameName, "");
}
} // namespace

EI_VERIFY(0x0041ADB0, ServerOnJoinVerify, 0, s_unused);
