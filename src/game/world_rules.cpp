// Sorting predicates used by the world code and the game interface glue functions. See world.h.
#include "world.h"

#include "base_manager.h"
#include "core_maths.h"
#include "map_object.h"
#include "map_object_figures.h"
#include "map_object_shadow.h"
#include "net_classes.h"
#include "net_messages.h"
#include "party.h"

#include "ei/hook.h"
#include "originals_engine.h"

#include <new>

namespace {

// The local player record through the client's local view (no null checks, as the original).
WorldPlayer* LocalPlayer() { return reinterpret_cast<LocalViewData*>(WorldClient()->LocalView())->player; }

// 00460AF0: a player's named float variable (WorldPlayer::variables).
float Variable(WorldPlayer* player, const char* name) { return NamedTableGet(player->variables, nullptr, name); }

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
    float da = StoreFloat(static_cast<float>(DotUnrounded<Sum3::ZYX, double>(p->position, normal) + offset));
    float db = StoreFloat(static_cast<float>(DotUnrounded<Sum3::ZYX, double>(q->position, normal) + offset));
    // Living objects come before dead ones; within each kind, by depth. The original asks each
    // object again at every step, so the calls are repeated here in the same order.
    if ((ObjectSlots(first)->IsDead() == 0 && ObjectSlots(second)->IsDead() == 0) ||
        (ObjectSlots(first)->IsDead() != 0 && ObjectSlots(second)->IsDead() != 0))
        return !(da >= db);
    return ObjectSlots(first)->IsDead() == 0;
}

// By the first part set's texture (its address), then the figure (its address), then the view depth.
bool UnitOrderLess::Less(CEngineObject* a, CEngineObject* b) {
    if (!a || !b) return false;
    auto* groupA = static_cast<CObject3DClientSpecific*>(Object3D(a)->attached);
    auto* groupB = static_cast<CObject3DClientSpecific*>(Object3D(b)->attached);
    if (!groupA || !groupB) return false;
    const TextureParts* rankA = groupA->partSets[0];
    const TextureParts* rankB = groupB->partSets[0];
    std::uint32_t ra = rankA ? reinterpret_cast<std::uint32_t>(rankA->texture) : 0;
    std::uint32_t rb = rankB ? reinterpret_cast<std::uint32_t>(rankB->texture) : 0;
    if (rankB && ra < rb) return true;
    if (ra != rb) return false;
    auto sa = reinterpret_cast<std::uint32_t>(Object3D(a)->figure);
    auto sb = reinterpret_cast<std::uint32_t>(Object3D(b)->figure);
    if (sa < sb) return true;
    if (sa != sb) return false;
    return !(Object3D(a)->viewPosition.z >= Object3D(b)->viewPosition.z);
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
    CWorldClient* client = WorldClient();
    return static_cast<float>(client->tick_) + client->tickFraction_;
}

std::uint8_t* LocalPlayerData() { return LocalPlayer()->party; }

mfc::CString* __stdcall LocalPlayerName(mfc::CString* out) {
    return new (out) mfc::CString(LocalPlayer()->Party()->zone_);
}

float __stdcall LocalPlayerVariable(const char* name) { return Variable(LocalPlayer(), name); }

float __stdcall PlayerZeroVariable(const char* name) {
    if (IsMultiplayerClient()) return Variable(LocalPlayer(), name);
    WorldPlayer* player = WorldServer()->GetPlayer(0);
    if (!player) return 0.0f;
    return Variable(player, name);
}

EI_REPLACE_METHOD(0x00573A60, DepthLess::Less);
EI_REPLACE_METHOD(0x00574300, UnitOrderLess::Less);
EI_REPLACE_METHOD(0x00589580, EntryIdLess::Less);
EI_REPLACE(0x0057F440, IsMultiplayerClient);
EI_REPLACE(0x00578B40, GameTime);
EI_REPLACE(0x00578BA0, LocalPlayerData);
EI_REPLACE(0x00578A90, LocalPlayerName);
EI_REPLACE(0x00578B50, LocalPlayerVariable);
EI_REPLACE(0x00578BE0, PlayerZeroVariable);
