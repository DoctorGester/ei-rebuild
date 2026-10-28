// CServerSpecificManager and CTorchObjectSpec. See server_specific_manager.h.
#include "combat.h"
#include "server_specific_manager.h"

#include "unit.h"   // CMapObjectServer
#include "player_messages.h"

#include "lib/crt.h"

#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

// The class names are the exe's strings (GetClassName returns their addresses).
constexpr const char* kManagerClassName = "CServerSpecificManager";
constexpr const char* kTorchSpecClassName = "CTorchObjectSpecific";
constexpr int kManagerClassId = 0x31;
constexpr int kTorchSpecClassId = 0x42;

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
int CServerSpecificManager::GetClassId() { return kManagerClassId; }

// Every object starts zeroed (operator_new_zeroed): CUnitServer's constructor leaves waitFrame,
// which ResolveBlocker reads, and others the saves write.
CEngineObject* CServerSpecificManager::NewObject(int type) {
    switch (type) {
    case kObject3DServer:
        if (void* p = gamecrt::operator_new_zeroed(sizeof(CObject3DServer))) return new (p) CObject3DServer();
        return nullptr;
    case kMapObjectServer:
        if (void* p = gamecrt::operator_new_zeroed(sizeof(CMapObjectServer))) return new (p) CMapObjectServer();
        return nullptr;
    case kUnitServer:
        if (void* p = gamecrt::operator_new_zeroed(sizeof(CUnitServer))) return new (p) CUnitServer();
        return nullptr;
    case kPlayerServer:
        if (void* p = gamecrt::operator_new_zeroed(sizeof(CPlayerServer))) return new (p) CPlayerServer();
        return nullptr;
    case kTorchObjectSpec:
        if (void* p = gamecrt::operator_new_zeroed(sizeof(CTorchObjectSpec))) return new (p) CTorchObjectSpec();
        return nullptr;
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
int CTorchObjectSpec::GetClassId() { return kTorchSpecClassId; }
int CTorchObjectSpec::RunTick() { return 1; }
