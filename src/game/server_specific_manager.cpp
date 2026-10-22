// CServerSpecificManager and CTorchObjectSpec (A-S7, 004D6900-004D6BA0). See server_specific_manager.h.
#include "combat.h"
#include "server_specific_manager.h"

#include "unit.h"   // CMapObjectServer
#include "player_messages.h"

#include "ei/hook.h"
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

// --- CServerSpecificManager ---------------------------------------------------------------------

CServerSpecificManager::CServerSpecificManager() {}
EI_REWRITTEN_SPECIAL(0x004D6900, CServerSpecificManager::CServerSpecificManager);

CServerSpecificManager::~CServerSpecificManager() {}
EI_REWRITTEN_SPECIAL(0x004D6960, CServerSpecificManager::~CServerSpecificManager);
EI_REWRITTEN_SPECIAL(0x004D6940, CServerSpecificManager::~CServerSpecificManager);

const char* CServerSpecificManager::GetClassName() { return kManagerClassName; }
int CServerSpecificManager::GetClassId() { return kManagerClassId; }

// Every object starts zeroed (operator_new_zeroed): CUnitServer's constructor leaves waitFrame (+0x93C),
// which ResolveBlocker reads, and others the saves write (SV7d3, SV7b2).
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

EI_REWRITTEN(0x004D6920, CServerSpecificManager::GetClassName);
EI_REWRITTEN(0x004D6930, CServerSpecificManager::GetClassId);
EI_REWRITTEN(0x004D6970, CServerSpecificManager::NewObject);

// --- CTorchObjectSpec ---------------------------------------------------------------------------

CTorchObjectSpec::CTorchObjectSpec() {
    ClearFields();
    torch_ = nullptr;
    CreateSpec();
}

CTorchObjectSpec::~CTorchObjectSpec() { DestroySpec(); }
EI_REWRITTEN_SPECIAL(0x004D6BA0, CTorchObjectSpec::~CTorchObjectSpec);
EI_REWRITTEN_SPECIAL(0x004D6B80, CTorchObjectSpec::~CTorchObjectSpec);   // deleting

const char* CTorchObjectSpec::GetClassName() { return kTorchSpecClassName; }
int CTorchObjectSpec::GetClassId() { return kTorchSpecClassId; }
int CTorchObjectSpec::RunTick() { return 1; }

EI_REWRITTEN(0x004D6B60, CTorchObjectSpec::GetClassName);
EI_REWRITTEN(0x004D6B70, CTorchObjectSpec::GetClassId);
EI_REWRITTEN(0x004D6B50, CTorchObjectSpec::RunTick);
