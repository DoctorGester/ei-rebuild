// ServerSpecificManager and TorchObjectSpec. See server_specific_manager.h.
#include "game/server_specific_manager.h"

#include "game/combat.h"
#include "game/player_messages.h"
#include "game/unit.h"   // MapObjectServer

#include <new>

namespace {

// The class names are the exe's strings (ClassName returns their addresses).
constexpr const char* kManagerClassName = "CServerSpecificManager";
constexpr const char* kTorchSpecClassName = "CTorchObjectSpecific";

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

TorchObjectSpec::TorchObjectSpec() {
    CreateSpec();
}

TorchObjectSpec::~TorchObjectSpec() { DestroySpec(); }

const char* TorchObjectSpec::ClassName() { return kTorchSpecClassName; }
ClassId TorchObjectSpec::GetClassId() { return ClassId::TorchObjectSpec; }
bool TorchObjectSpec::RunTick() { return true; }
