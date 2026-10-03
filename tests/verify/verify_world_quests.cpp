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
