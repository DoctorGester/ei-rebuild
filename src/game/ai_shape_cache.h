#pragma once

// The cache of unit occupancy shapes (a global): one shape per standing height, made on demand and shared
// through reference-counted handles. See ai_shape_cache.cpp, and CAIMap::UnitShape in ai_movement.cpp for its
// user.
#include "core_refcount.h"
#include "lib/stlp.h"
#include "lib/stlp_hash.h"
#include "ei/x87.h"
#include "lib/nan_audit.h"
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

// The map's pair, pair<const handle, RefPtr> (0x14 bytes; the members are compiler-written, ai_movement.cpp).
struct ShapePair {
    ShapeCacheHandle key;
    RefPtr value;
    ShapePair* Construct(const ShapeCacheHandle* from, const RefPtr* shared);
};
static_assert(sizeof(ShapePair) == 0x14);
// Destroyed, the value released as its owner (RefPtr::Destroy), then the handle's reference dropped.
void DestroyShapePair(ShapePair* pair);

// The unit shapes' cache's map (stlp_hash.h's hashtable on these traits): pair<const handle, RefPtr>, 0x18-byte
// nodes. A key lands in bucket handle.key % count; two handles are equal when their heights compare equal.
// The pair is copied and destroyed by its compiler-written members (ai_movement.cpp).
struct ShapeTraits {
    using Value = ShapePair;
    using Key = ShapeCacheHandle;
    static const Key& KeyOf(const Value& v) { return v.key; }
    static std::uint32_t Hash(const Key& k) { return k.key; }
    static bool Equal(const Key& a, const Key& b) {
        EI_NAN_AUDIT(a.height, b.height);
        return a.height == b.height;
    }
    static void Construct(Value* to, const Value& from) { to->Construct(&from.key, &from.value); }
    static void Destroy(Value* p) { DestroyShapePair(p); }
};
// The shape cache's operator[]: a pair of a copy of the handle and a null RefPtr, find_or_insert of it, the
// pair destroyed. The node's RefPtr.
struct ShapeTable : stlp::hashtable<ShapeTraits> {
    RefPtr* At(const ShapeCacheHandle* handle);
};
static_assert(sizeof(ShapeTable) == 0x14 && sizeof(ShapeTable::Node) == 0x18);

class UnitShapeCache {
public:
    CRefObject* Get(void* handleIn);

    virtual CRefObject* CreateShape(const ShapeCacheHandle* handle);

    // The static initialiser calls it on the global; returns this. An empty map with the first prime number of
    // buckets from 100 up (193), then the vtable (constructed in place).
    UnitShapeCache* Construct();
    void Destruct();

    std::uint32_t unused_04;
    ShapeTable shapes;
};
static_assert(offsetof(UnitShapeCache, shapes) == 8 && sizeof(UnitShapeCache) == 0x1C);

// The cache: built by a static initialiser, destroyed at exit (both in ai_shape_cache.cpp).
extern lib::StaticObject<UnitShapeCache> g_unitShapes;
inline UnitShapeCache* UnitShapes() { return &*g_unitShapes; }
