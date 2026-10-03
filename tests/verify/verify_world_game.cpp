// Checks for the game flow (src/game/world_game.cpp). Saving writes files and changes the party
// and script state, so only ours runs and the result is checked.
#include "verify.h"

#include "../../src/game/world.h"

#include <cstdio>
#include <cstring>

#include <windows.h>

namespace {
void* s_unused;
}

// 0057B3B0: a save that reports success has its scenario.sav; only single player saves.
static int __fastcall SaveGameVerify(GameFlow* self, void*, const char* slot) {
    int mode = *reinterpret_cast<const int*>(0x007B0098);
    int result = self->SaveGame(slot);
    char path[MAX_PATH];
    std::snprintf(path, sizeof(path), "%ssaves\\%s\\scenario.sav", *reinterpret_cast<char* const*>(0x007AF460), slot);
    bool exists = GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES;
    bool ok = result == 0 || (result == 1 && mode == 1 && exists);
    ei::verify::Check("GameFlow::SaveGame", true, ok, "%s -> %d (mode %d, scenario.sav %d)", slot, result, mode, exists);
    return result;
}
EI_VERIFY(0x0057B3B0, SaveGameVerify, 0, s_unused);

int __stdcall EnterZone(std::uint8_t* records);   // world_game.cpp

// 0057A060: 0 or 1 (a restored zone gives LoadSave's 1, which is 1 unless the AI graph is missing).
static int __stdcall EnterZoneVerify(std::uint8_t* records) {
    char zone[64] = "";
    std::snprintf(zone, sizeof(zone), "%s", *reinterpret_cast<const char* const*>(records + 0x68));
    int result = EnterZone(records);
    auto* world = reinterpret_cast<CWorldServer*>(0x007AFF60);
    bool ok = result == 0 || result == 1;
    ei::verify::Check("EnterZone", true, ok, "%s -> %d, map %s", zone, result, world->mapName_ ? world->mapName_ : "");
    return result;
}
EI_VERIFY(0x0057A060, EnterZoneVerify, 0, s_unused);

int __stdcall StartNetworkZone(const char* zone);   // world_game.cpp

// 0057BDF0: on success every exit of the world leads to "MPGame1".
static int __stdcall StartNetworkZoneVerify(const char* zone) {
    int result = StartNetworkZone(zone);
    auto* world = reinterpret_cast<CWorldServer*>(0x007AFF60);
    bool ok = result == 0;
    if (result == 1) {
        ok = true;
        for (int i = 0; i < world->exitCount_ && ok; ++i) ok = std::strcmp(world->exits_[i].target, "MPGame1") == 0;
    }
    ei::verify::Check("StartNetworkZone", true, ok, "%s -> %d, %d exits", zone ? zone : "", result, world->exitCount_);
    return result;
}
EI_VERIFY(0x0057BDF0, StartNetworkZoneVerify, 0, s_unused);
