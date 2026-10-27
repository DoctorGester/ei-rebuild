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

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include "game_arrays.h"
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
    std::string folder = TheTextFolder().GetString();
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
    exits_.SetSize(0);
    climate_ = 1;
    cave_ = 0;
    sendsStartPosition_ = 0;
    loadedFresh_ = 1;
    keepsState_ = 1;
    zoneName_ = game::EmptyText();
    gameResult_ = 0;
    mobFiles_.SetSize(0, -1);
    return 1;
}

void CWorldServer::Destroy() {
    // Every player disconnected and released (a heap iterator).
    auto* it = gamecrt::operator_new_as<PlayerIterator>();
    if (it) it->manager = nullptr;
    if (PlayerList()) {
        for (CPlayer* player = it->First(PlayerList()); player; player = it->Next()) {
            static_cast<CPlayer*>(player)->Disconnect();
            PlayerList()->ReleasePlayer(player);
        }
    }
    if (it) {
        if (it->manager) {
            --it->manager->refCount_;
            it->manager = nullptr;
        }
        gamecrt::operator_delete(it);
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

    if (exits_.data) {
        for (int i = 0; i < exits_.size; ++i) std::destroy_at(&exits_[i].target);
        gamecrt::operator_delete(exits_.data);
        exits_.data = nullptr;
    }
    exits_.maxSize = 0;
    exits_.size = 0;
    CWorld::Destroy();
}

EI_REWRITTEN(0x0058AE10, CWorldServer::Create);
EI_REWRITTEN(0x0058B020, CWorldServer::Destroy);
