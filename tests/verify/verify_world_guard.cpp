// Checks for the player data guard (src/game/world_guard.cpp).
//
// GuardPlayers XORs data with a key, so it undoes itself: ours runs, then the original with the
// same key must restore exactly what was there before (a checksum of every guarded dword), then
// ours runs again for the intended result.
#include "verify.h"

#include "../../src/game/world.h"

namespace {
void* s_unused;

using GuardPlayersFn = void(__fastcall*)(CEngineObject*, char, std::uint32_t);
GuardPlayersFn s_originalGuardPlayers;

std::uint32_t Mix(std::uint32_t hash, std::uint32_t value) { return (hash ^ value) * 16777619u; }

std::uint32_t Checksum(CEngineObject* players, char units) {
    std::uint32_t hash = 2166136261u;
    if (!players) return hash;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(players); player; player = it.Next()) {
        auto* raw = reinterpret_cast<std::uint8_t*>(player);
        hash = Mix(hash, *reinterpret_cast<std::uint32_t*>(raw + 0xC0));
        for (int i = 0; i < player->mercCount; ++i) {
            auto* p = reinterpret_cast<std::uint32_t*>(player->mercs + i * 0x784 + 0x20);
            for (int k = 0; k < 0x704 / 4; ++k) hash = Mix(hash, p[k]);
        }
        if (!units) continue;
        auto& map = *reinterpret_cast<stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>*>(raw + 0x40);
        for (auto* node = map.First(); node && node->value; node = map.After(node)) {
            auto* controller = *reinterpret_cast<std::uint8_t**>(reinterpret_cast<std::uint8_t*>(node->value) + 0x240);
            auto* p = reinterpret_cast<std::uint32_t*>(controller + 0x1E0);
            for (int k = 0; k < 0x704 / 4; ++k) hash = Mix(hash, p[k]);
        }
    }
    return hash;
}
} // namespace

// 005791C0.
static void __fastcall GuardPlayersVerify(CEngineObject* players, char units, std::uint32_t key) {
    std::uint32_t before = Checksum(players, units);
    GuardPlayers(players, units, key);
    std::uint32_t changed = Checksum(players, units);
    s_originalGuardPlayers(players, units, key);
    std::uint32_t restored = Checksum(players, units);
    GuardPlayers(players, units, key);
    ei::verify::Check("GuardPlayers", true, restored == before, "units %d key %08X: %08X -> %08X -> %08X", units, key,
                      before, changed, restored);
}
EI_VERIFY(0x005791C0, GuardPlayersVerify, 7, s_originalGuardPlayers);

// 005790E0: afterwards the guard state is `plain`.
static void __fastcall GuardPlayerDataVerify(void* glue, void*, char plain) {
    GuardPlayerData(glue, nullptr, plain);
    char state = *reinterpret_cast<char*>(0x0079026C);
    ei::verify::Check("GuardPlayerData", true, state == plain, "plain %d state %d", plain, state);
}
EI_VERIFY(0x005790E0, GuardPlayerDataVerify, 0, s_unused);
