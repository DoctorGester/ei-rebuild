// The world's managers released (0055F8E0, the base world's slot 3; A-U7). Their creation is
// world_create.cpp. See world.h and docs/notes/combat.md.
#include "world.h"

#include "base_manager.h"
#include "core_refcount.h"
#include "item.h"
#include "model.h"
#include "player_manager.h"
#include "resource_manager.h"
#include "spell.h"

#include "ei/hook.h"
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

void CWorld::Destroy() {
    if (SpellManager()) {
        ObjectListIterator objects{nullptr, nullptr};
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
EI_REWRITTEN(0x0055F8E0, CWorld::Destroy);

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
} // namespace

CPlayerManager::CPlayerManager() {}
CPlayerManager::~CPlayerManager() {}
const char* CPlayerManager::GetClassName() { return kPlayerManagerName; }
int CPlayerManager::GetClassId() { return kPlayerManagerId; }

EI_REWRITTEN(0x0055F310, CPlayerManager::GetClassName);
EI_REWRITTEN(0x0055F320, CPlayerManager::GetClassId);
EI_REWRITTEN_SPECIAL(0x0055F350, CPlayerManager::~CPlayerManager);
EI_REWRITTEN_SPECIAL(0x0055F330, CPlayerManager::~CPlayerManager);   // deleting

CEffectManager::CEffectManager() {}
CEffectManager::~CEffectManager() {}
int CEffectManager::Create() { return CBaseManager::Create() != 0 ? 1 : 0; }
const char* CEffectManager::GetClassName() { return kEffectManagerName; }
int CEffectManager::GetClassId() { return kEffectManagerId; }

EI_REWRITTEN(0x0055F360, CEffectManager::GetClassName);
EI_REWRITTEN(0x0055F370, CEffectManager::GetClassId);
EI_REWRITTEN_SPECIAL(0x0055F3A0, CEffectManager::~CEffectManager);
EI_REWRITTEN_SPECIAL(0x0055F380, CEffectManager::~CEffectManager);   // deleting
// 0067E150 (slot 2; A-H24).
EI_REWRITTEN(0x0067E150, CEffectManager::Create);

CItemManager::CItemManager() {}
CItemManager::~CItemManager() {}
const char* CItemManager::GetClassName() { return kItemManagerName; }
int CItemManager::GetClassId() { return kItemManagerId; }
const char* CItemManager::GetFolderName() { return kItemsFolderName; }

EI_REWRITTEN(0x0055F3C0, CItemManager::GetClassName);
EI_REWRITTEN(0x0055F3D0, CItemManager::GetClassId);
EI_REWRITTEN_SPECIAL(0x0055F400, CItemManager::~CItemManager);
EI_REWRITTEN_SPECIAL(0x0055F3E0, CItemManager::~CItemManager);   // deleting
