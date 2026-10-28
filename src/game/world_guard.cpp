// The player data guard: while the game window is not active the players' gold, party records and unit
// statistics are XORed with a random key, and XORed back when it is. See world.h and docs/notes/world.md.
#include "world.h"

#include "combat.h"
#include "party.h"
#include "random.h"
#include "ui_screens.h"   // MpHeroRecord (the party outside a game)
#include "unit.h"

#include "game_data.h"

#include <cstdint>

namespace {


std::int8_t g_guardPlain = /*0079026C*/ 1;   // 1: data in the clear
std::uint32_t g_guardKey = /*007AFECC*/ {};
CParty* GlobalParty() { return TheSavedParties().OutsideGame()->Party(); }
int GlobalRecordCount() { return GlobalParty()->members_.size; }
int& GlobalGold() { return GlobalParty()->money_; }

constexpr int kGuardedBytes = 0x704;   // a party record's stats (PartyUnitRecord::stats) or a unit's

static_assert(sizeof(UnitStats) == kGuardedBytes);

void XorRange(UnitStats* stats, std::uint32_t key) {
    auto* p = reinterpret_cast<std::uint32_t*>(stats);
    for (std::uint32_t* end = p + kGuardedBytes / 4; p < end; ++p) *p ^= key;
}

} // namespace

void GuardPlayers(CPlayerManager* players, char units, std::uint32_t key) {
    if (!players) return;
    PlayerIterator it;
    for (CPlayer* player = it.First(players); player; player = it.Next()) {
        player->Party()->Money() ^= static_cast<int>(key);   // gold
        for (int i = 0; i < player->Party()->members_.size; ++i) XorRange(&player->Party()->members_.data[i].stats, key);
        if (!units) continue;
        // The player's units (the walk stops at an empty entry): the 0x704 bytes of their
        // controllers' statistics.
        auto& map = player->units_;
        for (auto node = map.begin(); node != map.end() && node->second; ++node) {
            auto* controller = node->second->ServerController();
            XorRange(&controller->stats, key);
        }
    }
}

void GuardPlayerData(char plain) {
    if (plain == g_guardPlain) return;
    if (!plain) g_guardKey = IsaacRandom::Global().Next();   // a new key each time the data is hidden
    GuardPlayers(TheClientWorld()->players_, 0, g_guardKey);
    GuardPlayers(TheServerWorld()->players_, 1, g_guardKey);
    std::uint32_t key = g_guardKey;
    GlobalGold() ^= static_cast<int>(key);
    for (int i = 0; i < GlobalRecordCount(); ++i) XorRange(&GlobalParty()->members_.data[i].stats, key);
    g_guardPlain = plain;
}
