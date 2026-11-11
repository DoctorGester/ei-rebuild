// The side-specific managers, ServerSpecificManager and ClientSpecificManager. See objects/side_managers.h.
#include "game/objects/side_managers.h"

#include "game/combat/combat.h"
#include "engine/models/link_figure.h"
#include "game/objects/map_object_shadow.h"
#include "game/players/player_client.h"
#include "game/players/player_messages.h"
#include "game/players/player.h"
#include "game/objects/torch.h"
#include "game/units/unit.h"   // MapObjectServer
#include "game/units/unit_client_models.h"

#include <new>

namespace {

// The class names are the exe's strings (ClassName returns their addresses).
constexpr const char* kManagerClassName = "CServerSpecificManager";

} // namespace


ServerSpecificManager::ServerSpecificManager() {}

ServerSpecificManager::~ServerSpecificManager() {}

const char* ServerSpecificManager::ClassName() { return kManagerClassName; }
ClassId ServerSpecificManager::GetClassId() { return ClassId::ServerSpecificManager; }

EngineObject* ServerSpecificManager::NewObject(int objectType) {
    switch (static_cast<SideObjectType>(objectType)) {
    case SideObjectType::Object3D:
        return new Object3DServer();
    case SideObjectType::MapObject:
        return new MapObjectServer();
    case SideObjectType::Unit:
        return new UnitServer();
    case SideObjectType::Player:
        return new PlayerServer();
    case SideObjectType::Torch:
        return new TorchObjectSpec();
    default:
        return nullptr;
    }
}

ClientSpecificManager::ClientSpecificManager() {}
ClientSpecificManager::~ClientSpecificManager() {}
const char* ClientSpecificManager::ClassName() { return "CClientSpecificManager"; }
ClassId ClientSpecificManager::GetClassId() { return ClassId::ClientSpecificManager; }

EngineObject* ClientSpecificManager::NewObject(int objectType) {
    switch (static_cast<SideObjectType>(objectType)) {
    case SideObjectType::Drawer: return new FigureMeshDrawer();
    case SideObjectType::Object3D: return new Object3DClientSpecific();
    case SideObjectType::MapObject: return new MapObjectClientSpecific();
    case SideObjectType::Unit: return new UnitClientSpecific();
    case SideObjectType::Player: return new PlayerClient();
    case SideObjectType::Torch: return new TorchObjectClientSpec();
    default: return nullptr;
    }
}
