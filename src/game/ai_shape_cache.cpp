// 005BD7F0 (thiscall on a reference-counted object cache, RET 4): the object for a handle
// {cache, ref, key, height}. A handle that already holds a live (not detached) object of this
// cache returns it. Otherwise the cache's hash map (+8; buckets +0xC..+0x10, nodes {next, handle
// (0x10 bytes), value}) is searched by key (bucket key % count) and height (compared as floats,
// unordered counting as equal); a found value, or else a new object from the cache's slot 0 that
// is stored under the handle (005BE090, the old value released as an owner and the new one
// shared), becomes the handle's reference. Used for the unit shapes (007C2F70, ai_movement.cpp).
//
// The unit shapes' cache itself is constructed by a static initialiser (before the hooks are
// installed) and destroyed at exit: UnitShapeCache::Construct and Destruct at the end of this file.
#include "ai_shape_cache.h"

#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>

namespace {

using CacheHandle = ShapeCacheHandle;
using CacheNode = UnitShapeCache::Map::Node;

constexpr std::size_t kFirstBuckets = 100;

// 005BE090 (RET 4): the map's value slot for a handle, inserted (empty) when missing.
__declspec(naked) RefPtr* __fastcall CacheSlot(void*, void*, const CacheHandle*) EI_ORIGINAL(0x005BE090)

bool SameHeight(float a, float b) {
    // FCOMP then TEST AH, 0x40: equal, or unordered.
    return a == b || a != a || b != b;
}

} // namespace

#pragma float_control(precise, on, push)

CRefObject* UnitShapeCache::Get(void* handleIn) {
    auto* handle = static_cast<CacheHandle*>(handleIn);
    if (handle->cache == this && handle->ref && !(handle->ref->flags7_ & CRefObject::kDetached)) return handle->ref;

    auto count = static_cast<std::uint32_t>(shapes.BucketCount());
    CacheNode* node = shapes.Bucket(handle->key % count);
    while (node && !SameHeight(node->key.height, handle->height)) node = node->next;
    if (node) {
        CRefObject* value = node->value.ptr;
        handle->cache = this;
        reinterpret_cast<RefPtr*>(&handle->ref)->Assign(value);
        return value;
    }

    CRefObject* created = CreateShape(handle);
    RefPtr* slot = CacheSlot(&shapes, nullptr, handle);
    if (created != slot->ptr) {
        slot->Destroy();
        slot->Share(created);
    }
    handle->cache = this;
    reinterpret_cast<RefPtr*>(&handle->ref)->Assign(created);
    return created;
}

#pragma float_control(pop)

EI_REPLACE_METHOD(0x005BD7F0, UnitShapeCache::Get);

UnitShapeCache* UnitShapeCache::Construct() {
    shapes.InitBuckets(kFirstBuckets);
    ei::SetVtable(this, kVtableAddress);
    return this;
}
EI_REPLACE_METHOD(0x005B32E0, UnitShapeCache::Construct);

void UnitShapeCache::Destruct() {
    shapes.ClearWith([](Map::Node* node) {
        node->value.Destroy();
        if (CRefObject* ref = node->key.ref) ref->Release();
        stlp::deallocate(node, sizeof(Map::Node));
    });
    shapes.Destroy();
}
EI_REPLACE_METHOD(0x005B31F0, UnitShapeCache::Destruct);
