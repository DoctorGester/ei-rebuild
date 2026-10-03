// Checks for the world's per-player synchronisation (src/game/world_sync.cpp). Both functions send
// messages to the player state (network or local), so only ours runs and the state is checked.
#include "verify.h"

#include "../../src/game/world.h"

namespace {
void* s_unused;

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

// SyncPlayer's function static of collected objects ({data, size, capacity, growBy} at 007AFF48).
struct StaticPtrArray {
    CEngineObject** data;
    int size;
    int capacity;
    int growBy;
};
StaticPtrArray& Collected() { return *reinterpret_cast<StaticPtrArray*>(0x007AFF48); }
} // namespace

// 0058B750: the collection array is consistent afterwards (size within capacity, data when not
// empty, no null entry), and a leaving player (state mode 3) gives 1.
static int __fastcall SyncPlayerVerify(CWorldServer* self, void*, WorldPlayer* player) {
    int result = self->SyncPlayer(player);
    const StaticPtrArray& a = Collected();
    bool ok = a.size >= 0 && a.size <= a.capacity && (a.size == 0 || a.data != nullptr);
    for (int i = 0; ok && i < a.size; ++i) ok = a.data[i] != nullptr;
    int mode = At<int>(At<void*>(player, 0x20), 0x20);
    if (mode == 3) ok = ok && result == 1;
    ei::verify::Check("CWorldServer::SyncPlayer", true, ok, "player %d mode %d result %d, collected %d of %d", player->id,
                      mode, result, a.size, a.capacity);
    return result;
}
EI_VERIFY(0x0058B750, SyncPlayerVerify, 0, s_unused);

// 0058C890: 1 for a player that is not playing (nothing sent), otherwise the state's answer.
static int __fastcall SendZoneVerify(CWorldServer* self, void*, WorldPlayer* player) {
    int mode = At<int>(At<void*>(player, 0x20), 0x20);
    int result = self->SendZone(player);
    bool ok = mode == 1 || result == 1;
    ei::verify::Check("CWorldServer::SendZone", true, ok, "player %d mode %d result %d", player->id, mode, result);
    return result;
}
EI_VERIFY(0x0058C890, SendZoneVerify, 0, s_unused);
