#pragma once

// The cache of unit occupancy shapes (the global at 007C2F70): one shape per standing height, made
// on demand and shared through reference-counted handles. See ai_shape_cache.cpp, and
// CAIMap::UnitShape in ai_movement.cpp for its user.
#include "core_refcount.h"
#include "lib/stlp.h"

#include <cstddef>
#include <cstdint>

// A handle into a cache (0x10 bytes): the cache and the object it resolved to, or the key and the
// height to look up.
struct ShapeCacheHandle {
    void* cache;
    CRefObject* ref;
    std::uint32_t key;
    float height;
};
static_assert(sizeof(ShapeCacheHandle) == 0x10);

// The bucket of a handle: its key.
struct ShapeCacheHash {
    std::size_t operator()(const ShapeCacheHandle& handle) const { return handle.key; }
};

class __declspec(novtable) UnitShapeCache {
public:
    static constexpr std::uintptr_t kVtableAddress = std::uintptr_t(0x0073FD58);

    // A node is {next, handle, shape}: 0x18 bytes.
    using Map = stlp::hash_map<ShapeCacheHandle, RefPtr, ShapeCacheHash>;

    // 0 (RET 4): makes the shape for a handle (005B33D0, CreateUnitShape in ai_unit_layer.h).
    virtual CRefObject* CreateShape(const ShapeCacheHandle* handle);

    // 005B32E0 (the static initialiser 005B31C0 calls it on 007C2F70; returns this). An empty map
    // with the first prime number of buckets from 100 up (193), then the vtable.
    UnitShapeCache* Construct();
    // 005B31F0 (through the stub 005B31E0 the initialiser registers with atexit). Every node's shape
    // released as its owner and its handle's reference dropped (the pair's destructor, 005BE280),
    // the nodes and the buckets freed.
    void Destruct();

    std::uint32_t unused_04;       // +0x04
    Map shapes;                     // +0x08
};
static_assert(sizeof(UnitShapeCache::Map::Node) == 0x18);
static_assert(offsetof(UnitShapeCache, shapes) == 8 && sizeof(UnitShapeCache) == 0x1C);

inline UnitShapeCache* UnitShapes() { return reinterpret_cast<UnitShapeCache*>(0x007C2F70); }
