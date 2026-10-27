#pragma once

// The cache of unit occupancy shapes (a global): one shape per standing height, made on demand and shared
// through reference-counted handles. See ai_shape_cache.cpp, and CAIMap::UnitShape in ai_movement.cpp for its
// user.
#include "core_refcount.h"
#include "lib/stlp.h"
#include "game_data.h"
#include "lib/static_object.h"

#include <cstddef>
#include <cstdint>

// A handle into a cache (0x10 bytes): the cache and the object it resolved to, or the key and the
// height to look up.
struct ShapeCacheHandle {
    void* cache;
    union {
        CRefObject* ref;
        RefPtr counted;   // the same dword as the owning pointer it is (Assign adds a reference)
    };
    std::uint32_t key;
    float height;
};
static_assert(sizeof(ShapeCacheHandle) == 0x10);

struct ShapeCacheHash {
    std::size_t operator()(const ShapeCacheHandle& handle) const { return handle.key; }
};

class UnitShapeCache {
public:
    CRefObject* Get(void* handleIn);

    using Map = stlp::hash_map<ShapeCacheHandle, RefPtr, ShapeCacheHash>;

    virtual CRefObject* CreateShape(const ShapeCacheHandle* handle);

    // The static initialiser calls it on the global; returns this. An empty map with the first prime number of
    // buckets from 100 up (193), then the vtable (constructed in place).
    UnitShapeCache* Construct();
    void Destruct();

    std::uint32_t unused_04;
    Map shapes;
};
static_assert(sizeof(UnitShapeCache::Map::Node) == 0x18);
static_assert(offsetof(UnitShapeCache, shapes) == 8 && sizeof(UnitShapeCache) == 0x1C);

// The map's pair, pair<const handle, RefPtr> (ai_movement.cpp; compiler-written).
struct ShapePair : UnitShapeCache::Map::Pair {
    ShapePair* Construct(const ShapeCacheHandle* from, const RefPtr* shared);
};
static_assert(sizeof(ShapePair) == 0x14);
// Destroyed, the value released as its owner (RefPtr::Destroy), then the handle's reference dropped.
void __fastcall DestroyShapePair(UnitShapeCache::Map::Pair* pair);

// The cache: built by a static initialiser, destroyed at exit (both in ai_shape_cache.cpp).
extern lib::StaticObject<UnitShapeCache> g_unitShapes;
inline UnitShapeCache* UnitShapes() { return &*g_unitShapes; }
