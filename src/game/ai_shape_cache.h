#pragma once

// The cache of unit occupancy shapes (a global): one shape per standing height, made on demand and shared
// through reference-counted handles. See ai_shape_cache.cpp, and CAIMap::UnitShape in ai_movement.cpp for its
// user.
#include "core_refcount.h"

#include <cstdint>
#include <unordered_map>

// A handle into the cache: the cache and the object it resolved to (one reference), or the height to look up. It
// starts with nothing resolved: no reference, no cache.
struct ShapeCacheHandle {
    void* cache = nullptr;
    Ref<CRefObject> ref;
    float height = 0.0f;
};

class UnitShapeCache {
public:
    CRefObject* Get(void* handleIn);

    virtual CRefObject* CreateShape(const ShapeCacheHandle* handle);

    // The shape for the handle's height: the stored one, else a new one from CreateShape, stored for the height
    // (shared). The handle then holds it (one more reference) and this cache.
    CRefObject* Resolve(ShapeCacheHandle* handle) {
        CRefObject* shape;
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
    std::unordered_map<float, SharedRef<CRefObject>> shapes;
};

// The cache (defined in ai_movement.cpp).
extern UnitShapeCache g_unitShapes;
