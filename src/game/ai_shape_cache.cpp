// The object for a handle {cache, ref, key, height} of a reference-counted object cache. A handle that
// already holds a live (not detached) object of this cache returns it. Otherwise the cache's hash map (nodes
// {next, handle (0x10 bytes), value}) is searched by key (bucket key % count) and height (compared as floats,
// unordered counting as equal); a found value, or else a new object from the cache's creator virtual that is
// stored under the handle (the old value released as an owner and the new one shared), becomes the handle's
// reference. Used for the unit shapes (ai_movement.cpp). The unit shapes' cache itself is constructed by a
// static initialiser and destroyed at exit: UnitShapeCache::Construct and Destruct at the end of this file.
#include "ai_shape_cache.h"
#include "ai_unit_layer.h"

#include "core_refcount.h"

#include <cstdlib>
#include <memory>
#include <new>

CRefObject* UnitShapeCache::Get(void* handleIn) {
    auto* handle = static_cast<ShapeCacheHandle*>(handleIn);
    if (handle->cache == this && IsLive(handle->ref)) return handle->ref;
    return Resolve(handle);
}

CRefObject* UnitShapeCache::CreateShape(const ShapeCacheHandle* handle) { return CreateUnitShape(handle); }

UnitShapeCache* UnitShapeCache::Construct() { return new (this) UnitShapeCache; }

void UnitShapeCache::Destruct() { std::destroy_at(&shapes); }

namespace {
void DestroyUnitShapes() { g_unitShapes->Destruct(); }
} // namespace

void ConstructUnitShapes() {
    g_unitShapes->Construct();
    std::atexit(DestroyUnitShapes);
}
