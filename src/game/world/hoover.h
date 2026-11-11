#pragma once

// Shared pieces of the world objects: small engine objects defined in this range and the trivial virtual
// getters of units and figures. Names are provisional. See docs/notes/objects.md for the class map.
// (EngineObject: engine_object.h; the object list iterator: base_manager.h; MemoryStream: core_streams.h.)

#include "game/ai/ai_map.h"
#include "engine/core/base_manager.h"
#include "engine/core/core_transform.h"
#include "engine/core/engine_object.h"

#include <cstddef>
#include <cstdint>
#include <cstring>

// "Hoover" (class id 0x1F): no fields of its own seen. world/world_loot.cpp.
class World;
class Hoover final : public EngineObject {
public:
    ~Hoover() override;

    void Create() override;
    void Destroy() override {}              // empty
    const char* ClassName() override;
    ClassId GetClassId() override;

    // (world/world_loot.cpp; `this`, the world's hoover, is not read): the loot of the
    // dead `unit` for its taker on `dropList` / `itemList` and `gold`; 0 when there is nothing to take.
    int CollectLoot(World* world, Unit* unit, std::vector<Item*>* dropList,
                    std::vector<Item*>* itemList, int* gold);
};
