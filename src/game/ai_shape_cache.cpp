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

#include "ei/hook.h"
#include "game_hashes.h"

#include <cstdint>
#include <new>

namespace {

using CacheHandle = ShapeCacheHandle;
using CacheNode = ShapeTable::Node;

constexpr std::size_t kFirstBuckets = 100;


bool SameHeight(float a, float b) {
    // FCOMP then TEST AH, 0x40: equal, or unordered.
    return a == b || a != a || b != b;
}

} // namespace

CRefObject* UnitShapeCache::Get(void* handleIn) {
    auto* handle = static_cast<CacheHandle*>(handleIn);
    if (handle->cache == this && handle->ref && !(handle->ref->flags7_ & CRefObject::kDetached)) return handle->ref;

    auto count = static_cast<std::uint32_t>(shapes.BucketCount());
    CacheNode* node = shapes.BucketVector()[handle->key % count];
    while (node && !SameHeight(node->value.key.height, handle->height)) node = node->next;
    if (node) {
        CRefObject* value = node->value.value.ptr;
        handle->cache = this;
        handle->counted.Assign(value);
        return value;
    }

    CRefObject* created = CreateShape(handle);
    // The map's value slot for a handle, inserted (empty) when missing.
    RefPtr* slot = shapes.At(handle);
    if (created != slot->ptr) {
        slot->Destroy();
        slot->Share(created);
    }
    handle->cache = this;
    handle->counted.Assign(created);
    return created;
}

EI_REWRITTEN(0x005BD7F0, UnitShapeCache::Get);
CRefObject* UnitShapeCache::CreateShape(const ShapeCacheHandle* handle) { return CreateUnitShape(handle); }

UnitShapeCache* UnitShapeCache::Construct() {
    new (this) UnitShapeCache;   // the vtable (the map's default constructor only nulls its bucket vector)
    shapes.InitZeroedBuckets(kFirstBuckets);
    return this;
}
EI_REWRITTEN(0x005B32E0, UnitShapeCache::Construct);

void UnitShapeCache::Destruct() {
    shapes.Destroy();
}
EI_REWRITTEN(0x005B31F0, UnitShapeCache::Destruct);

namespace {
void __cdecl DestroyUnitShapes() { g_unitShapes->Destruct(); }
void __cdecl ConstructUnitShapes() {
    g_unitShapes->Construct();
    libcrt::AtExit(DestroyUnitShapes);
}
} // namespace

EI_REPLACE(0x005B31C0, ConstructUnitShapes);
EI_REWRITTEN(0x005B31E0, DestroyUnitShapes);
