// Checks for the texture manager's cache (texture_cache.cpp). All three change the manager's
// registration tables and its cache list: ours only, with what must hold afterwards. Reached in
// every scenario with a new game (the figures' and materials' textures are found by name through
// 00500A00 -> 00692D30 -> 00691BF0 and 004EB550 -> 00693C60; textures are released at zone
// changes and when windows close): frames_basic, render_scene, render_present, ui_saveload.
#include "verify.h"

#include "../../src/game/texture.h"

#include <cstring>

namespace {

bool Registered(CTextureManager* manager, const CEngineObject* object) {
    return manager->FindObject(static_cast<unsigned>(object->id_)) == object;
}

bool Cached(CTextureManager* manager, const CTexture* texture) { return manager->cache_.Find(texture) != nullptr; }

// The first cached texture of `type` (any type when `type` is -1) named `name`, or null.
const CTexture* CachedNamed(CTextureManager* manager, int type, const char* name) {
    for (auto* node = manager->cache_.head; node; node = node->next) {
        CTexture* texture = node->data;
        if ((type == -1 || texture->type_ == type) && std::strcmp(texture->GetName(), name) == 0) return texture;
    }
    return nullptr;
}

} // namespace

// --- 00691B00 ----------------------------------------------------------------------------------------

// Ours only. Result 0. With references (or null) nothing changes; a registered texture with a flag of
// 0x38 ends at the cache's tail, unregistered, the cached bytes up by its byte size; any other is
// unregistered and without owner (kept as a spare).
static void* s_releaseObject;
static int __fastcall ReleaseObjectVerify(CTextureManager* self, void*, CEngineObject* object) {
    bool idle = object && object->refCount_ == 0;
    bool cacheable = idle && Registered(self, object) && (object->flags_ & 0x38);
    int count = self->cache_.count;
    int bytes = self->cachedBytes_;
    int result = self->CTextureManager::ReleaseObjectImpl(object);
    const char* problem = nullptr;
    if (result != 0) {
        problem = "result";
    } else if (!idle) {
        if (self->cache_.count != count || self->cachedBytes_ != bytes) problem = "cache changed";
    } else if (cacheable) {
        auto* texture = static_cast<CTexture*>(object);
        if (self->cache_.count != count + 1 || !self->cache_.tail || self->cache_.tail->data != texture)
            problem = "not cached";
        else if (self->cachedBytes_ != bytes + texture->uploadResult_)
            problem = "cached bytes";
        else if (Registered(self, object))
            problem = "still registered";
    } else if (Registered(self, object) || object->owner_ || self->cache_.count != count) {
        problem = "not released";
    }
    ei::verify::Check("CTextureManager::ReleaseObject", true, problem == nullptr, "%p flags %X: %s",
                      static_cast<void*>(object), object ? object->flags_ : 0, problem ? problem : "ok");
    return result;
}
EI_VERIFY(0x00691B00, ReleaseObjectVerify, 0, s_releaseObject);

// --- 00691BF0 ----------------------------------------------------------------------------------------

// Ours only. The cached texture of the type and name, out of the cache and registered, the cached
// bytes down by its byte size; null when the cache has none (nothing changed).
static void* s_takeFromCache;
static CTexture* __fastcall TakeFromCacheVerify(CTextureManager* self, void*, int type, const char* name) {
    const CTexture* expected = CachedNamed(self, type, name);
    int count = self->cache_.count;
    int bytes = self->cachedBytes_;
    CTexture* texture = self->TakeFromCache(type, name);
    const char* problem = nullptr;
    if (texture != expected) {
        problem = "not the cached texture";
    } else if (!texture) {
        if (self->cache_.count != count || self->cachedBytes_ != bytes) problem = "cache changed";
    } else if (self->cache_.count != count - 1 || Cached(self, texture) || !Registered(self, texture)) {
        problem = "not taken back";
    } else if (self->cachedBytes_ != bytes - texture->uploadResult_) {
        problem = "cached bytes";
    }
    ei::verify::Check("CTextureManager::TakeFromCache", true, problem == nullptr, "type %d \"%s\": %s", type, name,
                      problem ? problem : "ok");
    return texture;
}
EI_VERIFY(0x00691BF0, TakeFromCacheVerify, 0, s_takeFromCache);

