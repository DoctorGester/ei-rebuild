#pragma once

// The context a manager belongs to (CBaseManager::context_, set by Init / SetContext): the world
// (CWorld) or a party (CParty), the two classes deriving from it. Their slots 13-24 are the same
// getters; a party answers only the item and spell ones (the others return null). The vtables are
// the compiled tables of world_class.h and party_lifetime.cpp; these declarations give the slots'
// order and types for the calls. Names and evidence: docs/notes/map_objects.md ("Virtual slot
// names"). Not a data member of its own: CWorld's and CParty's layouts start with CEngineObject's.

#include "engine_object.h"

class CBaseManager;
class CItem;
class CItemConstructor;
struct CItemManager;
class CLinkFigureLoader;   // model.h
class CSpellConstructor;
class CSpellManager;
class CWorld;

class CManagerContext : public CEngineObject {
public:
    virtual CWorld* ThisWorld();                    // 13: the world (0055EB40 returns this), null for a party (0065F4B0)
    virtual CBaseManager* TimedObjectManager();     // 14: 0055EB90 (the world's +0x48, manager48_; the client's Segment ticks its objects)
    virtual CBaseManager* FigureManager();          // 15: the figure manager (+0x3C)
    virtual CItemConstructor* ItemConstructor();    // 16: 0055EBB0 (+0x58); the party's 0065F540
    virtual CSpellConstructor* SpellConstructor();  // 17: 0055EBC0 (+0x50); the party's 0065F550
    virtual CItemManager* ItemFactory();            // 18: 004BA000 (the world's itemManager_ +0x54); the party's itemManager_
    virtual CBaseManager* MapObjectManager();       // 19: 0055EB70, the map object manager (+0x40)
    virtual CBaseManager* PlayerList();             // 20: 0055EB50 (+0x38, players_)
    // 21: 0055EB80 (+0x44): the side-specific halves' manager (the client's is a
    // CClientSpecificManager, 0042D690; it makes the class 0x45 objects of CFakeUnit's figures).
    virtual CBaseManager* SpecificManager();
    virtual CSpellManager* SpellManager();          // 22: 0055EBA0 (+0x4C); the party's spellManager_
    virtual CLinkFigureLoader* FigureLoader();      // 23 (vtable +0x5C): the figure loader (model.h)
    // 24: a copy of an item (the party and the world client 0055E940, CParty::MakeCopyOf; the
    // server 00594CE0, CWorldServer::CopyItem). Not called CopyItem here: CWorldServer's plain
    // member of that name would hide it.
    virtual CItem* DuplicateItem(CItem* item);
};
