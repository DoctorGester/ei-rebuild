// The world's managers released (0055F8E0, the base world's slot 3; A-U7). Their creation is
// world_create.cpp (0055EE40). See world.h and docs/notes/combat.md.
#include "world.h"

#include "base_manager.h"
#include "core_refcount.h"
#include "item.h"
#include "model.h"
#include "player_manager.h"
#include "resource_manager.h"
#include "spell.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

#include <cstdint>

namespace {
// The original drops the world's reference and deletes the manager without testing the count.
void DeleteManager(CEngineObject*& manager) {
    if (!manager) return;
    --manager->refCount_;
    delete manager;
    manager = nullptr;
}

// A shared pointer's release (RefPtr::Destroy) and reset (RefPtr::Share(nullptr)).
void DropShared(RefPtr* slot) {
    if (!slot->ptr) return;
    slot->Destroy();
    slot->Share(nullptr);
}
} // namespace

void CWorld::DestroyManagers() {
    if (SpellManager()) {
        ObjectListIterator objects{nullptr, nullptr};
        for (CEngineObject* object = objects.First(SpellManager()); object; object = objects.Next()) object->ClearState();
        if (objects.manager) --objects.manager->refCount_;
    }
    DeleteManager(players_);
    DropShared(&shared5C_);
    DropShared(reinterpret_cast<RefPtr*>(&netSession_));
    DeleteManager(manager40_);
    DeleteManager(manager3C_);
    if (CLinkFigureLoader* loader = figureLoader_) {
        figureLoader_ = nullptr;
        if (--loader->refCount_ == 0) loader->DeleteThis(1);
    }
    DeleteManager(manager44_);
    DeleteManager(manager58_);
    DeleteManager(itemManager_);
    DeleteManager(manager50_);
    DeleteManager(manager4C_);
    DeleteManager(manager48_);
}
EI_REPLACE_METHOD(0x0055F8E0, CWorld::DestroyManagers);

// ---- The managers' own small slots (0055F310-0055F400, A-U8) -------------------------------------
// GetClassName (11) / GetClassId (12) and the destructors of the player manager (vtable 0073F328),
// the effect manager (0073F2C4, the world's +0x48) and the item manager (0073F25C). Each destructor
// body stores the class's vtable and runs the base's body; slot 0 adds the delete. The item
// manager's slot 25 (0055F3B0, "items") is a CResourceManager slot not declared yet.

namespace {
constexpr std::uintptr_t kPlayerManagerName = std::uintptr_t(0x0078FEE8);   // "CPlayerManager"
constexpr std::uintptr_t kEffectManagerName = std::uintptr_t(0x0078FEF8);   // "CEffectManager"
constexpr std::uintptr_t kItemManagerName = std::uintptr_t(0x0078FF10);     // "CItemManager"
constexpr int kPlayerManagerId = 0x2E;
constexpr int kEffectManagerId = 0x32;
constexpr int kItemManagerId = 0x2F;
constexpr std::uintptr_t kEffectManagerVtable = std::uintptr_t(0x0073F2C4);

template <typename T> T* DeleteIf(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
} // namespace

const char* __fastcall PlayerManagerClassName(CPlayerManager*) { return reinterpret_cast<const char*>(kPlayerManagerName); }
int __fastcall PlayerManagerClassId(CPlayerManager*) { return kPlayerManagerId; }
void __fastcall PlayerManagerDestruct(CPlayerManager* self) {   // 0055F350
    ei::SetVtable(self, CPlayerManager::kVtableAddress);
    self->DestructBody();
}
CPlayerManager* CPlayerManager::DeleteScalar(unsigned flags) {   // 0055F330
    PlayerManagerDestruct(this);
    return DeleteIf(this, flags);
}
EI_REPLACE(0x0055F310, PlayerManagerClassName);
EI_REPLACE(0x0055F320, PlayerManagerClassId);
EI_REPLACE(0x0055F350, PlayerManagerDestruct);
EI_REPLACE_METHOD(0x0055F330, CPlayerManager::DeleteScalar);

const char* __fastcall EffectManagerClassName(CBaseManager*) { return reinterpret_cast<const char*>(kEffectManagerName); }
int __fastcall EffectManagerClassId(CBaseManager*) { return kEffectManagerId; }
void __fastcall EffectManagerDestruct(CBaseManager* self) {   // 0055F3A0
    ei::SetVtable(self, kEffectManagerVtable);
    self->DestructBody();
}
CBaseManager* CEffectManager::DeleteScalar(unsigned flags) {   // 0055F380
    EffectManagerDestruct(this);
    return DeleteIf(this, flags);
}
EI_REPLACE(0x0055F360, EffectManagerClassName);
EI_REPLACE(0x0055F370, EffectManagerClassId);
EI_REPLACE(0x0055F3A0, EffectManagerDestruct);
EI_REPLACE_METHOD(0x0055F380, CEffectManager::DeleteScalar);
// 0067E150 (slot 2; A-H24): CBaseManager's Create, as 0 / 1 (slot 3 is a thunk to CBaseManager's).
BOOL __fastcall EffectManagerCreate(CBaseManager* self) { return self->CBaseManager::Create() != 0 ? 1 : 0; }
EI_REPLACE(0x0067E150, EffectManagerCreate);

const char* __fastcall ItemManagerClassName(CItemManager*) { return reinterpret_cast<const char*>(kItemManagerName); }
int __fastcall ItemManagerClassId(CItemManager*) { return kItemManagerId; }
void __fastcall ItemManagerDestruct(CItemManager* self) {   // 0055F400
    ei::SetVtable(self, CItemManager::kVtableAddress);
    self->CResourceManager::Destruct();
}
CItemManager* CItemManager::DeleteScalar(unsigned flags) {   // 0055F3E0
    ItemManagerDestruct(this);
    return DeleteIf(this, flags);
}
EI_REPLACE(0x0055F3C0, ItemManagerClassName);
EI_REPLACE(0x0055F3D0, ItemManagerClassId);
EI_REPLACE(0x0055F400, ItemManagerDestruct);
EI_REPLACE_METHOD(0x0055F3E0, CItemManager::DeleteScalar);
