// Sorting predicates used by the world code and the game interface glue functions. See world.h.
#include "world.h"

#include "address_order.h"
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

#include "ei/hook.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <memory>
#include <new>

namespace {

// The local player record through the client's local view (no null checks).
CPlayer* LocalPlayer() { return TheClientWorld()->LocalView()->player_; }

// A player's named float variable (its party's variables_).
float Variable(CPlayer* player, const char* name) { return player->Party()->variables_.Get(name); }

} // namespace

// ---- Sorting predicates --------------------------------------------------------------------------

bool DepthLess::Less(const ScreenEntry* a, const ScreenEntry* b) {
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

// Texture and figure by name, then the view depth (address_order::DrawBefore, address_order.cpp).
bool UnitOrderLess::Less(CEngineObject* a, CEngineObject* b) { return address_order::DrawBefore(a, b); }

BOOL EntryIdLess::Less(const stlp::list<RefPtr>::Node* a, const stlp::list<RefPtr>::Node* b) {
    using Variables = stlp::CountedRefListOf<net::VariableMessage>;
    return Variables::At(a)->serial_ < Variables::At(b)->serial_;
}

// ---- Game interface glue -------------------------------------------------------------------------

BOOL IsMultiplayerClient() {
    net::ClientSession* client = net::GameClient();
    if (client && !(client->flags7_ & CRefCounted::kDetached)) return 1;
    return 0;
}

float GameTime() {
    CWorldClient* client = TheClientWorld();
    return static_cast<float>(client->tick_) + client->tickFraction_;
}

CParty* LocalPlayerData() { return LocalPlayer()->Party(); }

mfc::CString* __stdcall LocalPlayerName(mfc::CString* out) {
    return std::construct_at(out, LocalPlayer()->Party()->zone_);
}

float __stdcall LocalPlayerVariable(const char* name) { return Variable(LocalPlayer(), name); }

float __stdcall PlayerZeroVariable(const char* name) {
    if (IsMultiplayerClient()) return Variable(LocalPlayer(), name);
    CPlayer* player = TheServerWorld()->GetPlayer(0);
    if (!player) return 0.0f;
    return Variable(player, name);
}

EI_REWRITTEN(0x00573A60, DepthLess::Less);
EI_REWRITTEN(0x00574300, UnitOrderLess::Less);
EI_REWRITTEN(0x00589580, EntryIdLess::Less);
EI_REWRITTEN(0x0057F440, IsMultiplayerClient);
EI_REWRITTEN(0x00578B40, GameTime);
EI_REWRITTEN(0x00578BA0, LocalPlayerData);
EI_REWRITTEN(0x00578A90, LocalPlayerName);
EI_REWRITTEN(0x00578B50, LocalPlayerVariable);
EI_REWRITTEN(0x00578BE0, PlayerZeroVariable);
