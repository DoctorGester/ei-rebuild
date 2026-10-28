#pragma once

// Shared pieces of the world objects: small engine objects defined in this range and the trivial virtual
// getters of units and figures. Names are provisional. See docs/notes/objects.md for the class map.
// (CEngineObject: engine_object.h; the object list iterator: base_manager.h; CMemoryStream: core_streams.h.)

#include "ai_map.h"
#include "base_manager.h"
#include "core_transform.h"
#include "engine_object.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <list>

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
    int CollectLoot(CWorld* world, CUnit* unit, std::list<CItem*>* dropList,
                    std::list<CItem*>* itemList, int* gold);
};
