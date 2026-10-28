// The world's managers released (the base world's Destroy). Their creation is world_create.cpp. See world.h
// and docs/notes/combat.md.
#include "world.h"
#include "figure_manager.h"

#include "base_manager.h"
#include "core_refcount.h"
#include "item.h"
#include "model.h"
#include "player_manager.h"
#include "resource_manager.h"
#include "spell.h"

#include <cstdint>
#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {
// The world's reference is dropped and the manager deleted without testing the count.
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

void CWorld::Destroy() {
    if (SpellManager()) {
        ObjectListIterator objects{nullptr, {}};
        for (CEngineObject* object = objects.First(SpellManager()); object; object = objects.Next()) object->ClearState();
        if (objects.manager) --objects.manager->refCount_;
    }
    DeleteManager(players_);
    DropShared(&shared5C_);
    DropShared(&netSession_);
    DeleteManager(manager40_);
    DeleteManager(manager3C_);
    if (CLinkFigureLoader* loader = figureLoader_) {
        figureLoader_ = nullptr;
        if (--loader->refCount_ == 0) delete loader;
    }
    DeleteManager(manager44_);
    DeleteManager(manager58_);
    DeleteManager(itemManager_);
    DeleteManager(manager50_);
    DeleteManager(manager4C_);
    DeleteManager(manager48_);
}

// ---- The managers' own small virtuals -------------------------------------
// GetClassName / GetClassId and the destructors of the player manager, the effect manager and the item manager
// (the item manager's folder name "items" is in world_class_slots.cpp). The deleting destructor adds the delete.

namespace {
constexpr const char* kPlayerManagerName = "CPlayerManager";
constexpr const char* kEffectManagerName = "CEffectManager";
constexpr const char* kItemManagerName = "CItemManager";
constexpr const char* kItemsFolderName = "items";
constexpr int kPlayerManagerId = 0x2E;
constexpr int kEffectManagerId = 0x32;
constexpr int kItemManagerId = 0x2F;
} // namespace

CPlayerManager::CPlayerManager() {}
CPlayerManager::~CPlayerManager() {}
const char* CPlayerManager::GetClassName() { return kPlayerManagerName; }
int CPlayerManager::GetClassId() { return kPlayerManagerId; }

CEffectManager::CEffectManager() {}
CEffectManager::~CEffectManager() {}
int CEffectManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }
const char* CEffectManager::GetClassName() { return kEffectManagerName; }
int CEffectManager::GetClassId() { return kEffectManagerId; }

CItemManager::CItemManager() {}
CItemManager::~CItemManager() {}
const char* CItemManager::GetClassName() { return kItemManagerName; }
int CItemManager::GetClassId() { return kItemManagerId; }
const char* CItemManager::GetFolderName() { return kItemsFolderName; }
