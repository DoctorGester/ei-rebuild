// Checks for the object cache lookup (src/game/ai_shape_cache.cpp). Ours only (it may create and
// store an object): afterwards the handle belongs to the cache and references the result.
#include "verify.h"

#include <cstdint>

class CRefObject;
CRefObject* __fastcall CacheGet(std::uint8_t* cache, void*, void* handle);

namespace {
void* s_unused;
}

// 005BD7F0.
static CRefObject* __fastcall CacheGetVerify(std::uint8_t* cache, void*, void* handle) {
    CRefObject* result = CacheGet(cache, nullptr, handle);
    auto** fields = static_cast<void**>(handle);
    bool ok = fields[0] == cache && fields[1] == result;
    ei::verify::Check("CacheGet", true, ok, "%p", result);
    return result;
}
EI_VERIFY(0x005BD7F0, CacheGetVerify, 0, s_unused);
