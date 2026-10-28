// The texture manager's cache of released textures: a texture nobody references any more is kept
// (ReleaseObject) and taken back when the game asks for it again by type and
// name or by name (FindTexture: the material stages,
// the terrain decals). A cached texture's DirectDraw
// priority was lowered to 0 and raised to 100 when it is taken back; no holder has a DirectDraw surface,
// so that is left out. See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "lib/file_io.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstdint>
#include <cstring>
#include <string>
#include "game_singletons.h"


namespace {

// Texture flags that make a released texture cacheable (8: loaded).
constexpr std::uint32_t kCacheable = 0x38;

const char* ComposedTexturePattern() { return "textures\\*.mmp"; }
const char* CantDeleteText() { return "Can't delete file \"%s\" due to %d.\n"; }

// The virtual slot's entry point (rewrite-guide rule 4).

} // namespace

void DeleteComposedTextures() {
    lib::FileFind find;
    std::string pattern = std::string(*g_dataFolder) + ComposedTexturePattern();
    if (!find.FindFile(pattern)) return;
    int more;
    do {
        more = find.FindNext();
        BOOL deleted = gameapi::DeleteFileA(find.GetFilePath().c_str());
        if (!deleted) {
            const std::string name = find.GetFileName();
            game::DebugLog(CantDeleteText(), name.c_str(), gameapi::GetLastError());
        }
    } while (more);
}

// Returns 0 (the result is the priority call's, or a list node, on the cache path;
// CEngineObject::Release, its caller, returns it but no caller reads it).
int CTextureManager::ReleaseObject(CEngineObject* object) {
    if (!object || object->refCount_ != 0) return 0;
    auto* texture = static_cast<CTexture*>(object);
    if (auto node = objects_.find(static_cast<unsigned>(object->id_)); node != objects_.end()) {
        Unregister(node->second->id_);
        if (object->flags_ & kCacheable) {
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
