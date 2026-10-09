// The world's managers released (0055F8E0, the base world's slot 3; A-U7). Their creation is
// world_create.cpp (0055EE40). See world.h and docs/notes/combat.md.
#include "world.h"

#include "base_manager.h"
#include "core_refcount.h"
#include "model.h"
#include "resource_manager.h"
#include "spell.h"

#include "ei/hook.h"

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
    DropShared(reinterpret_cast<RefPtr*>(&unknown60_));
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
