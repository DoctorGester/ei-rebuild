// Check for ServerBase::OnLoginImpl (00443080, src/game/net_lobby.cpp; A-E2). Ours only: the handler
// sends a packet and changes the joins list, so the original cannot run beside it.
//
// What must hold afterwards: with a wrong magic only the magic was read and the joins did not
// change; otherwise the id and the rate were read, the address has at most one pending join, a
// join that was there is still there, and the address's join carries the id of the packet (and,
// when it is new, an age of zero).
//
// Only a multiplayer server that receives a login reaches it, and only in the vanilla launch
// (Rebirth patches the original, so the hook is skipped there); no scenario does.
#include "net_verify.h"

#include "../../src/game/net_classes.h"
#include "../../src/game/net_coder.h"

#include <cstdint>
#include <cstring>

namespace {
void* s_unused;

// The pending join of `address` (the last one, with the number of them in `count`).
const net::PendingJoin* JoinOf(net::ServerBase* self, const net::NetAddress* address, int* count) {
    const auto& joins = *reinterpret_cast<const stlp::list<net::PendingJoin>*>(&self->joins_);
    const net::PendingJoin* found = nullptr;
    *count = 0;
    for (auto* node = joins.First(); node != joins.End(); node = node->next) {
        if (std::memcmp(&node->data.address, address, sizeof(net::NetAddress)) != 0) continue;
        found = &node->data;
        ++*count;
    }
    return found;
}

void __fastcall ServerOnLoginVerify(net::ServerBase* self, void*, const net::NetAddress* from, BufferCursor* cursor) {
    const std::uint8_t* start = cursor->p;
    std::uint32_t magic;
    std::memcpy(&magic, start, 4);
    const bool ours = magic == self->magic_;
    std::uint32_t id = 0;
    if (ours) std::memcpy(&id, start + 4, 4);
    int before = 0, after = 0;
    JoinOf(self, from, &before);

    self->ServerBase::OnLoginImpl(from, cursor);

    const net::PendingJoin* join = JoinOf(self, from, &after);
    const int read = static_cast<int>(cursor->p - start);
    bool ok;
    if (!ours)
        ok = read == 4 && after == before;
    else
        ok = read >= 12 && after <= 1 && after >= before && (!join || (join->id == id && (before == 1 || join->age == 0.0f)));
    ei::verify::Check("ServerBase::OnLogin", true, ok, "magic %08X / %08X, read %d, joins %d -> %d, id %08X / %08X", magic, self->magic_, read,
                      before, after, id, join ? join->id : 0u);
}
} // namespace

EI_VERIFY(0x00443080, ServerOnLoginVerify, 0, s_unused);
