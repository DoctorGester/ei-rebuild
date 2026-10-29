// The texture manager's cache of released textures: a texture nobody references any more is kept
// (ReleaseObject) and taken back when the game asks for it again by type and
// name or by name (FindTexture: the material stages,
// the terrain decals). A cached texture's DirectDraw
// priority was lowered to 0 and raised to 100 when it is taken back; no holder has a DirectDraw surface,
// so that is left out. See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "lib/crt.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"


namespace {

} // namespace

// Returns 0 (the result is the priority call's, or a list node, on the cache path;
// CEngineObject::Release, its caller, returns it but no caller reads it).
int CTextureManager::ReleaseObject(CEngineObject* object) {
    if (!object || object->refCount_ != 0) return 0;
    auto* texture = static_cast<CTexture*>(object);
    if (auto node = objects_.find(static_cast<unsigned>(object->id_)); node != objects_.end()) {
        Unregister(node->second->id_);
        if (object->flags_ & TextureFlag::Cacheable) {
            cache_.push_back(texture);
            cachedBytes_ += texture->uploadResult_;
            return 0;
        }
    }
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

CTexture* CTextureManager::TakeFromCache(int type, std::string_view name) {
    for (auto node = cache_.begin(); node != cache_.end();) {
        auto next = std::next(node);
        CTexture* texture = *node;
        if (texture->type_ == type && texture->name_ == name) {
            cache_.erase(node);
            cachedBytes_ -= texture->uploadResult_;
            RegisterInstant(texture);
            return texture;
        }
        node = next;
    }
    return nullptr;
}

// Details kept: a texture taken back from the cache here is not subtracted from
// cachedBytes_ (the by-type lookup does subtract it).
CTexture* CTextureManager::FindTexture(std::string_view name) {
    ObjectListIteratorOf<CTexture> registered{};
    CTexture* found = nullptr;
    for (CTexture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (texture->name_ == name) {
            found = texture;
            break;
        }
    }
    if (!found) {
        for (auto node = cache_.begin(); node != cache_.end();) {
            auto next = std::next(node);
            CTexture* texture = *node;
            if (texture->name_ == name) {
                cache_.erase(node);
                RegisterInstant(texture);
                found = texture;
                break;
            }
            node = next;
        }
    }
    if (registered.manager) --registered.manager->refCount_;
    return found;
}

CTexture* CTextureManager::FindTypedTexture(int type, std::string_view name) {
    ObjectListIteratorOf<CTexture> registered{};
    CTexture* found = nullptr;
    for (CTexture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (texture->type_ == type && texture->name_ == name) {
            found = texture;
            break;
        }
    }
    if (!found) found = TakeFromCache(type, name);
    if (registered.manager) --registered.manager->refCount_;
    return found;
}

void CTextureManager::FreeCache() {
    for (auto node = cache_.begin(); node != cache_.end();) {
        CTexture* texture = *node;
        ++node;
        if (texture) delete texture;
    }
    cachedBytes_ = 0;
    cache_.clear();
}
