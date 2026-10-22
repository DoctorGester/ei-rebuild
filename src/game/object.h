#pragma once

// Shared pieces of the world objects (00440000-00470000): small engine objects defined in this
// range and the trivial virtual getters of units and figures. Names are provisional. See
// docs/notes/objects.md for the class map. (CEngineObject: engine_object.h; the object list
// iterator: base_manager.h; CMemoryStream: core_streams.h.)

#include "ai_map.h"
#include "base_manager.h"
#include "core_transform.h"
#include "engine_object.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


// Virtual getters shared by CUnit (vtable 0073EA8C) and CFakeUnit (0073BFE8). Written as __fastcall
// shims (this in ECX, no stack arguments) because the classes' virtual interfaces are not declared.
const char* __fastcall UnitGetName(const CUnit* unit);       // slot 7:  00446820
int __fastcall UnitGetFlag278(const CUnit* unit);            // slot 54: 00446890
int __fastcall UnitGetFlag274(const CUnit* unit);            // slot 55: 00446880
float __fastcall UnitGetValue328(const CUnit* unit);         // slot 60: 004468A0

// Slot 7 (GetName) of game.exe's base CFigure table (0073C224; the figure classes' GetName) and of
// CFakeFigure (0073C274): CFigureMesh::name_ (+0x120) / SceneMesh::name (+0x94).
const char* __fastcall FigureGetName(const CEngineObject* figure);       // 00453440
const char* __fastcall FakeFigureGetName(const CEngineObject* figure);   // 004547C0

// "CHoover" (class id 0x1F, vtable Vtable() in place of game.exe's 0073C434): no fields of its own seen.
class CItem;    // item.h
class CUnit;    // unit.h
class CWorld;   // world.h
class CHoover : public CEngineObject {
public:
    // 00461CB0 (RET 0x18; hoover_loot.cpp; `this`, the world's hoover, is not read): the loot of the
    // dead `unit` for its taker on `dropList` / `itemList` and `gold`; 0 when there is nothing to take.
    int CollectLoot(CWorld* world, CUnit* unit, mfc::CTypedPtrList<CItem>* dropList,
                    mfc::CTypedPtrList<CItem>* itemList, void* unused, int* gold);
    CHoover* DeletingDestruct(unsigned flags);
    // The compiled table (C19, class_slots.cpp's HooverClass).
    static void* Vtable();

    // 00461C10. Constructor body: the engine object's fields cleared, then its vtable. Returns
    // this.
    CHoover* Construct();
    // 00461C80. Destructor body: restores its vtable, then CEngineObject::DestructBase.
    void Destruct();
};
