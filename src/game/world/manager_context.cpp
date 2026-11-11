// ManagerContext's members of the game (manager_context.h): the casts to the world it is and the world's managers no
// engine code asks for, an engine object's world (EngineObject::OwningWorld and the side's), and the copy of an item
// that the party and the client world make (their DuplicateItem).
#include "engine/core/manager_context.h"

#include "engine/core/base_manager.h"

#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/world/world.h"

#include <cstdint>

World* ManagerContext::ThisWorld() { return contextKind == ContextKind::Party ? nullptr : static_cast<World*>(this); }

WorldServer* ManagerContext::ThisServerWorld() {
    return contextKind == ContextKind::Server ? static_cast<WorldServer*>(this) : nullptr;
}

WorldClient* ManagerContext::ThisClientWorld() {
    return contextKind == ContextKind::Client ? static_cast<WorldClient*>(this) : nullptr;
}

BaseManager* ManagerContext::TimedObjectManager() {
    World* world = ThisWorld();
    return world ? world->timedObjectManager.get() : nullptr;
}

MapObjectManager* ManagerContext::GetMapObjectManager() {
    World* world = ThisWorld();
    return world ? world->objectManager.get() : nullptr;
}

World* EngineObject::OwningWorld() const {
    auto* context = owner->context;
    return context ? context->ThisWorld() : nullptr;
}

WorldServer* EngineObject::ServerWorld() const {
    auto* context = owner->context;
    return context ? context->ThisServerWorld() : nullptr;
}

WorldClient* EngineObject::ClientWorld() const {
    auto* context = owner->context;
    return context ? context->ThisClientWorld() : nullptr;
}

Item* ManagerContext::MakeCopyOf(Item* item) {
    std::uint32_t packed = item->prototypeId;
    Item* copy = ItemFactory()->MakeItem(item->Kind(), packed >> 16, packed & 0xFFFF);
    item->CopyTo(copy, GetSpellManager());
    return copy;
}
