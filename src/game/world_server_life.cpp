// WorldServer's own Create (Setup) and Destroy (Teardown). WinMain calls both on the global server: Setup at
// start-up, Teardown at exit. See world.h and docs/notes/world.md.
#include "player_messages.h"
#include "world.h"
#include "world_data.h"

#include "base_manager.h"
#include "game_strings.h"
#include "object.h"
#include "player_manager.h"
#include "registry.h"
#include "server_specific_manager.h"

#include "lib/crt.h"
#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

#include <memory>

namespace {

// The messages registry's file name.
constexpr const char* kMessagesFileName = /*0073F918*/ "SMessBase.reg";
// FatalError texts.
const char* CantCreateText() { return "CWorldServer::Create.\n Can't create base world.\n"; }
const char* CantOpenText() { return "CWorldServer::Create.\n Can't opent message base resource.\n"; }

constexpr float kWindStrength = 0.4f;

} // namespace

void WorldServer::Create() {
    Destroy();
    CWorld::Create();
    std::string folder = g_textFolder;
    bool opened;
    {
        std::string path = folder + kMessagesFileName;
        opened = messages.Open(path);
    }
    if (!opened) {
        game::FatalError(CantOpenText());
        return;
    }

    specificManager = new ServerSpecificManager();
    specificManager->AddRef();    // the reference is counted without testing the pointer
    specificManager->SetContext(this);
    hoover = new Hoover();
    hoover->AddRef();

    // The wind: along x, no strength yet (three dwords), then 0.4.
    const float direction[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(windDirection, direction, sizeof(direction));
    windStrength = std::bit_cast<std::uint32_t>(kWindStrength);
    sessionKind = 0;
    scriptsActive = 0;
    exits.clear();
    climate = 1;
    cave = 0;
    loadedFresh = 1;
    keepsState = 1;
    zoneName = game::kEmptyText;
    gameResult = 0;
    mobFiles.clear();
}

void WorldServer::Destroy() {
    // Every player disconnected and released. The iterator drops its count of the player list when it goes out
    // of scope: before CWorld::Destroy deletes the list.
    if (PlayerList()) {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
            player->Disconnect();
            PlayerList()->ReleasePlayer(player);
        }
    }

    if (hoover) {
        --hoover->refCount;
        if (hoover) delete hoover;     // the virtual destructor
        hoover = nullptr;
    }
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    ResetState();
    messages.Close();

    exits = {};
    CWorld::Destroy();
}
