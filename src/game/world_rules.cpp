// Sorting predicates used by the world code and the game interface glue functions. See world.h.
#include "world.h"

#include "base_manager.h"
#include "core_maths.h"
#include "link_figure.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"
#include "player_client.h"
#include "texture.h"

#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <memory>
#include <new>

namespace {

// The local player record through the client's local view (no null checks).
CPlayer* LocalPlayer() { return g_clientWorld->LocalView()->player_; }

// A player's named float variable (its party's variables_).
float Variable(CPlayer* player, std::string_view name) { return player->Party()->variables_.Get(name); }

} // namespace

// ---- Sorting predicates --------------------------------------------------------------------------

bool DepthLess::Less(const ScreenEntry* a, const ScreenEntry* b) const {
    CMapObject* first = manager->FindMapObject(a->objectId);
    CMapObject* second = manager->FindMapObject(b->objectId);
    if (!first || !second) return false;
    // The matrix's row 2: the depth axis and its offset.
    const Vector3 normal{view[8], view[9], view[10]};
    float offset = view[11];
    float da = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(first->localPosition_, normal) + offset);
    float db = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(second->localPosition_, normal) + offset);
    // Living objects come before dead ones; within each kind, by depth. The game asks each object again at
    // every step, so the calls are repeated here in the same order.
    if ((first->IsDead() == 0 && second->IsDead() == 0) ||
        (first->IsDead() != 0 && second->IsDead() != 0)) {
        EI_NAN_AUDIT(da, db);
        return da < db;
    }
    return first->IsDead() == 0;
}

// ---- Game interface glue -------------------------------------------------------------------------

bool IsMultiplayerClient() {
    net::ClientSession* client = net::g_gameClient;
    if (client && !(client->flags7_ & CRefCounted::kDetached)) return true;
    return false;
}

float GameTime() {
    CWorldClient* client = &g_clientWorld.Get();
    return static_cast<float>(client->tick_) + client->tickFraction_;
}

CParty* LocalPlayerData() { return LocalPlayer()->Party(); }

std::string LocalPlayerZone() { return LocalPlayer()->Party()->zone_; }

float LocalPlayerVariable(std::string_view name) { return Variable(LocalPlayer(), name); }

float PlayerZeroVariable(std::string_view name) {
    if (IsMultiplayerClient()) return Variable(LocalPlayer(), name);
    CPlayer* player = g_serverWorld->GetPlayer(0);
    if (!player) return 0.0f;
    return Variable(player, name);
}
