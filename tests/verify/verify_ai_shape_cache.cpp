// Checks for the object cache lookup (src/game/ai_shape_cache.cpp). Ours only (it may create and
// store an object): afterwards the handle belongs to the cache and references the result.
#include "verify.h"
#include "../../src/game/ai_shape_cache.h"

#include <cstdint>

namespace {
void* s_unused;
}

// 005BD7F0.
static CRefObject* __fastcall CacheGetVerify(UnitShapeCache* cache, void*, void* handle) {
    CRefObject* result = cache->Get(handle);
    const auto* fields = static_cast<const ShapeCacheHandle*>(handle);
    bool ok = fields->cache == cache && fields->ref == result;
    ei::verify::Check("CacheGet", true, ok, "%p", result);
    return result;
}
EI_VERIFY(0x005BD7F0, CacheGetVerify, 0, s_unused);
