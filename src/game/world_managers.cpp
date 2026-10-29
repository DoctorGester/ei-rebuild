// The world's managers released (the base world's Destroy). Their creation is world_create.cpp. See world.h
// and docs/notes/combat.md.
#include "base_manager.h"
#include "core_refcount.h"
#include "figure_manager.h"
#include "item.h"
#include "model.h"
#include "player_manager.h"
#include "resource_manager.h"
#include "spell.h"
#include "world.h"

#include <cstdint>
#include <new>

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

constexpr const char* kPlayerManagerName = "CPlayerManager";
constexpr const char* kEffectManagerName = "CEffectManager";
constexpr const char* kItemManagerName = "CItemManager";
constexpr const char* kItemsFolderName = "items";

} // namespace

void CWorld::Destroy() {
    if (SpellManager()) {
        ObjectListIterator objects{nullptr, {}};
        for (CEngineObject* object = objects.First(SpellManager()); object; object = objects.Next()) object->ClearState();
        if (objects.manager) --objects.manager->refCount_;
    }
    DeleteManager(players_);
    DropShared(&worldHandle_);
    DropShared(&netSession_);
    DeleteManager(objectManager_);
    DeleteManager(figureManager_);
    figureLoader_.Reset();
    DeleteManager(specificManager_);
    DeleteManager(itemConstructor_);
    DeleteManager(itemManager_);
    DeleteManager(spellConstructor_);
    DeleteManager(spellManager_);
    DeleteManager(timedObjectManager_);
}

// ---- The managers' own small virtuals -------------------------------------
// ClassName / GetClassId and the destructors of the player manager, the effect manager and the item manager
// (the item manager's folder name "items" is in world_class_slots.cpp). The deleting destructor adds the delete.

CPlayerManager::CPlayerManager() {}
CPlayerManager::~CPlayerManager() {}
const char* CPlayerManager::ClassName() { return kPlayerManagerName; }
ClassId CPlayerManager::GetClassId() { return ClassId::PlayerManager; }

CEffectManager::CEffectManager() {}
CEffectManager::~CEffectManager() {}
int CEffectManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }
const char* CEffectManager::ClassName() { return kEffectManagerName; }
ClassId CEffectManager::GetClassId() { return ClassId::EffectManager; }

CItemManager::CItemManager() {}
CItemManager::~CItemManager() {}
const char* CItemManager::ClassName() { return kItemManagerName; }
ClassId CItemManager::GetClassId() { return ClassId::ItemManager; }
const char* CItemManager::GetFolderName() { return kItemsFolderName; }
