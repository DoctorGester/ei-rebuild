// The game server's tick (004182E0, slot 14) and a player state query (00417E40, called by the
// file chunk sender at 00479EF4). Float maths in plain C++ (rewrite-guide rule 7). See
// net_classes.h and docs/notes/unmapped_410_470.md ("Eighth round").
#include "net_classes.h"
#include "core_refcount.h"

#include "ei/hook.h"
#include "lib/crt.h"

#include <cstring>

namespace net {

namespace {

// The server base layer's tick (00440000 range).
__declspec(naked) void __fastcall ServerBaseTick(ServerSession* /*self*/, void*, double /*dt*/) EI_ORIGINAL(0x00443590)
// erase(position) of the timed lists +0x25C0 and +0x25B4 (library).
__declspec(naked) void** __fastcall List25C0Erase(void* /*list*/, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x004236C0)
__declspec(naked) void** __fastcall List25B4Erase(void* /*list*/, void*, void** /*out*/, void* /*node*/) EI_ORIGINAL(0x00422FA0)
// 0041AA10: announces the game to the master server (patched by Rebirth at a call site; called
// through its address).
__declspec(naked) void __fastcall ServerAnnounceToMaster(ServerSession* /*self*/) EI_ORIGINAL(0x0041AA10)

constexpr std::uintptr_t kZero = 0x0073B858;             // 0.0f
constexpr std::uintptr_t kAnnouncePeriod = 0x0073BB20;   // seconds between announcements
constexpr std::uintptr_t kMasterHost = 0x0079BED8;       // char*: the master server's host name
constexpr std::uintptr_t kEmptyText = 0x0079B298;

template <typename T>
T& F(void* object, std::size_t offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}
float GameFloat(std::uintptr_t address) {
    float value;
    std::memcpy(&value, reinterpret_cast<const void*>(address), sizeof(value));
    return value;
}
bool Live(const void* object) {
    return object && !(static_cast<const CRefObject*>(object)->flags7_ & CRefObject::kDetached);
}

// Timed entries {next, prev, seconds left (float), ...}: each loses `dt`; those below zero (or
// NaN) are erased. The end is read again after every entry, as the original does.
template <typename Erase>
void CountDown(void*& list, Erase erase, float dt) {
    void** node = static_cast<void**>(static_cast<void**>(list)[0]);
    while (node != list) {
        void** current = node;
        node = static_cast<void**>(node[0]);
        float left = F<float>(current, 8) - dt;
        F<float>(current, 8) = left;
        if (!(left >= GameFloat(kZero))) {
            void* next;
            erase(&list, nullptr, &next, current);
        }
    }
}

} // namespace

// 004182E0 (slot 14): the base layer's tick, then the timed entries of +0x25C0 and +0x25B4 count
// down by `dt` (as a float), and when the game is public (+0x2574) the announcement timer (+0x2570)
// does too: when it runs out it is reset and, with a master server host set, the game is announced.
// A timer that lands within rounding of zero may expire one tick apart from the original (x87
// versus plain float maths).
void __fastcall ServerTick(ServerSession* self, void*, double dt) {
    ServerBaseTick(self, nullptr, dt);
    float step = static_cast<float>(dt);
    CountDown(self->list_25C0, List25C0Erase, step);
    CountDown(self->list_25B4, List25B4Erase, step);
    if (!self->unknown_2574) return;
    float timer;
    std::memcpy(&timer, &self->unknown_2570, sizeof(timer));
    timer = static_cast<float>(timer - dt);
    std::memcpy(&self->unknown_2570, &timer, sizeof(timer));
    if (!(timer >= GameFloat(kZero))) {   // run out (or NaN)
        std::memcpy(&self->unknown_2570, reinterpret_cast<const void*>(kAnnouncePeriod), 4);
        const auto* host = *reinterpret_cast<const unsigned char* const*>(kMasterHost);
        if (gamecrt::mbscmp(host, reinterpret_cast<const unsigned char*>(kEmptyText)) != 0) ServerAnnounceToMaster(self);
    }
}
EI_REPLACE(0x004182E0, ServerTick);

// 00417E40: whether the player bound to `connection` (the first binding of +0x25A4 for it) is in
// state 3 (its +0x24). False for a dead connection, binding or player.
bool __fastcall ServerPlayerStateIs3(ServerSession* self, void*, void* connection) {
    if (!Live(connection)) return false;
    void** sentinel = static_cast<void**>(self->list_25A4);
    for (void** node = static_cast<void**>(sentinel[0]); node != sentinel; node = static_cast<void**>(node[0])) {
        if (F<void*>(node[2], 0x10) != connection) continue;
        void* binding = node[2];
        if (!Live(binding)) return false;
        void* player = F<void*>(binding, 0xC);
        if (!Live(player)) return false;
        return F<std::uint32_t>(player, 0x24) == 3;
    }
    return false;
}
EI_REPLACE(0x00417E40, ServerPlayerStateIs3);

} // namespace net
