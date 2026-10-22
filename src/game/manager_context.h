#pragma once

// The context a manager belongs to (CBaseManager::context_, set by Init / SetContext): the world
// (CWorld) or a party (CParty), the two classes deriving from it. Its getters return null (game.exe's
// 0074306C, the party's base table); the world answers them all (world_class.h), a party only the
// item and spell ones. Names and evidence: docs/notes/map_objects.md ("Virtual slot names"). No data
// of its own.

#include "engine_object.h"

class CBaseManager;
class CItem;
class CItemConstructor;
struct CItemManager;
class CLinkFigureLoader;   // model.h
struct CMapObjectManager;  // unit_create.h
class CPlayerManager;      // player_manager.h
class CSpellConstructor;
class CSpellManager;
class CWorld;

class CManagerContext : public CEngineObject {
public:
    int Create() override;   // 0055E930: 1

    virtual CWorld* ThisWorld() { return nullptr; }                      // the world (0055EB40 returns this)
    virtual CBaseManager* TimedObjectManager() { return nullptr; }       // the world's manager48_ (the client's Segment ticks its objects)
    virtual CBaseManager* FigureManager() { return nullptr; }            // the world's manager3C_
    virtual CItemConstructor* ItemConstructor() { return nullptr; }
    virtual CSpellConstructor* SpellConstructor() { return nullptr; }
    virtual CItemManager* ItemFactory() { return nullptr; }              // the item manager
    virtual CMapObjectManager* MapObjectManager() { return nullptr; }
    virtual CPlayerManager* PlayerList() { return nullptr; }
    // The side-specific halves' manager (the client's is a CClientSpecificManager, 0042D690; it makes
    // the class 0x45 objects of CFakeUnit's figures).
    virtual CBaseManager* SpecificManager() { return nullptr; }
    virtual CSpellManager* SpellManager() { return nullptr; }
    virtual CLinkFigureLoader* FigureLoader() { return nullptr; }        // the figure loader (model.h)
    // A copy of an item: MakeCopyOf (the server's is CWorldServer::CopyItem, 00594CE0). Not called
    // CopyItem here: CWorldServer's plain member of that name would hide it.
    virtual CItem* DuplicateItem(CItem* item) { return MakeCopyOf(item); }

    // 0055E940 (RET 4; manager_context.cpp). The item factory makes an item of `item`'s kind,
    // prototype (high word of the packed id) and material (low word), which `item` then fills (its
    // CopyTo with the spell manager). Returns the new item.
    CItem* MakeCopyOf(CItem* item);
};
