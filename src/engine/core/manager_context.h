#pragma once

// The context a manager belongs to (BaseManager::context, set by Init / SetContext): the world (World) or
// a party (Party), the two classes deriving from it. A party answers only the item and spell getters (virtual,
// both classes override them); the others answer from the world and are null for a party. Names and evidence:
// docs/notes/map_objects.md ("Virtual slot names").

#include "engine/core/engine_object.h"

class BaseManager;
class FigureManager;
class Item;
class ItemConstructor;
struct ItemManager;
class LightFactory;
class LinkFigureLoader;
class MaterialManager;
struct MapObjectManager;
class PlayerManager;
class SpellConstructor;
class SpellManager;
class TextureManager;
class World;
class WorldClient;
class WorldServer;

// What a context is: a party, the server's world or the client's world.
enum class ContextKind { Party, Server, Client };

class ManagerContext : public EngineObject {
public:
    explicit ManagerContext(ContextKind contextKind = ContextKind::Party) : contextKind(contextKind) {}
    void Create() override;

    // The world this context is (world/manager_context.cpp: the members below answer from the world), else null.
    World* ThisWorld();
    WorldServer* ThisServerWorld();
    WorldClient* ThisClientWorld();
    BaseManager* TimedObjectManager();                                  // the world's timedObjectManager (the client's Segment ticks its objects)
    FigureManager* GetFigureManager();                                  // the world's figureManager
    virtual ItemConstructor* GetItemConstructor() { return nullptr; }
    virtual SpellConstructor* GetSpellConstructor() { return nullptr; }
    virtual ItemManager* ItemFactory() { return nullptr; }
    MapObjectManager* GetMapObjectManager();
    virtual PlayerManager* PlayerList() { return nullptr; }
    // The side-specific halves' manager (the client's is a ClientSpecificManager).
    BaseManager* SpecificManager();
    virtual SpellManager* GetSpellManager() { return nullptr; }
    LinkFigureLoader* FigureLoader();                                   // the figure loader (link_loader.h)
    // A copy of an item: MakeCopyOf (the server's is WorldServer::CopyItem). Not called
    // CopyItem here: WorldServer's plain member of that name would hide it.
    virtual Item* DuplicateItem(Item* item) { return MakeCopyOf(item); }
    // What the scene draws with (the scene renderer's world, scene_renderer.h; a texture's manager): the client world's
    // texture manager, its material manager, its light factory and its time in ticks (whole ticks plus the fraction of
    // the current one: the animated meshes' and the sky's clock); null and 0 for another context.
    TextureManager* GetTextureManager();
    MaterialManager* GetMaterialManager();
    LightFactory* GetLightFactory();
    float SceneTime();

    // (world/manager_context.cpp). The item factory makes an item of `item`'s kind,
    // prototype (high word of the packed id) and material (low word), which `item` then fills (its
    // CopyTo with the spell manager). Returns the new item.
    Item* MakeCopyOf(Item* item);

    const ContextKind contextKind;
};
