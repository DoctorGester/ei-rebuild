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
    if (GetSpellManager()) {
        ObjectListIterator objects{nullptr, {}};
        for (EngineObject* object = objects.First(GetSpellManager()); object; object = objects.Next()) object->ClearState();
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
// ClassName / GetClassId and the destructors of the player manager, the effect manager and the item manager. The deleting
// destructor adds the delete.

PlayerManager::PlayerManager() {}
PlayerManager::~PlayerManager() {}
const char* PlayerManager::ClassName() { return kPlayerManagerName; }
ClassId PlayerManager::GetClassId() { return ClassId::PlayerManager; }

EffectManager::EffectManager() {}
EffectManager::~EffectManager() {}
const char* EffectManager::ClassName() { return kEffectManagerName; }
ClassId EffectManager::GetClassId() { return ClassId::EffectManager; }

ItemManager::ItemManager() {}
ItemManager::~ItemManager() {}
const char* ItemManager::ClassName() { return kItemManagerName; }
ClassId ItemManager::GetClassId() { return ClassId::ItemManager; }
const char* ItemManager::GetFolderName() { return kItemsFolderName; }

BaseManager* CWorld::GetFigureManager() { return figureManager; }
