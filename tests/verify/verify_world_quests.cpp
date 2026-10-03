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

// 0058FD90: afterwards no record's unit is still a living world object (they were removed).
static void __fastcall StorePartyVerify(CWorldServer* self, void*, void* records, void* party, void* placements) {
    struct Array {
        void* vtable;
        std::uint8_t** data;
        int size;
    };
    auto* list = static_cast<Array*>(records);
    std::uint8_t* before[64] = {};
    int n = list->size < 64 ? list->size : 64;
    for (int i = 0; i < n; ++i) before[i] = list->data[i];
    self->StoreParty(records, party, placements);
    bool ok = true;
    for (int i = 0; i < n && ok; ++i) {
        CEngineObject* unit = self->FindObject(*reinterpret_cast<unsigned*>(before[i] + 0x1C));
        ok = !unit || unit->GetClassId() != world::kClassUnit ||
             *reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(unit) + 0x24C) == nullptr;
    }
    ei::verify::Check("CWorldServer::StoreParty", true, ok, "%d records", list->size);
}
EI_VERIFY(0x0058FD90, StorePartyVerify, 0, s_unused);

// 00590A40: afterwards the player's records all name units that exist or were just stored.
static void __fastcall CollectPartyVerify(CWorldServer* self, void*, WorldPlayer* player, void* placements) {
    int before = player ? player->mercCount : 0;
    self->CollectParty(player, placements);
    int after = player ? player->mercCount : 0;
    ei::verify::Check("CWorldServer::CollectParty", true, after <= before && after >= 0, "records %d -> %d", before, after);
}
EI_VERIFY(0x00590A40, CollectPartyVerify, 0, s_unused);
