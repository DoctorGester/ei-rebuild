// The player data guard (005790E0, 005791C0): while the game window is not active the players'
// gold, party records and unit statistics are XORed with a random key, and XORed back when it is.
// See world.h and docs/notes/world.md (seventh round).
#include "world.h"

#include "random.h"

#include "ei/hook.h"

#include <cstdint>

namespace {

template <typename T>
T& At(void* object, int offset) {
    return *reinterpret_cast<T*>(static_cast<std::uint8_t*>(object) + offset);
}

IsaacRandom& Random() { return *reinterpret_cast<IsaacRandom*>(0x007A9D40); }

char& GuardPlain() { return *reinterpret_cast<char*>(0x0079026C); }            // 1: data in the clear
std::uint32_t& GuardKey() { return *reinterpret_cast<std::uint32_t*>(0x007AFECC); }
// The players_ lists (+0x38) of the client world (007AF5B8) and the server world (007AFF60).
CEngineObject* ClientPlayers() { return *reinterpret_cast<CEngineObject**>(0x007AF5F0); }
CEngineObject* ServerPlayers() { return *reinterpret_cast<CEngineObject**>(0x007AFF98); }
// The party records outside a game (007C82B0, laid out like WorldPlayer + 0x70): records +0x30,
// count +0x34, gold +0x50.
std::uint8_t* GlobalRecords() { return *reinterpret_cast<std::uint8_t**>(0x007C82E0); }
int GlobalRecordCount() { return *reinterpret_cast<int*>(0x007C82E4); }
std::uint32_t& GlobalGold() { return *reinterpret_cast<std::uint32_t*>(0x007C8300); }

constexpr int kRecordSize = 0x784;
constexpr int kRecordGuarded = 0x20;   // the 0x704 bytes from +0x20 of a party record
constexpr int kStatsGuarded = 0x1E0;   // the 0x704 bytes from +0x1E0 of a unit's controller
constexpr int kGuardedBytes = 0x704;

void XorRange(std::uint8_t* first, std::uint32_t key) {
    auto* p = reinterpret_cast<std::uint32_t*>(first);
    auto* end = reinterpret_cast<std::uint32_t*>(first + kGuardedBytes);
    for (; p < end; ++p) *p ^= key;
}

// 005791C0, through its address.
__declspec(naked) void __fastcall GuardPlayersAt(CEngineObject*, char, std::uint32_t) EI_ORIGINAL(0x005791C0)

} // namespace

void __fastcall GuardPlayers(CEngineObject* players, char units, std::uint32_t key) {
    if (!players) return;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(players); player; player = it.Next()) {
        At<std::uint32_t>(player, 0xC0) ^= key;   // gold
        for (int i = 0; i < player->mercCount; ++i) XorRange(player->mercs + i * kRecordSize + kRecordGuarded, key);
        if (!units) continue;
        // The player's units (a hash map at +0x40; the walk stops at an empty entry).
        auto& map = At<stlp::hash_map<unsigned, CEngineObject*, stlp::IntHash>>(player, 0x40);
        for (auto* node = map.First(); node && node->value; node = map.After(node))
            XorRange(At<std::uint8_t*>(node->value, 0x240) + kStatsGuarded, key);
    }
}

void __fastcall GuardPlayerData(void* /*glue*/, void*, char plain) {
    if (plain == GuardPlain()) return;
    if (!plain) GuardKey() = Random().Next();   // a new key each time the data is hidden
    GuardPlayersAt(ClientPlayers(), 0, GuardKey());
    GuardPlayersAt(ServerPlayers(), 1, GuardKey());
    std::uint32_t key = GuardKey();
    GlobalGold() ^= key;
    for (int i = 0; i < GlobalRecordCount(); ++i) XorRange(GlobalRecords() + i * kRecordSize + kRecordGuarded, key);
    GuardPlain() = plain;
}

EI_REPLACE(0x005791C0, GuardPlayers);
EI_REPLACE(0x005790E0, GuardPlayerData);
