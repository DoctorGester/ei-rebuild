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


char& GuardPlain() { return ei::GameData<char>(0x0079026C); }            // 1: data in the clear
std::uint32_t& GuardKey() { return ei::GameData<std::uint32_t>(0x007AFECC); }
// The players_ lists (+0x38) of the client world (007AF5B8) and the server world (007AFF60).
CEngineObject* ClientPlayers() { return ei::GameData<CEngineObject*>(0x007AF5F0); }
CEngineObject* ServerPlayers() { return ei::GameData<CEngineObject*>(0x007AFF98); }
// The party records outside a game (007C82B0, laid out like WorldPlayer + 0x70): records +0x30,
// count +0x34, gold +0x50.
PartyRecords* GlobalParty() { return reinterpret_cast<PartyRecords*>(TheSavedParties().outsideGame_); }
std::uint8_t* GlobalRecords() { return reinterpret_cast<std::uint8_t*>(GlobalParty()->records); }
int GlobalRecordCount() { return GlobalParty()->count; }
std::uint32_t& GlobalGold() { return *reinterpret_cast<std::uint32_t*>(GlobalParty()->array40 + 0x10); }

constexpr int kRecordSize = 0x784;
constexpr int kRecordGuarded = 0x20;   // the 0x704 bytes from +0x20 of a party record
constexpr int kGuardedBytes = 0x704;

void XorRange(std::uint8_t* first, std::uint32_t key) {
    auto* p = reinterpret_cast<std::uint32_t*>(first);
    auto* end = reinterpret_cast<std::uint32_t*>(first + kGuardedBytes);
    for (; p < end; ++p) *p ^= key;
}

} // namespace

void __fastcall GuardPlayers(CEngineObject* players, char units, std::uint32_t key) {
    if (!players) return;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(players); player; player = it.Next()) {
        player->Party()->Money() ^= static_cast<int>(key);   // gold
        for (int i = 0; i < player->mercCount; ++i) XorRange(player->mercs + i * kRecordSize + kRecordGuarded, key);
        if (!units) continue;
        // The player's units (the walk stops at an empty entry): the 0x704 bytes of their
        // controllers' statistics.
        auto& map = player->units.map;
        for (auto* node = map.First(); node && node->value; node = map.After(node)) {
            auto* controller = reinterpret_cast<CUnitServer*>(node->value->controller);
            XorRange(reinterpret_cast<std::uint8_t*>(&controller->stats), key);
        }
    }
}

void __fastcall GuardPlayerData(void* /*glue*/, void*, char plain) {
    if (plain == GuardPlain()) return;
    if (!plain) GuardKey() = IsaacRandom::Global().Next();   // a new key each time the data is hidden
    GuardPlayers(ClientPlayers(), 0, GuardKey());
    GuardPlayers(ServerPlayers(), 1, GuardKey());
    std::uint32_t key = GuardKey();
    GlobalGold() ^= key;
    for (int i = 0; i < GlobalRecordCount(); ++i) XorRange(GlobalRecords() + i * kRecordSize + kRecordGuarded, key);
    GuardPlain() = plain;
}

EI_REPLACE(0x005791C0, GuardPlayers);
EI_REPLACE(0x005790E0, GuardPlayerData);
