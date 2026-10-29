// ServerSpecificManager and TorchObjectSpec. See server_specific_manager.h.
#include "server_specific_manager.h"

#include "combat.h"
#include "player_messages.h"
#include "unit.h"   // MapObjectServer

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

// Every object starts zeroed (kZeroed): UnitServer's constructor leaves waitFrame,
// which ResolveBlocker reads, and others the saves write.
EngineObject* ServerSpecificManager::NewObject(int objectType) {
    switch (static_cast<SideObjectType>(objectType)) {
    case SideObjectType::Object3D:
        return new (kZeroed) Object3DServer();
    case SideObjectType::MapObject:
        return new (kZeroed) MapObjectServer();
    case SideObjectType::Unit:
        return new (kZeroed) UnitServer();
    case SideObjectType::Player:
        return new (kZeroed) PlayerServer();
    case SideObjectType::Torch:
        return new (kZeroed) TorchObjectSpec();
    default:
        return nullptr;
    }
}

TorchObjectSpec::TorchObjectSpec() {
    torch = nullptr;
    CreateSpec();
}

TorchObjectSpec::~TorchObjectSpec() { DestroySpec(); }

const char* TorchObjectSpec::ClassName() { return kTorchSpecClassName; }
ClassId TorchObjectSpec::GetClassId() { return ClassId::TorchObjectSpec; }
bool TorchObjectSpec::RunTick() { return true; }
