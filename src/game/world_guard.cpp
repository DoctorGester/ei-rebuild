// The player data guard (005790E0, 005791C0): while the game window is not active the players'
// gold, party records and unit statistics are XORed with a random key, and XORed back when it is.
// See world.h and docs/notes/world.md.
#include "world.h"

#include "combat.h"
#include "party.h"
#include "random.h"
#include "unit.h"

#include "ei/hook.h"
#include "game_data.h"

#include <cstdint>

namespace {


std::int8_t g_guardPlain = /*0079026C*/ 1;   // 1: data in the clear
std::uint32_t g_guardKey = /*007AFECC*/ {};
// The players_ lists (+0x38) of the client world (007AF5B8) and the server world (007AFF60).
CEngineObject* ClientPlayers() { return ei::GameData<CEngineObject*>(0x007AF5F0); }
CEngineObject* ServerPlayers() { return ei::GameData<CEngineObject*>(0x007AFF98); }
// The party outside a game (007C82B0, a CParty like CPlayer + 0x70): its records, their
// count and its gold.
CParty* GlobalParty() { return reinterpret_cast<CParty*>(TheSavedParties().outsideGame_); }
int GlobalRecordCount() { return GlobalParty()->members_.size; }
std::uint32_t& GlobalGold() { return GlobalParty()->money_; }

constexpr int kGuardedBytes = 0x704;   // a party record's stats (PartyUnitRecord::stats) or a unit's

void XorRange(std::uint8_t* first, std::uint32_t key) {
    auto* p = reinterpret_cast<std::uint32_t*>(first);
    auto* end = reinterpret_cast<std::uint32_t*>(first + kGuardedBytes);
    for (; p < end; ++p) *p ^= key;
}

} // namespace

void __fastcall GuardPlayers(CEngineObject* players, char units, std::uint32_t key) {
    if (!players) return;
    PlayerIterator it;
    for (CPlayer* player = it.First(players); player; player = it.Next()) {
        player->Party()->Money() ^= static_cast<int>(key);   // gold
        for (int i = 0; i < player->Party()->members_.size; ++i) XorRange(player->Party()->members_.data[i].stats, key);
        if (!units) continue;
        // The player's units (the walk stops at an empty entry): the 0x704 bytes of their
        // controllers' statistics.
        auto& map = player->units_;
        for (auto* node = map.First(); node && node->value; node = map.After(node)) {
            auto* controller = reinterpret_cast<CUnitServer*>(node->value->controller);
            XorRange(reinterpret_cast<std::uint8_t*>(&controller->stats), key);
        }
    }
}

void __fastcall GuardPlayerData(void* /*glue*/, void*, char plain) {
    if (plain == g_guardPlain) return;
    if (!plain) g_guardKey = IsaacRandom::Global().Next();   // a new key each time the data is hidden
    GuardPlayers(ClientPlayers(), 0, g_guardKey);
    GuardPlayers(ServerPlayers(), 1, g_guardKey);
    std::uint32_t key = g_guardKey;
    GlobalGold() ^= key;
    for (int i = 0; i < GlobalRecordCount(); ++i) XorRange(GlobalParty()->members_.data[i].stats, key);
    g_guardPlain = plain;
}

EI_REWRITTEN(0x005791C0, GuardPlayers);
EI_REWRITTEN(0x005790E0, GuardPlayerData);
