// The world's managers released (the base world's Destroy). Their creation is world_create.cpp. See world.h
// and docs/notes/combat.md.
#include "base_manager.h"
#include "core_refcount.h"
#include "figure_manager.h"
#include "item.h"
#include "item_manager.h"
#include "model.h"
#include "net_link.h"
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
    --manager->refCount;
    delete manager;
    manager = nullptr;
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
        if (objects.manager) --objects.manager->refCount;
    }
    DeleteManager(players);
    DestroyOwned(worldHandle);
    DestroyOwned(netSession);
    DeleteManager(objectManager);
    DeleteManager(figureManager);
    figureLoader.Reset();
    DeleteManager(specificManager);
    DeleteManager(itemConstructor);
    DeleteManager(itemManager);
    DeleteManager(spellConstructor);
    DeleteManager(spellManager);
    DeleteManager(timedObjectManager);
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
const char* CEffectManager::ClassName() { return kEffectManagerName; }
ClassId CEffectManager::GetClassId() { return ClassId::EffectManager; }

CItemManager::CItemManager() {}
CItemManager::~CItemManager() {}
const char* CItemManager::ClassName() { return kItemManagerName; }
ClassId CItemManager::GetClassId() { return ClassId::ItemManager; }
const char* CItemManager::GetFolderName() { return kItemsFolderName; }
