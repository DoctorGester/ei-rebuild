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
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
// The original drops the world's reference and deletes the manager without testing the count.
template <typename T>
void DeleteManager(T*& manager) {
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
// GetClassName (11) / GetClassId (12) and the destructors of the player manager, the effect manager
// (the world's +0x48) and the item manager (compiler-made vtables, C19; the item manager's slot 25,
// 0055F3B0 "items", is registered in world_class_slots.cpp). Slot 0 adds the delete.

namespace {
constexpr const char* kPlayerManagerName = "CPlayerManager";
constexpr const char* kEffectManagerName = "CEffectManager";
constexpr const char* kItemManagerName = "CItemManager";
constexpr const char* kItemsFolderName = "items";
constexpr int kPlayerManagerId = 0x2E;
constexpr int kEffectManagerId = 0x32;
constexpr int kItemManagerId = 0x2F;

template <typename T> T* DeleteIf(T* self, unsigned flags) {
    if (flags & 1) gamecrt::operator_delete(self);
    return self;
}
} // namespace

CPlayerManager::CPlayerManager() {}
CPlayerManager* CPlayerManager::Construct() { return new (this) CPlayerManager(); }
CPlayerManager::~CPlayerManager() {}
const char* CPlayerManager::GetClassName() { return kPlayerManagerName; }
int CPlayerManager::GetClassId() { return kPlayerManagerId; }

const char* __fastcall PlayerManagerClassName(CPlayerManager* self) { return self->CPlayerManager::GetClassName(); }
int __fastcall PlayerManagerClassId(CPlayerManager* self) { return self->CPlayerManager::GetClassId(); }
void __fastcall PlayerManagerDestruct(CPlayerManager* self) { self->CPlayerManager::~CPlayerManager(); }   // 0055F350
CPlayerManager* CPlayerManager::DeleteScalar(unsigned flags) {   // 0055F330
    this->CPlayerManager::~CPlayerManager();
    return DeleteIf(this, flags);
}
EI_REPLACE(0x0055F310, PlayerManagerClassName);
EI_REPLACE(0x0055F320, PlayerManagerClassId);
EI_REWRITTEN(0x0055F350, PlayerManagerDestruct);
EI_REWRITTEN(0x0055F330, CPlayerManager::DeleteScalar);

CEffectManager::CEffectManager() {}
CEffectManager* CEffectManager::Construct() { return new (this) CEffectManager(); }
CEffectManager::~CEffectManager() {}
int CEffectManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }
const char* CEffectManager::GetClassName() { return kEffectManagerName; }
int CEffectManager::GetClassId() { return kEffectManagerId; }
CEngineObject* CEffectManager::NewObject(int type) { return NewObjectImpl(type); }

const char* __fastcall EffectManagerClassName(CEffectManager* self) { return self->CEffectManager::GetClassName(); }
int __fastcall EffectManagerClassId(CEffectManager* self) { return self->CEffectManager::GetClassId(); }
void __fastcall EffectManagerDestruct(CEffectManager* self) { self->CEffectManager::~CEffectManager(); }   // 0055F3A0
CBaseManager* CEffectManager::DeleteScalar(unsigned flags) {   // 0055F380
    this->CEffectManager::~CEffectManager();
    return DeleteIf(this, flags);
}
EI_REPLACE(0x0055F360, EffectManagerClassName);
EI_REPLACE(0x0055F370, EffectManagerClassId);
EI_REWRITTEN(0x0055F3A0, EffectManagerDestruct);
EI_REWRITTEN(0x0055F380, CEffectManager::DeleteScalar);
// 0067E150 (slot 2; A-H24).
BOOL __fastcall EffectManagerCreate(CEffectManager* self) { return self->CEffectManager::Create(); }
EI_REWRITTEN(0x0067E150, EffectManagerCreate);

CItemManager::CItemManager() {}
CItemManager* CItemManager::Construct() { return new (this) CItemManager(); }
CItemManager::~CItemManager() {}
const char* CItemManager::GetClassName() { return kItemManagerName; }
int CItemManager::GetClassId() { return kItemManagerId; }
const char* CItemManager::GetFolderName() { return kItemsFolderName; }

const char* __fastcall ItemManagerClassName(CItemManager* self) { return self->CItemManager::GetClassName(); }
int __fastcall ItemManagerClassId(CItemManager* self) { return self->CItemManager::GetClassId(); }
void __fastcall ItemManagerDestruct(CItemManager* self) { self->CItemManager::~CItemManager(); }   // 0055F400
CItemManager* CItemManager::DeleteScalar(unsigned flags) {   // 0055F3E0
    this->CItemManager::~CItemManager();
    return DeleteIf(this, flags);
}
EI_REPLACE(0x0055F3C0, ItemManagerClassName);
EI_REPLACE(0x0055F3D0, ItemManagerClassId);
EI_REWRITTEN(0x0055F400, ItemManagerDestruct);
EI_REWRITTEN(0x0055F3E0, CItemManager::DeleteScalar);
