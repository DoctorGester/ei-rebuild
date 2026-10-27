#pragma once

// Shared pieces of the world objects: small engine objects defined in this range and the trivial virtual
// getters of units and figures. Names are provisional. See docs/notes/objects.md for the class map.
// (CEngineObject: engine_object.h; the object list iterator: base_manager.h; CMemoryStream: core_streams.h.)

#include "ai_map.h"
#include "base_manager.h"
#include "core_transform.h"
#include "engine_object.h"
#include "game_data.h"

#include <cstddef>
#include <cstdint>
#include <cstring>


// Virtual getters shared by CUnit and CFakeUnit. Written as __fastcall
// functions (this in ECX, no stack arguments) because the classes' virtual interfaces are not declared.
const char* __fastcall UnitGetName(const CUnit* unit);       // slot 7
int __fastcall UnitGetFlag278(const CUnit* unit);            // slot 54
int __fastcall UnitGetFlag274(const CUnit* unit);            // slot 55
float __fastcall UnitGetValue328(const CUnit* unit);         // slot 60

// Slot 7 (GetName) of the base CFigure table (the figure classes' GetName) and of
// CFakeFigure: CFigureMesh::name_ / SceneMesh::name.
const char* __fastcall FigureGetName(const CEngineObject* figure);
const char* __fastcall FakeFigureGetName(const CEngineObject* figure);

// "CHoover" (class id 0x1F): no fields of its own seen. object.cpp.
class CWorld;
class CHoover final : public CEngineObject {
public:
    CHoover();              // the engine object's fields cleared
    ~CHoover() override;

    int Create() override;
    void Destroy() override {}              // empty
    const char* GetClassName() override;
    int GetClassId() override;

    // (hoover_loot.cpp; `this`, the world's hoover, is not read): the loot of the
    // dead `unit` for its taker on `dropList` / `itemList` and `gold`; 0 when there is nothing to take.
    int CollectLoot(CWorld* world, CUnit* unit, mfc::CTypedPtrList<CItem>* dropList,
                    mfc::CTypedPtrList<CItem>* itemList, void* unused, int* gold);
};
