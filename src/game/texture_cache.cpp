// The texture manager's cache of released textures: a texture nobody references any more is kept
// (ReleaseObject) and taken back when the game asks for it again by type and
// name or by name (FindTexture: the material stages,
// the terrain decals). A cached texture's DirectDraw
// priority was lowered to 0 and raised to 100 when it is taken back; no holder has a DirectDraw surface,
// so that is left out. See docs/notes/renderer.md (section 7).
#include "texture.h"


#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"
#include "game_data.h"
#include "game_singletons.h"

#include <cstring>
#include <string>

namespace {


// Texture flags that make a released texture cacheable (8: loaded).
constexpr std::uint32_t kCacheable = 0x38;

} // namespace


namespace {
const char* ComposedTexturePattern() { return "textures\\*.mmp"; }
const char* CantDeleteText() { return "Can't delete file \"%s\" due to %d.\n"; }
} // namespace

void __cdecl DeleteComposedTextures() {
    mfc::CFileFind find;
    std::string pattern = (mfc::CString(TheDataFolder()) + ComposedTexturePattern()).GetString();
    if (!find.FindFile(pattern.c_str(), 0)) return;
    int more;
    do {
        more = find.FindNext();
        BOOL deleted = gameapi::DeleteFileA(find.GetFilePath());
        if (!deleted) {
            const std::string name = find.GetFileName().GetString();
            game::DebugLog(CantDeleteText(), name.c_str(), gameapi::GetLastError());
        }
    } while (more);
}
EI_REWRITTEN(0x006916F0, DeleteComposedTextures);


// Returns 0 (the result is the priority call's, or a list node, on the cache path;
// CEngineObject::Release, its caller, returns it but no caller reads it).
int CTextureManager::ReleaseObject(CEngineObject* object) {
    if (!object || object->refCount_ != 0) return 0;
    auto* texture = static_cast<CTexture*>(object);
    if (const auto* node = objects_.Find(static_cast<unsigned>(object->id_))) {
        Unregister(node->value->id_);
        if (object->flags_ & kCacheable) {
            cache_.AddTail(texture);
            cachedBytes_ += texture->uploadResult_;
            return 0;
        }
    }
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

namespace {
// The virtual slot's entry point (rewrite-guide rule 4).
} // namespace
EI_REWRITTEN(0x00691B00, CTextureManager::ReleaseObject);


CTexture* CTextureManager::TakeFromCache(int type, const char* name) {
    for (auto* node = cache_.head; node;) {
        auto* next = node->next;
        CTexture* texture = node->data;
        if (texture->type_ == type && std::strcmp(texture->GetName(), name) == 0) {
            cache_.RemoveAt(node);
            cachedBytes_ -= texture->uploadResult_;
            RegisterInstant(texture);
            return texture;
        }
        node = next;
    }
    return nullptr;
}
EI_REWRITTEN(0x00691BF0, CTextureManager::TakeFromCache);


// Details kept: a texture taken back from the cache here is not subtracted from
// cachedBytes_ (the by-type lookup does subtract it).
CTexture* CTextureManager::FindTexture(const char* name) {
    ObjectListIteratorOf<CTexture> registered{};
    CTexture* found = nullptr;
    for (CTexture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (std::strcmp(texture->GetName(), name) == 0) {
            found = texture;
            break;
        }
    }
    if (!found) {
        for (auto* node = cache_.head; node;) {
            auto* next = node->next;
            CTexture* texture = node->data;
            if (std::strcmp(texture->GetName(), name) == 0) {
                cache_.RemoveAt(node);
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
EI_REWRITTEN(0x00693C60, CTextureManager::FindTexture);


CTexture* CTextureManager::FindTypedTexture(int type, const char* name) {
    ObjectListIteratorOf<CTexture> registered{};
    CTexture* found = nullptr;
    for (CTexture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (texture->type_ == type && std::strcmp(texture->GetName(), name) == 0) {
            found = texture;
            break;
        }
    }
    if (!found) found = TakeFromCache(type, name);
    if (registered.manager) --registered.manager->refCount_;
    return found;
}
EI_REWRITTEN(0x00692D30, CTextureManager::FindTypedTexture);


void CTextureManager::FreeCache() {
    for (auto* node = cache_.head; node;) {
        CTexture* texture = node->data;
        node = node->next;
        if (texture) delete texture;
    }
    cachedBytes_ = 0;
    cache_.RemoveAll();
}
EI_REWRITTEN(0x00692F00, CTextureManager::FreeCache);
