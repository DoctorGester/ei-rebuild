// CWorldServer's own Create (Setup) and Destroy (Teardown). WinMain calls both on the global server: Setup at
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
#include <cstdint>
#include <cstring>
#include <string>

#include <memory>
#include "game_data.h"

namespace {

// The messages registry's file name.
constexpr const char* kMessagesFileName = /*0073F918*/ "SMessBase.reg";
// FatalError texts.
const char* CantCreateText() { return "CWorldServer::Create.\n Can't create base world.\n"; }
const char* CantOpenText() { return "CWorldServer::Create.\n Can't opent message base resource.\n"; }

constexpr std::uint32_t kWindStrength = 0x3ECCCCCD;   // 0.4f

} // namespace

int CWorldServer::Create() {
    Destroy();
    if (!CWorld::Create()) return game::FatalError(CantCreateText());
    std::string folder = TheTextFolder();
    BOOL opened;
    {
        std::string path = folder + kMessagesFileName;
        opened = Messages()->Open(path.c_str());
    }
    if (!opened) return game::FatalError(CantOpenText());

    manager44_ = new CServerSpecificManager();
    ++manager44_->refCount_;   // the reference is counted without testing the pointer
    manager44_->SetContext(this);
    hoover_ = new CHoover();
    ++hoover_->refCount_;

    // The wind: along x, no strength yet (three dwords), then 0.4.
    const float direction[3] = {1.0f, 0.0f, 0.0f};
    std::memcpy(windDirection_, direction, sizeof(direction));
    windStrength_ = kWindStrength;
    sessionKind_ = 0;
    scriptsActive_ = 0;
    exits_.clear();
    climate_ = 1;
    cave_ = 0;
    sendsStartPosition_ = 0;
    loadedFresh_ = 1;
    keepsState_ = 1;
    zoneName_ = game::EmptyText();
    gameResult_ = 0;
    mobFiles_.clear();
    return 1;
}

void CWorldServer::Destroy() {
    // Every player disconnected and released. The iterator drops its count of the player list when it goes out
    // of scope: before CWorld::Destroy deletes the list.
    if (PlayerList()) {
        PlayerIterator it;
        for (CPlayer* player = it.First(PlayerList()); player; player = it.Next()) {
            player->Disconnect();
            PlayerList()->ReleasePlayer(player);
        }
    }

    if (hoover_) {
        --hoover_->refCount_;
        if (hoover_) delete hoover_;   // the virtual destructor
        hoover_ = nullptr;
    }
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    ResetState();
    Messages()->Close();

    exits_ = {};
    CWorld::Destroy();
}
