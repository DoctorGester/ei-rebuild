// CServerSpecificManager and CTorchObjectSpec (A-S7, 004D6900-004D6BA0). See server_specific_manager.h.
#include "combat.h"
#include "server_specific_manager.h"

#include "unit.h"   // CMapObjectServer
#include "player_messages.h"

#include "ei/hook.h"
#include "ei/vcall.h"
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

CServerSpecificManager* CServerSpecificManager::Construct() { return new (this) CServerSpecificManager(); }
EI_REPLACE_METHOD(0x004D6900, CServerSpecificManager::Construct);

CServerSpecificManager::~CServerSpecificManager() {}

void CServerSpecificManager::Destruct() { this->CServerSpecificManager::~CServerSpecificManager(); }
EI_REPLACE_METHOD(0x004D6960, CServerSpecificManager::Destruct);

CServerSpecificManager* CServerSpecificManager::DeletingDestruct(unsigned flags) {
    this->CServerSpecificManager::~CServerSpecificManager();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004D6940, CServerSpecificManager::DeletingDestruct);

const char* CServerSpecificManager::GetClassName() { return kManagerClassName; }
int CServerSpecificManager::GetClassId() { return kManagerClassId; }
CEngineObject* CServerSpecificManager::NewObject(int type) { return NewObjectImpl(type); }

// Every object starts zeroed (operator_new_zeroed): CUnitServer's constructor leaves waitFrame (+0x93C),
// which ResolveBlocker reads, and others the saves write (SV7d3, SV7b2).
CEngineObject* CServerSpecificManager::NewObjectImpl(int type) {
    switch (type) {
    case kObject3DServer:
        if (void* p = gamecrt::operator_new_zeroed(0x20)) return Object3DServerConstruct(static_cast<CObject3DSpecific*>(p));
        return nullptr;
    case kMapObjectServer:
        if (void* p = gamecrt::operator_new_zeroed(sizeof(CMapObjectServer)))
            return static_cast<CMapObjectServer*>(p)->Construct();
        return nullptr;
    case kUnitServer:
        if (void* p = gamecrt::operator_new_zeroed(0xB20)) return static_cast<CUnitServer*>(p)->Construct();
        return nullptr;
    case kPlayerServer:
        if (void* p = gamecrt::operator_new_zeroed(0x5128))   // 0066C540
            return reinterpret_cast<CEngineObject*>(static_cast<CPlayerServer*>(p)->Construct());
        return nullptr;
    case kTorchObjectSpec:
        if (void* p = gamecrt::operator_new_zeroed(sizeof(CTorchObjectSpec))) return new (p) CTorchObjectSpec();
        return nullptr;
    default:
        return nullptr;
    }
}

namespace {
const char* __fastcall ManagerClassNameHook(CServerSpecificManager* self) {
    return self->CServerSpecificManager::GetClassName();
}
int __fastcall ManagerClassIdHook(CServerSpecificManager* self) { return self->CServerSpecificManager::GetClassId(); }
} // namespace
EI_REPLACE(0x004D6920, ManagerClassNameHook);
EI_REPLACE(0x004D6930, ManagerClassIdHook);
EI_REPLACE_METHOD(0x004D6970, CServerSpecificManager::NewObjectImpl);

// --- CTorchObjectSpec ---------------------------------------------------------------------------

CTorchObjectSpec::CTorchObjectSpec() {
    ClearFields();
    torch_ = nullptr;
    CreateSpec();
}

CTorchObjectSpec::~CTorchObjectSpec() { DestroySpec(); }

void CTorchObjectSpec::Destruct() { this->CTorchObjectSpec::~CTorchObjectSpec(); }
EI_REPLACE_METHOD(0x004D6BA0, CTorchObjectSpec::Destruct);

CTorchObjectSpec* CTorchObjectSpec::DeletingDestruct(unsigned flags) {
    this->CTorchObjectSpec::~CTorchObjectSpec();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004D6B80, CTorchObjectSpec::DeletingDestruct);

const char* CTorchObjectSpec::GetClassName() { return kTorchSpecClassName; }
int CTorchObjectSpec::GetClassId() { return kTorchSpecClassId; }
int CTorchObjectSpec::RunTick() { return 1; }

namespace {
const char* __fastcall TorchSpecClassNameHook(CTorchObjectSpec* self) { return self->CTorchObjectSpec::GetClassName(); }
int __fastcall TorchSpecClassIdHook(CTorchObjectSpec* self) { return self->CTorchObjectSpec::GetClassId(); }
// 004D6B50: slot 6 (the per-tick update of world objects) returns 1. Slots 13-15 (004D6B20 / 30 / 40)
// are a bare RET (too short to hook).
int __fastcall TorchSpecUpdate(CTorchObjectSpec* self) { return self->CTorchObjectSpec::RunTick(); }
} // namespace
EI_REPLACE(0x004D6B60, TorchSpecClassNameHook);
EI_REPLACE(0x004D6B70, TorchSpecClassIdHook);
EI_REPLACE(0x004D6B50, TorchSpecUpdate);
