#pragma once

// The cache of unit occupancy shapes (a global): one shape per standing height, made on demand and shared
// through reference-counted handles. See ai_shape_cache.cpp, and CAIMap::UnitShape in ai_movement.cpp for its
// user.
#include "core_refcount.h"
#include "lib/static_object.h"

#include <cstdint>
#include <unordered_map>

// A handle into the cache: the cache and the object it resolved to, or the height to look up.
struct ShapeCacheHandle {
    void* cache;
    union {
        CRefObject* ref;
        RefPtr counted;   // the same dword as the owning pointer it is (Assign adds a reference)
    };
    float height;
};

// A handle that has resolved nothing: no reference, no cache (the height is left as it is).
inline ShapeCacheHandle* InitShapeHandle(ShapeCacheHandle* self) {
    self->ref = nullptr;
    self->cache = nullptr;
    return self;
}

class UnitShapeCache {
public:
    CRefObject* Get(void* handleIn);

    virtual CRefObject* CreateShape(const ShapeCacheHandle* handle);

    // The static initialiser calls it on the global: the cache built in place, no shapes; returns this.
    UnitShapeCache* Construct();
    void Destruct();
    // The shape for the handle's height: the stored one, else a new one from CreateShape, stored for the height
    // (shared). The handle then holds it (one more reference) and this cache.
    CRefObject* Resolve(ShapeCacheHandle* handle) {
        CRefObject* shape;
        auto found = shapes.find(handle->height);
        if (found != shapes.end()) {
            shape = found->second.ptr;
        } else {
            shape = CreateShape(handle);
            shapes[handle->height].ShareAssign(shape);
        }
        handle->cache = this;
        handle->counted.Assign(shape);
        return shape;
    }

    // By standing height (the game hashed its bits; the heights are tenths, never NaN, so float equality is the
    // game's).
    std::unordered_map<float, SharedRefPtr> shapes;
};

// The cache: built by a static initialiser, destroyed at exit (both in ai_shape_cache.cpp).
extern lib::StaticObject<UnitShapeCache> g_unitShapes;
inline UnitShapeCache* UnitShapes() { return &*g_unitShapes; }
