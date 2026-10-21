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
#include "texture.h"

#include "ei/hook.h"
#include "originals_engine.h"
#include "game_singletons.h"
#include "lib/nan_audit.h"

#include <bit>
#include <cstring>
#include <new>

namespace {

// The local player record through the client's local view (no null checks, as the original).
WorldPlayer* LocalPlayer() { return reinterpret_cast<LocalViewData*>(TheClientWorld()->LocalView())->player; }

// 00460AF0: a player's named float variable (WorldPlayer::variables).
float Variable(WorldPlayer* player, const char* name) { return player->variables.Get(name); }

CMapObject* MapObject(CEngineObject* object) { return static_cast<CMapObject*>(object); }
CObject3D* Object3D(CEngineObject* object) { return static_cast<CObject3D*>(object); }

// The client's list of the server's variables: STLport list nodes holding the message.
using VariableNode = stlp::list<net::VariableMessage*>::Node;

} // namespace

// ---- Sorting predicates --------------------------------------------------------------------------

bool DepthLess::Less(const int* a, const int* b) {
    auto* registry = static_cast<CBaseManager*>(manager);
    CEngineObject* first = registry->FindObject(static_cast<unsigned>(a[1]));
    CEngineObject* second = registry->FindObject(static_cast<unsigned>(b[1]));
    if (!first || !second) return false;
    // The plane's normal at +0x20, its offset at +0x2C.
    const Vector3& normal = *reinterpret_cast<const Vector3*>(plane + 8);
    float offset = plane[11];
    const CMapObject* p = MapObject(first);
    const CMapObject* q = MapObject(second);
    float da = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(p->position, normal) + offset);
    float db = static_cast<float>(DotUnrounded<Sum3::ZYX, double>(q->position, normal) + offset);
    // Living objects come before dead ones; within each kind, by depth. The original asks each
    // object again at every step, so the calls are repeated here in the same order.
    if ((ObjectSlots(first)->IsDead() == 0 && ObjectSlots(second)->IsDead() == 0) ||
        (ObjectSlots(first)->IsDead() != 0 && ObjectSlots(second)->IsDead() != 0)) {
        EI_NAN_AUDIT(da, db);
        return da < db;
    }
    return ObjectSlots(first)->IsDead() == 0;
}

namespace {

// The first part set's texture of a drawn object's model, null without one.
const CTexture* RankTexture(CObject3DClientSpecific* model) {
    const TextureParts* rank = model->partSets[0];
    return rank ? rank->texture : nullptr;
}

// The texture's and the figure's order without their addresses (address_order::DrawBefore): null
// first, then by name, a figure then by its morph weights (their bits); 0 for the same keys.
int CompareTextures(const CTexture* a, const CTexture* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    const char* aName = a->name_;
    const char* bName = b->name_;
    return std::strcmp(aName ? aName : "", bName ? bName : "");
}

int CompareFigures(const CLinkMesh* a, const CLinkMesh* b) {
    if (!a || !b) return (a != nullptr) - (b != nullptr);
    if (a->name_ < b->name_) return -1;
    if (b->name_ < a->name_) return 1;
    for (int i = 0; i < 3; ++i) {
        const auto wa = std::bit_cast<std::uint32_t>(a->morphWeights_[i]);
        const auto wb = std::bit_cast<std::uint32_t>(b->morphWeights_[i]);
        if (wa != wb) return wa < wb ? -1 : 1;
    }
    return 0;
}

bool DepthBefore(CEngineObject* a, CEngineObject* b) {
    EI_NAN_AUDIT(Object3D(a)->viewPosition.z, Object3D(b)->viewPosition.z);
    return Object3D(a)->viewPosition.z < Object3D(b)->viewPosition.z;
}

} // namespace

// By the first part set's texture (its address), then the figure (its address), then the view depth.
bool UnitOrderLess::LessByAddress(CEngineObject* a, CEngineObject* b) {
    if (!a || !b) return false;
    auto* groupA = static_cast<CObject3DClientSpecific*>(Object3D(a)->attached);
    auto* groupB = static_cast<CObject3DClientSpecific*>(Object3D(b)->attached);
    if (!groupA || !groupB) return false;
    const auto ra = reinterpret_cast<std::uint32_t>(RankTexture(groupA));
    const auto rb = reinterpret_cast<std::uint32_t>(RankTexture(groupB));
    if (ra < rb) return true;   // the original's `rankB && ra < rb`: rb is 0 without a rank
    if (ra != rb) return false;
    auto sa = reinterpret_cast<std::uint32_t>(Object3D(a)->figure);
    auto sb = reinterpret_cast<std::uint32_t>(Object3D(b)->figure);
    if (sa < sb) return true;
    if (sa != sb) return false;
    return DepthBefore(a, b);
}

bool address_order::DrawBefore(CEngineObject* a, CEngineObject* b) {
    if (!a || !b) return false;
    auto* groupA = static_cast<CObject3DClientSpecific*>(Object3D(a)->attached);
    auto* groupB = static_cast<CObject3DClientSpecific*>(Object3D(b)->attached);
    if (!groupA || !groupB) return false;
    if (int c = CompareTextures(RankTexture(groupA), RankTexture(groupB))) return c < 0;
    if (int c = CompareFigures(Object3D(a)->figure, Object3D(b)->figure)) return c < 0;
    return DepthBefore(a, b);
}

bool UnitOrderLess::Less(CEngineObject* a, CEngineObject* b) {
    return address_order::ById() ? address_order::DrawBefore(a, b) : LessByAddress(a, b);
}

BOOL EntryIdLess::Less(const std::uint8_t* a, const std::uint8_t* b) {
    return reinterpret_cast<const VariableNode*>(a)->data->serial_ < reinterpret_cast<const VariableNode*>(b)->data->serial_;
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

std::uint8_t* LocalPlayerData() { return LocalPlayer()->party; }

mfc::CString* __stdcall LocalPlayerName(mfc::CString* out) {
    return new (out) mfc::CString(LocalPlayer()->Party()->zone_);
}

float __stdcall LocalPlayerVariable(const char* name) { return Variable(LocalPlayer(), name); }

float __stdcall PlayerZeroVariable(const char* name) {
    if (IsMultiplayerClient()) return Variable(LocalPlayer(), name);
    WorldPlayer* player = TheServerWorld()->GetPlayer(0);
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
