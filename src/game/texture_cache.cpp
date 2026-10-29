// The texture manager's cache of released textures: a texture nobody references any more is kept
// (ReleaseObject) and taken back when the game asks for it again by type and
// name or by name (FindTexture: the material stages,
// the terrain decals). A cached texture's DirectDraw
// priority was lowered to 0 and raised to 100 when it is taken back; no holder has a DirectDraw surface,
// so that is left out. See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "lib/crt.h"
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"


namespace {

} // namespace

// Returns 0 (the result is the priority call's, or a list node, on the cache path;
// EngineObject::Release, its caller, returns it but no caller reads it).
int CTextureManager::ReleaseObject(EngineObject* object) {
    if (!object || object->refCount != 0) return 0;
    auto* texture = static_cast<CTexture*>(object);
    if (auto node = objects.find(static_cast<unsigned>(object->id)); node != objects.end()) {
        Unregister(node->second->id);
        if (object->flags & TextureFlag::Cacheable) {
            cache.push_back(texture);
            cachedBytes += texture->uploadResult;
            return 0;
        }
    }
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

CTexture* CTextureManager::TakeFromCache(int textureType, std::string_view name) {
    auto node = std::ranges::find_if(cache, [&](const CTexture* texture) { return texture->type == textureType && texture->name == name; });
    if (node == cache.end()) return nullptr;
    CTexture* texture = *node;
    cache.erase(node);
    cachedBytes -= texture->uploadResult;
    RegisterInstant(texture);
    return texture;
}

// Details kept: a texture taken back from the cache here is not subtracted from
// cachedBytes (the by-type lookup does subtract it).
CTexture* CTextureManager::FindTexture(std::string_view name) {
    ObjectListIteratorOf<CTexture> registered{};
    CTexture* found = nullptr;
    for (CTexture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (texture->name == name) {
            found = texture;
            break;
        }
    }
    if (!found) {
        auto node = std::ranges::find_if(cache, [&](const CTexture* texture) { return texture->name == name; });
        if (node != cache.end()) {
            found = *node;
            cache.erase(node);
            RegisterInstant(found);
        }
    }
    if (registered.manager) --registered.manager->refCount;
    return found;
}

CTexture* CTextureManager::FindTypedTexture(int textureType, std::string_view name) {
    ObjectListIteratorOf<CTexture> registered{};
    CTexture* found = nullptr;
    for (CTexture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (texture->type == textureType && texture->name == name) {
            found = texture;
            break;
        }
    }
    if (!found) found = TakeFromCache(textureType, name);
    if (registered.manager) --registered.manager->refCount;
    return found;
}

void CTextureManager::FreeCache() {
    for (auto node = cache.begin(); node != cache.end();) {
        CTexture* texture = *node;
        ++node;
        if (texture) delete texture;
    }
    cachedBytes = 0;
    cache.clear();
}
