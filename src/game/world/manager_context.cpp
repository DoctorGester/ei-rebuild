// ManagerContext's own members (manager_context.h): the base Create, the world's managers and clock the engine asks
// for, and the copy of an item that the party and the client world make (their DuplicateItem).
#include "engine/core/manager_context.h"

#include "game/items/item.h"
#include "game/items/item_manager.h"
#include "game/world/world.h"

#include <cstdint>

void ManagerContext::Create() {}

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

FigureManager* ManagerContext::GetFigureManager() {
    World* world = ThisWorld();
    return world ? world->figureManager.get() : nullptr;
}

MapObjectManager* ManagerContext::GetMapObjectManager() {
    World* world = ThisWorld();
    return world ? world->objectManager.get() : nullptr;
}

BaseManager* ManagerContext::SpecificManager() {
    World* world = ThisWorld();
    return world ? world->specificManager.get() : nullptr;
}

LinkFigureLoader* ManagerContext::FigureLoader() {
    World* world = ThisWorld();
    return world ? world->figureLoader.Get() : nullptr;
}

TextureManager* ManagerContext::GetTextureManager() {
    WorldClient* client = ThisClientWorld();
    return client ? client->textureManager.get() : nullptr;
}

MaterialManager* ManagerContext::GetMaterialManager() {
    WorldClient* client = ThisClientWorld();
    return client ? client->materialManager.get() : nullptr;
}

LightFactory* ManagerContext::GetLightFactory() {
    WorldClient* client = ThisClientWorld();
    return client ? client->lightFactory.get() : nullptr;
}

float ManagerContext::SceneTime() {
    WorldClient* client = ThisClientWorld();
    return client ? static_cast<float>(client->tick) + client->tickFraction : 0.0f;
}

Item* ManagerContext::MakeCopyOf(Item* item) {
    std::uint32_t packed = item->prototypeId;
    Item* copy = ItemFactory()->MakeItem(item->Kind(), packed >> 16, packed & 0xFFFF);
    item->CopyTo(copy, GetSpellManager());
    return copy;
}
