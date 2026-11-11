// The player data guard: while the game window is not active the players' gold, party records and unit
// statistics are XORed with a random key, and XORed back when it is. See world.h and docs/notes/world.md.
#include "game/world/world.h"

#include "game/combat/combat.h"
#include "game/players/party.h"
#include "engine/core/random.h"
#include "game/players/player.h"
#include "game/players/saved_parties.h"
#include "game/ui/ui_screens.h"   // MpHeroRecord (the party outside a game)
#include "game/units/unit.h"
#include "game/players/player_manager.h"
#include "lib/bytes.h"
#include "lib/containers.h"

#include <cstddef>
#include <cstdint>
#include <span>

namespace {


std::int8_t g_guardPlain = /*0079026C*/ 1;   // 1: data in the clear
std::uint32_t g_guardKey = /*007AFECC*/ {};
int GlobalRecordCount() { return lib::Count(GlobalParty()->members); }
int& GlobalGold() { return GlobalParty()->money; }

constexpr int kGuardedBytes = 0x704;   // a party record's stats (PartyUnitRecord::stats) or a unit's

static_assert(sizeof(UnitStats) == kGuardedBytes);

// Every dword of the stats' bytes XORed with `key`.
void XorRange(UnitStats* stats, std::uint32_t key) {
    std::span<std::byte> bytes = std::as_writable_bytes(std::span(stats, 1));
    for (std::size_t at = 0; at < bytes.size(); at += 4) {
        std::uint32_t word = lib::Load<std::uint32_t>(&bytes[at]);
        lib::Store(&bytes[at], word ^ key);
    }
}

} // namespace

void GuardPlayers(PlayerManager* players, char units, std::uint32_t key) {
    if (!players) return;
    PlayerIterator it;
    for (Player* player = it.First(players); player; player = it.Next()) {
        player->GetParty()->money ^= static_cast<int>(key);    // gold
        for (int i = 0; i < std::ssize(player->GetParty()->members); ++i) XorRange(&player->GetParty()->members[i].stats, key);
        if (!units) continue;
        // The player's units (the walk stops at an empty entry): the 0x704 bytes of their
        // controllers' statistics.
        auto& map = player->units;
        for (auto node = map.begin(); node != map.end() && node->second; ++node) {
            auto* controller = node->second->ServerController();
            XorRange(&controller->stats, key);
        }
    }
}

void GuardPlayerData(char plain) {
    if (plain == g_guardPlain) return;
    if (!plain) g_guardKey = g_random.Next();   // a new key each time the data is hidden
    GuardPlayers(g_clientWorld.players.get(), 0, g_guardKey);
    GuardPlayers(g_serverWorld.players.get(), 1, g_guardKey);
    std::uint32_t key = g_guardKey;
    GlobalGold() ^= static_cast<int>(key);
    for (int i = 0; i < GlobalRecordCount(); ++i) XorRange(&GlobalParty()->members[i].stats, key);
    g_guardPlain = plain;
}
