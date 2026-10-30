#pragma once

// The context a manager belongs to (BaseManager::context, set by Init / SetContext): the world (CWorld) or
// a party (CParty), the two classes deriving from it. Its getters return null (the party's base table); the
// world answers them all (world.h), a party only the item and spell ones. Names and evidence:
// docs/notes/map_objects.md ("Virtual slot names"). No data of its own.

#include "engine_object.h"

class BaseManager;
class Item;
class CItemConstructor;
struct ItemManager;
class LinkFigureLoader;
struct CMapObjectManager;
class PlayerManager;
class CSpellConstructor;
class CSpellManager;
class CWorld;

class CManagerContext : public EngineObject {
public:
    void Create() override;

    virtual CWorld* ThisWorld() { return nullptr; }                      // the world (returns this)
    virtual BaseManager* TimedObjectManager() { return nullptr; }       // the world's timedObjectManager (the client's Segment ticks its objects)
    virtual BaseManager* GetFigureManager() { return nullptr; }            // the world's figureManager
    virtual CItemConstructor* GetItemConstructor() { return nullptr; }
    virtual CSpellConstructor* GetSpellConstructor() { return nullptr; }
    virtual ItemManager* ItemFactory() { return nullptr; }
    virtual CMapObjectManager* GetMapObjectManager() { return nullptr; }
    virtual PlayerManager* PlayerList() { return nullptr; }
    // The side-specific halves' manager (the client's is a ClientSpecificManager).
    virtual BaseManager* SpecificManager() { return nullptr; }
    virtual CSpellManager* GetSpellManager() { return nullptr; }
    virtual LinkFigureLoader* FigureLoader() { return nullptr; }        // the figure loader (model.h)
    // A copy of an item: MakeCopyOf (the server's is WorldServer::CopyItem). Not called
    // CopyItem here: WorldServer's plain member of that name would hide it.
    virtual Item* DuplicateItem(Item* item) { return MakeCopyOf(item); }

    // (manager_context.cpp). The item factory makes an item of `item`'s kind,
    // prototype (high word of the packed id) and material (low word), which `item` then fills (its
    // CopyTo with the spell manager). Returns the new item.
    Item* MakeCopyOf(Item* item);
};
