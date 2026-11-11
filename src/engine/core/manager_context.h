#pragma once

// The context a manager belongs to (BaseManager::context, set by Init / SetContext): the world (World) or a party
// (Party), the two game classes deriving from it, or a program's scene without the game's world (ei-effects:
// tools/effect_viewer_scene.h). Its getters are virtual, null by default: a party answers the item and spell getters,
// a world the others too (the client's world also the scene's), the effect viewer's scene the figure and scene ones.
// The world casts and the item copy are the game's (world/manager_context.cpp). Names and evidence:
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
    void Create() override {}

    // The world this context is (world/manager_context.cpp: casts by contextKind), else null.
    World* ThisWorld();
    WorldServer* ThisServerWorld();
    WorldClient* ThisClientWorld();
    BaseManager* TimedObjectManager();                                  // the world's timedObjectManager (the client's Segment ticks its objects)
    MapObjectManager* GetMapObjectManager();
    virtual FigureManager* GetFigureManager() { return nullptr; }
    virtual ItemConstructor* GetItemConstructor() { return nullptr; }
    virtual SpellConstructor* GetSpellConstructor() { return nullptr; }
    virtual ItemManager* ItemFactory() { return nullptr; }
    virtual PlayerManager* PlayerList() { return nullptr; }
    // The side-specific halves' manager (the client's is a ClientSpecificManager): the figure parts' drawers come from
    // it (FigureManager::CreateObject).
    virtual BaseManager* SpecificManager() { return nullptr; }
    virtual SpellManager* GetSpellManager() { return nullptr; }
    virtual LinkFigureLoader* FigureLoader() { return nullptr; }       // the figure loader (link_loader.h)
    // A copy of an item: the party's and the client world's MakeCopyOf, the server's WorldServer::CopyItem. Not called
    // CopyItem here: WorldServer's plain member of that name would hide it.
    virtual Item* DuplicateItem(Item*) { return nullptr; }
    // What the scene draws with (the scene renderer's world, scene_renderer.h; a texture's manager): the client world's
    // (or the viewer's scene's) texture manager, its material manager, its light factory and its time in ticks (whole
    // ticks plus the fraction of the current one: the animated meshes' and the sky's clock).
    virtual TextureManager* GetTextureManager() { return nullptr; }
    virtual MaterialManager* GetMaterialManager() { return nullptr; }
    virtual LightFactory* GetLightFactory() { return nullptr; }
    virtual float SceneTime() { return 0.0f; }

    // (world/manager_context.cpp). The item factory makes an item of `item`'s kind,
    // prototype (high word of the packed id) and material (low word), which `item` then fills (its
    // CopyTo with the spell manager). Returns the new item.
    Item* MakeCopyOf(Item* item);

    const ContextKind contextKind;
};
