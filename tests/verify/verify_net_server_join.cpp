// Checks for net_server_join.cpp: ours only (the handlers change the server's lists and records).
//  - ServerOnJoin: afterwards the message's binding must be the last binding and point at the last
//    player record, which carries the message's name.
//  - ServerOnPlayerChanged: the player holds the message's +0x28 / +0x2C.
//  - ServerOnPlayerGone: a live binding is no longer listed, nor its live player.
#include "net_verify.h"

#include "../../src/game/net_classes.h"

#include <cstring>

namespace net {
void __fastcall ServerOnJoin(ServerSession* self, Message* message);
void __fastcall ServerOnPlayerChanged(ServerSession* self, Message* message);
void __fastcall ServerOnPlayerGone(ServerSession* self, Message* message);
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

void* Ptr(const void* p, std::size_t offset) {
    void* v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}
std::uint32_t Dword(const void* p, std::size_t offset) {
    std::uint32_t v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(p) + offset, 4);
    return v;
}
bool Live(const void* object) {
    return object && !(static_cast<const std::uint8_t*>(object)[7] & 0x80);
}
bool Listed(void* list, const void* value) {
    void** sentinel = static_cast<void**>(list);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0]))
        if (node[2] == value) return true;
    return false;
}

void __fastcall PlayerChangedVerify(net::ServerSession* self, net::Message* message) {
    net::ServerOnPlayerChanged(self, message);
    void* player = Ptr(Ptr(message, 0x30), 0xC);
    ei::verify::Check("ServerOnPlayerChanged", true,
                      Dword(player, 0x24) == Dword(message, 0x28) && Dword(player, 0x2C) == Dword(message, 0x2C), "");
}
void __fastcall PlayerGoneVerify(net::ServerSession* self, net::Message* message) {
    void* binding = Ptr(message, 0x30);
    bool live = Live(binding);
    void* player = live ? Ptr(binding, 0xC) : nullptr;
    bool playerLive = live && Live(player);
    net::ServerOnPlayerGone(self, message);
    bool ok = !live || (!Listed(self->bindings_, binding) && (!playerLive || !Listed(self->players_, player)));
    ei::verify::Check("ServerOnPlayerGone", true, ok, "");
}
} // namespace

EI_VERIFY(0x0041ADB0, ServerOnJoinVerify, 0, s_unused);
EI_VERIFY(0x0041AD80, PlayerChangedVerify, 0, s_unused);
EI_VERIFY(0x0041B0C0, PlayerGoneVerify, 0, s_unused);