// --- 00693C60 ----------------------------------------------------------------------------------------

// Ours only. A registered texture of that name (unchanged cache); else the cached one, out of the
// cache and registered, the cached bytes unchanged (the original's); else null.
static void* s_findTexture;
static CTexture* __fastcall FindTextureVerify(CTextureManager* self, void*, const char* name) {
    const CTexture* cached = CachedNamed(self, -1, name);
    int count = self->cache_.count;
    int bytes = self->cachedBytes_;
    CTexture* texture = self->FindTexture(name);
    const char* problem = nullptr;
    if (!texture) {
        if (cached || self->cache_.count != count) problem = "a cached texture not found";
    } else if (std::strcmp(texture->GetName(), name) != 0 || !Registered(self, texture)) {
        problem = "not a registered texture of the name";
    } else if (self->cachedBytes_ != bytes) {
        problem = "cached bytes";
    } else if (self->cache_.count != count && (texture != cached || self->cache_.count != count - 1)) {
        problem = "cache";
    }
    ei::verify::Check("CTextureManager::FindTexture", true, problem == nullptr, "\"%s\": %s", name,
                      problem ? problem : "ok");
    return texture;
}
EI_VERIFY(0x00693C60, FindTextureVerify, 0, s_findTexture);

// 006916F0 deletes files: ours only, counted; nothing checked afterwards, since what a delete does
// in a harness run is the overlay's business (the game folder itself is never changed). Reached at
// startup and when the client world is set up (every scenario).
static void* s_deleteComposed;
static void __cdecl DeleteComposedVerify() { DeleteComposedTextures(); }
EI_VERIFY(0x006916F0, DeleteComposedVerify, 0, s_deleteComposed);

// --- 00692D30 ----------------------------------------------------------------------------------------

// Ours only. A registered texture of the type and name (unchanged cache); else the cached one taken
// back as TakeFromCache does (the cached bytes down by its size); else null with the cache unchanged.
static void* s_findTypedTexture;
static CTexture* __fastcall FindTypedTextureVerify(CTextureManager* self, void*, int type, const char* name) {
    const CTexture* cached = CachedNamed(self, type, name);
    int count = self->cache_.count;
    int bytes = self->cachedBytes_;
    int references = self->refCount_;
    CTexture* texture = self->FindTypedTexture(type, name);
    const char* problem = nullptr;
    if (self->refCount_ != references) {
        problem = "manager reference";
    } else if (!texture) {
        if (cached || self->cache_.count != count) problem = "a cached texture not found";
    } else if (texture->type_ != type || std::strcmp(texture->GetName(), name) != 0 || !Registered(self, texture)) {
        problem = "not a registered texture of the type and name";
    } else if (self->cache_.count != count &&
               (texture != cached || self->cache_.count != count - 1 || self->cachedBytes_ != bytes - texture->uploadResult_)) {
        problem = "cache";
    } else if (self->cache_.count == count && self->cachedBytes_ != bytes) {
        problem = "cached bytes";
    }
    ei::verify::Check("CTextureManager::FindTypedTexture", true, problem == nullptr, "type %d \"%s\": %s", type, name,
                      problem ? problem : "ok");
    return texture;
}
EI_VERIFY(0x00692D30, FindTypedTextureVerify, 0, s_findTypedTexture);

// --- 00692F00 ----------------------------------------------------------------------------------------

// Ours only (deletes textures): the cache empty, no cached bytes. Reached when the client loads a
// zone (00567AB0): every scenario with a new game.
static void* s_freeCache;
static void __fastcall FreeCacheVerify(CTextureManager* self) {
    self->FreeCache();
    ei::verify::Check("CTextureManager::FreeCache", true,
                      self->cache_.count == 0 && self->cache_.head == nullptr && self->cache_.blocks == nullptr &&
                          self->cachedBytes_ == 0,
                      "manager %p", static_cast<void*>(self));
}
EI_VERIFY(0x00692F00, FreeCacheVerify, 0, s_freeCache);
