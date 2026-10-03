// Checks for the server's quest variables (src/game/world_quests.cpp). Setting a variable goes
// through the player state (and to the network), so only ours runs; afterwards every player's
// briefing variables for a slot have at most one of n1 / n3 / n9 set.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstdio>

namespace {
void* s_unused;

__declspec(naked) double __fastcall VariableGet(void*, void*, const char*) {
    __asm mov eax, 0x00460AF0
    __asm jmp eax
}
} // namespace

// 00591800.
static void __fastcall UpdateMercBriefingsVerify(CWorldServer* self, void*) {
    self->UpdateMercBriefings();
    bool ok = true;
    int players = 0;
    PlayerIterator it;
    for (WorldPlayer* player = it.First(self->players_); player && ok; player = it.Next(), ++players) {
        for (int slot = 1; slot <= 10 && ok; ++slot) {
            char name[64];
            int set = 0;
            for (const char* part : {"n1", "n3", "n9"}) {
                std::snprintf(name, sizeof(name), "b.merc%d.%s_%d", slot, part, slot);
                if (VariableGet(player->variables, nullptr, name) == 1.0) ++set;
            }
            ok = set <= 1;
        }
    }
    ei::verify::Check("CWorldServer::UpdateMercBriefings", true, ok, "%d players", players);
}
EI_VERIFY(0x00591800, UpdateMercBriefingsVerify, 0, s_unused);

// 00592090: gives rewards and queues a script line; afterwards no player's gold went down.
static void __fastcall CompleteBriefingVerify(CWorldServer* self, void*, int playerId, const char* name) {
    WorldPlayer* player = self->GetPlayer(playerId);
    int before = player ? *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(player) + 0xC0) : 0;
    self->CompleteBriefing(playerId, name);
    int after = player ? *reinterpret_cast<int*>(reinterpret_cast<std::uint8_t*>(player) + 0xC0) : 0;
    ei::verify::Check("CWorldServer::CompleteBriefing", true, after >= before, "player %d %s gold %d -> %d", playerId,
                      name ? name : "", before, after);
}
EI_VERIFY(0x00592090, CompleteBriefingVerify, 0, s_unused);

// 005911C0: the experience goes to units and records through original code; counted only (the
// player's own counter is not a field of the world).
static void __fastcall AwardExperienceVerify(CWorldServer* self, void*, WorldPlayer* player, float amount) {
    self->AwardExperience(player, amount);
    ei::verify::Check("CWorldServer::AwardExperience", true, true, "");
}
EI_VERIFY(0x005911C0, AwardExperienceVerify, 0, s_unused);

// 00590C70: the records' list counts stay sane (each list's count not negative).
static void __fastcall SyncPartyRecordsVerify(CWorldServer* self, void*, WorldPlayer* player) {
    self->SyncPartyRecords(player);
    bool ok = true;
    for (int i = 0; i < player->mercCount && ok; ++i) {
        std::uint8_t* record = player->mercs + i * 0x784;
        for (int offset : {0x734, 0x744, 0x754, 0x764}) ok = ok && *reinterpret_cast<int*>(record + offset) >= 0;
    }
    ei::verify::Check("CWorldServer::SyncPartyRecords", true, ok, "player %d, %d records", player->id, player->mercCount);
}
EI_VERIFY(0x00590C70, SyncPartyRecordsVerify, 0, s_unused);
