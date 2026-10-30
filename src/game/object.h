#pragma once

// Shared pieces of the world objects: small engine objects defined in this range and the trivial virtual
// getters of units and figures. Names are provisional. See docs/notes/objects.md for the class map.
// (EngineObject: engine_object.h; the object list iterator: base_manager.h; MemoryStream: core_streams.h.)

#include "ai_map.h"
#include "base_manager.h"
#include "core_transform.h"
#include "engine_object.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// "Hoover" (class id 0x1F): no fields of its own seen. object.cpp.
class World;
class Hoover final : public EngineObject {
public:
    ~Hoover() override;

    void Create() override;
    void Destroy() override {}              // empty
    const char* ClassName() override;
    ClassId GetClassId() override;

    // (hoover_loot.cpp; `this`, the world's hoover, is not read): the loot of the
    // dead `unit` for its taker on `dropList` / `itemList` and `gold`; 0 when there is nothing to take.
    int CollectLoot(World* world, Unit* unit, std::vector<Item*>* dropList,
                    std::vector<Item*>* itemList, int* gold);
};
