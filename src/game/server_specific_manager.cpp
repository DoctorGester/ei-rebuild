// CServerSpecificManager and CTorchObjectSpec (A-S7, 004D6900-004D6BA0). See server_specific_manager.h.
#include "combat.h"
#include "server_specific_manager.h"

#include "unit.h"   // CMapObjectServer
#include "player_messages.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <new>

namespace {

// The class names are the exe's strings (GetClassName returns their addresses).
constexpr std::uintptr_t kManagerClassName = std::uintptr_t(0x0078DAD0);
constexpr std::uintptr_t kTorchSpecClassName = std::uintptr_t(0x0078DAE8);
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

CServerSpecificManager* CServerSpecificManager::Construct() {
    ConstructBase();
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x004D6900, CServerSpecificManager::Construct);

void CServerSpecificManager::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    DestructBody();
}
EI_REPLACE_METHOD(0x004D6960, CServerSpecificManager::Destruct);

CServerSpecificManager* CServerSpecificManager::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004D6940, CServerSpecificManager::DeletingDestruct);

const char* CServerSpecificManager::GetClassName() { return reinterpret_cast<const char*>(kManagerClassName); }
int CServerSpecificManager::GetClassId() { return kManagerClassId; }

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
    case kTorchObjectSpec: {
        auto* spec = static_cast<CTorchObjectSpec*>(gamecrt::operator_new_zeroed(sizeof(CTorchObjectSpec)));
        if (!spec) return nullptr;
        ei::SetVtable(spec, CEngineObject::kVtableAddress);
        spec->refCount_ = 0;
        spec->id_ = -1;
        spec->prototypeId_ = 0;
        spec->type_ = 0;
        spec->flags_ = 0;
        spec->owner_ = nullptr;
        new (&spec->name_) mfc::CString();
        ei::SetVtable(spec, CTorchObjectSpec::kVtableAddress);
        spec->torch_ = nullptr;
        spec->Create();
        return spec;
    }
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

void CTorchObjectSpec::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    Destroy();
    name_.~CString();
    DestructBase();
}
EI_REPLACE_METHOD(0x004D6BA0, CTorchObjectSpec::Destruct);

CTorchObjectSpec* CTorchObjectSpec::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x004D6B80, CTorchObjectSpec::DeletingDestruct);

const char* CTorchObjectSpec::GetClassName() { return reinterpret_cast<const char*>(kTorchSpecClassName); }
int CTorchObjectSpec::GetClassId() { return kTorchSpecClassId; }

namespace {
const char* __fastcall TorchSpecClassNameHook(CTorchObjectSpec* self) { return self->CTorchObjectSpec::GetClassName(); }
int __fastcall TorchSpecClassIdHook(CTorchObjectSpec* self) { return self->CTorchObjectSpec::GetClassId(); }
// 004D6B50: slot 6 (the per-tick update of world objects) returns 1. Slots 13-15 (004D6B20 / 30 / 40)
// are a bare RET (too short to hook).
int __fastcall TorchSpecUpdate(CTorchObjectSpec*) { return 1; }
} // namespace
EI_REPLACE(0x004D6B60, TorchSpecClassNameHook);
EI_REPLACE(0x004D6B70, TorchSpecClassIdHook);
EI_REPLACE(0x004D6B50, TorchSpecUpdate);
