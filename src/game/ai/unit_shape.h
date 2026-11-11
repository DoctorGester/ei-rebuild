#pragma once

// The cache of unit occupancy shapes (a global): one shape per standing height, made on demand and shared
// through reference-counted handles. See ai_unit_shape.cpp, and AIMap::ShapeForHeight in ai_movement.cpp for its
// user. The unit occupancy layers' helpers (the layers are AIMap::unitLayerSmall and unitLayerLarge, declared in
// ai_map.h) are at the end: the shape cache's creator and the block hash.

#include "game/ai/ai_map.h"
#include "engine/core/core_refcount.h"
#include "game/units/unit.h"

#include <cstddef>
#include <cstdint>
#include <unordered_map>

// A unit's footprint on the occupancy layers (ai/unit_shape.h): 16 x 16 cell levels around the unit, shared by
// radius through the cache below. Methods in ai_unit_shape.cpp.
class UnitShape final : public RefObject {
public:
    // The levels of a disc: per cell (2 + radius - half its distance from the
    // middle cell (8, 8)) x 16, rounded; cells where that is not above 0 stay 0. No references.
    explicit UnitShape(float radius);
    // An empty shape, as OnShutdown leaves one (the water patch, ai/ai_unit_layer.cpp). No references.
    UnitShape();
    // Constructed again empty: no levels, radius -1. Returns 0.
    void OnShutdown() override;
    UnitShape* Clone() override;

    std::uint8_t levels[16][16] = {};  // rows of 16
    float radius = 0.0f;
};

class UnitShapeCache;

// A handle into the cache: the cache and the shape it resolved to (one reference), or the height to look up. It
// starts with nothing resolved: no reference, no cache.
struct ShapeCacheHandle {
    UnitShapeCache* cache = nullptr;
    Ref<UnitShape> ref;
    float height = 0.0f;
};

class UnitShapeCache {
public:
    UnitShape* Get(ShapeCacheHandle* handle);

    virtual UnitShape* CreateShape(const ShapeCacheHandle* handle);

    // The shape for the handle's height: the stored one, else a new one from CreateShape, stored for the height
    // (shared). The handle then holds it (one more reference) and this cache.
    UnitShape* Resolve(ShapeCacheHandle* handle) {
        UnitShape* shape;
        auto found = shapes.find(handle->height);
        if (found != shapes.end()) {
            shape = found->second.Get();
        } else {
            shape = CreateShape(handle);
            ShareAssignOwned(shapes[handle->height], shape);
        }
        handle->cache = this;
        handle->ref.Assign(shape);
        return shape;
    }

    // By standing height (the game hashed its bits; the heights are tenths, never NaN, so float equality is the
    // game's).
    std::unordered_map<float, SharedRef<UnitShape>> shapes;
};

// The cache (defined in ai_unit_shape.cpp).
extern UnitShapeCache g_unitShapes;

// The shape cache's creator virtual: a new shape for the handle's radius (`this` unused).
UnitShape* CreateUnitShape(const ShapeCacheHandle* handle);

inline int UnitLayerBucket(int blockX, int blockY) { return (blockX * 17 + blockY) & 0x3FF; }
