// CServerSpecificManager and CTorchObjectSpec. See server_specific_manager.h.
#include "combat.h"
#include "server_specific_manager.h"

#include "unit.h"   // CMapObjectServer
#include "player_messages.h"

#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

// The class names are the exe's strings (GetClassName returns their addresses).
constexpr const char* kManagerClassName = "CServerSpecificManager";
constexpr const char* kTorchSpecClassName = "CTorchObjectSpecific";

enum ServerObjectType {
    kObject3DServer = 0x46,
    kMapObjectServer = 0x47,
    kUnitServer = 0x48,
    kPlayerServer = 0x49,
    kTorchObjectSpec = 0x4B,
};


} // namespace


CServerSpecificManager::CServerSpecificManager() {}

CServerSpecificManager::~CServerSpecificManager() {}

const char* CServerSpecificManager::GetClassName() { return kManagerClassName; }
ClassId CServerSpecificManager::GetClassId() { return kClassServerSpecificManager; }

// Every object starts zeroed (kZeroed): CUnitServer's constructor leaves waitFrame,
// which ResolveBlocker reads, and others the saves write.
CEngineObject* CServerSpecificManager::NewObject(int type) {
    switch (type) {
    case kObject3DServer:
        return new (kZeroed) CObject3DServer();
    case kMapObjectServer:
        return new (kZeroed) CMapObjectServer();
    case kUnitServer:
        return new (kZeroed) CUnitServer();
    case kPlayerServer:
        return new (kZeroed) CPlayerServer();
    case kTorchObjectSpec:
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

const char* CTorchObjectSpec::GetClassName() { return kTorchSpecClassName; }
ClassId CTorchObjectSpec::GetClassId() { return kClassTorchObjectSpec; }
int CTorchObjectSpec::RunTick() { return 1; }
