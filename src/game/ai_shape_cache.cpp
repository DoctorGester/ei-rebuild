// 005BD7F0 (thiscall on a reference-counted object cache, RET 4): the object for a handle
// {cache, ref, key, height}. A handle that already holds a live (not detached) object of this
// cache returns it. Otherwise the cache's hash map (+8; buckets +0xC..+0x10, nodes {next, handle
// (0x10 bytes), value}) is searched by key (bucket key % count) and height (compared as floats,
// unordered counting as equal); a found value, or else a new object from the cache's slot 0 that
// is stored under the handle (005BE090, the old value released as an owner and the new one
// shared), becomes the handle's reference. Used for the unit shapes (007C2F70, ai_movement.cpp).
#include "core_refcount.h"

#include "ei/hook.h"
#include "ei/vcall.h"

#include <cstdint>

namespace {

struct CacheHandle {
    void* cache;
    CRefObject* ref;
    std::uint32_t key;
    float height;
};

struct CacheNode {
    CacheNode* next;
    CacheHandle handle;
    CRefObject* value;
};

// 005BE090 (RET 4): the map's value slot for a handle, inserted (empty) when missing.
__declspec(naked) RefPtr* __fastcall CacheSlot(void*, void*, const CacheHandle*) EI_ORIGINAL(0x005BE090)

bool SameHeight(float a, float b) {
    // FCOMP then TEST AH, 0x40: equal, or unordered.
    return a == b || a != a || b != b;
}

} // namespace

#pragma float_control(precise, on, push)

CRefObject* __fastcall CacheGet(std::uint8_t* cache, void*, void* handleIn) {
    auto* handle = static_cast<CacheHandle*>(handleIn);
    if (handle->cache == cache && handle->ref && !(handle->ref->flags7_ & CRefObject::kDetached)) return handle->ref;

    CacheNode** begin = *reinterpret_cast<CacheNode***>(cache + 0xC);
    CacheNode** end = *reinterpret_cast<CacheNode***>(cache + 0x10);
    auto count = static_cast<std::uint32_t>(end - begin);
    CacheNode* node = begin[handle->key % count];
    while (node && !SameHeight(node->handle.height, handle->height)) node = node->next;
    if (node) {
        CRefObject* value = node->value;
        handle->cache = cache;
        reinterpret_cast<RefPtr*>(&handle->ref)->Assign(value);
        return value;
    }

    CRefObject* created = ei::VCall<CRefObject*>(cache, 0, handle);
    RefPtr* slot = CacheSlot(cache + 8, nullptr, handle);
    if (created != slot->ptr) {
        slot->Destroy();
        slot->Share(created);
    }
    handle->cache = cache;
    reinterpret_cast<RefPtr*>(&handle->ref)->Assign(created);
    return created;
}

#pragma float_control(pop)

EI_REPLACE(0x005BD7F0, CacheGet);
