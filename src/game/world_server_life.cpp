// CWorldServer's own slots 2 and 3: Setup (0058AE10, Create) and Teardown (0058B020, Destroy).
// WinMain calls both on the global server (007AFF60): Setup at start-up, Teardown at exit. See
// world.h and docs/notes/world.md.
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

int CWorldServer::Setup() {
    Destroy();
    if (!WorldCreateManagers(this)) return game::FatalError(CantCreateText());
    mfc::CString folder(TheTextFolder());
    BOOL opened;
    {
        mfc::CString path = folder + kMessagesFileName;
        opened = Messages()->Open(path);
    }
    if (!opened) return game::FatalError(CantOpenText());

    auto* specific = gamecrt::operator_new_as<CServerSpecificManager>();
    manager44_ = specific ? specific->Construct() : nullptr;
    ++manager44_->refCount_;   // the original counts the reference without testing the pointer
    manager44_->SetContext(this);
    auto* hoover = gamecrt::operator_new_as<CHoover>();
    hoover_ = hoover ? hoover->Construct() : nullptr;
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

void CWorldServer::Teardown() {
    // Every player disconnected and released (a heap iterator, as the original allocates it).
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
        if (hoover_) delete hoover_;   // slot 0 with 1 (the virtual destructor)
        hoover_ = nullptr;
    }
    ScriptReset(MainScript());
    ScriptReset(SecondScript());
    ResetState();
    Messages()->Close();

    // The exits' storage freed (each record's target string destroyed), as SetSize(0) inlined.
    if (exits_.data) {
        for (int i = 0; i < exits_.size; ++i) exits_[i].target.~CString();
        gamecrt::operator_delete(exits_.data);
        exits_.data = nullptr;
    }
    exits_.maxSize = 0;
    exits_.size = 0;
    DestroyManagers();
}

EI_REWRITTEN(0x0058AE10, CWorldServer::Setup);
EI_REWRITTEN(0x0058B020, CWorldServer::Teardown);
