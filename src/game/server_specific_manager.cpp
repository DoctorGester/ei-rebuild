// CServerSpecificManager and CTorchObjectSpec. See server_specific_manager.h.
#include "server_specific_manager.h"

#include "combat.h"
#include "player_messages.h"
#include "unit.h"   // CMapObjectServer

#include <new>

namespace {

// The class names are the exe's strings (ClassName returns their addresses).
constexpr const char* kManagerClassName = "CServerSpecificManager";
constexpr const char* kTorchSpecClassName = "CTorchObjectSpecific";

} // namespace


CServerSpecificManager::CServerSpecificManager() {}

CServerSpecificManager::~CServerSpecificManager() {}

const char* CServerSpecificManager::ClassName() { return kManagerClassName; }
ClassId CServerSpecificManager::GetClassId() { return kClassServerSpecificManager; }

// Every object starts zeroed (kZeroed): CUnitServer's constructor leaves waitFrame,
// which ResolveBlocker reads, and others the saves write.
CEngineObject* CServerSpecificManager::NewObject(int type) {
    switch (static_cast<SideObjectType>(type)) {
    case SideObjectType::Object3D:
        return new (kZeroed) CObject3DServer();
    case SideObjectType::MapObject:
        return new (kZeroed) CMapObjectServer();
    case SideObjectType::Unit:
        return new (kZeroed) CUnitServer();
    case SideObjectType::Player:
        return new (kZeroed) CPlayerServer();
    case SideObjectType::Torch:
        return new (kZeroed) CTorchObjectSpec();
    default:
        return nullptr;
    }
}

CTorchObjectSpec::CTorchObjectSpec() {
    ClearFields();
    torch_ = nullptr;
    CreateSpec();
}

CTorchObjectSpec::~CTorchObjectSpec() { DestroySpec(); }

const char* CTorchObjectSpec::ClassName() { return kTorchSpecClassName; }
ClassId CTorchObjectSpec::GetClassId() { return kClassTorchObjectSpec; }
int CTorchObjectSpec::RunTick() { return 1; }
