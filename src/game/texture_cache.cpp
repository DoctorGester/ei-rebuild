// The texture manager's cache of released textures: a texture nobody references any more is kept
// (00691B00, the manager's ReleaseObject) and taken back when the game asks for it again by type and
// name (00691BF0, from 00692D30) or by name (00693C60, FindTexture: the material stages 004EB550,
// the terrain decals, 00693F10, 004EB1D0). The cache lowers a cached texture's DirectDraw priority
// to 0 and raises it to 100 when it is taken back.
//
// In the switch-over ("present" with "cpuwindows", ticket R8) a texture's holder may have no
// DirectDraw surface at all (the .mmp textures, the CPU-written and the shadow textures): the
// priority is then left out. The originals do not check the surface. See docs/notes/renderer.md
// (section 16).
#include "texture.h"

#include "device.h"

#include "originals_library.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstring>

namespace {


// Texture flags that make a released texture cacheable (8: loaded).
constexpr std::uint32_t kCacheable = 0x38;
constexpr unsigned kPriorityCached = 0, kPriorityInUse = 100;

// The texture's surface priority (the managed texture's eviction order); nothing for a texture
// without a holder, or with a holder that has no DirectDraw surface.
void SetPriority(const CTexture* texture, unsigned priority) {
    const CDXSurface* holder = texture->surface_;
    if (holder && holder->surface) device::Surface(holder)->SetPriority(priority);
}

} // namespace

// --- 006916F0 ----------------------------------------------------------------------------------------

namespace {
const mfc::CString& DataPathString() { return *reinterpret_cast<const mfc::CString*>(0x007AF460); }
const char* ComposedTexturePattern() { return reinterpret_cast<const char*>(0x00795D58); }   // "textures\*.mmp"
const char* CantDeleteText() { return reinterpret_cast<const char*>(0x00795D34); }   // "Can't delete file \"%s\" due to %d"
} // namespace

void __cdecl DeleteComposedTextures() {
    mfc::CFileFind find;
    mfc::CString pattern = mfc::CString(DataPathString()) + ComposedTexturePattern();
    if (!find.FindFile(pattern, 0)) return;
    int more;
    do {
        more = CFileFindNext(&find);
        alignas(mfc::CString) unsigned char pathSlot[sizeof(mfc::CString)];
        mfc::CString* path = CFileFindPathOriginal(&find, nullptr, reinterpret_cast<mfc::CString*>(pathSlot));
        BOOL deleted = gameapi::DeleteFileA(*path);
        path->~CString();
        if (!deleted) {
            alignas(mfc::CString) unsigned char nameSlot[sizeof(mfc::CString)];
            mfc::CString* name = CFileFindNameOriginal(&find, nullptr, reinterpret_cast<mfc::CString*>(nameSlot));
            game::DebugLog(CantDeleteText(), static_cast<const char*>(*name), gameapi::GetLastError());
            name->~CString();
        }
    } while (more);
}
EI_REPLACE(0x006916F0, DeleteComposedTextures);

// --- 00691B00 ----------------------------------------------------------------------------------------

// Returns 0 (the original's EAX is the priority call's result, or a list node, on the cache path;
// CEngineObject::Release, its caller, returns it but no caller reads it).
int CTextureManager::ReleaseObject(CEngineObject* object) {
    if (!object || object->refCount_ != 0) return 0;
    auto* texture = static_cast<CTexture*>(object);
    if (const auto* node = objects_.Find(static_cast<unsigned>(object->id_))) {
        Unregister(node->value->id_);
        if (object->flags_ & kCacheable) {
            cache_.AddTail(texture);
            cachedBytes_ += texture->uploadResult_;
            SetPriority(texture, kPriorityCached);
            return 0;
        }
    }
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

namespace {
// The virtual slot's entry point (rewrite-guide rule 4).
int __fastcall ReleaseObjectHook(CTextureManager* self, void*, CEngineObject* object) {
    return self->CTextureManager::ReleaseObject(object);
}
} // namespace
EI_REPLACE(0x00691B00, ReleaseObjectHook);

// --- 00691BF0 ----------------------------------------------------------------------------------------

CTexture* CTextureManager::TakeFromCache(int type, const char* name) {
    for (auto* node = cache_.head; node;) {
        auto* next = node->next;
        CTexture* texture = node->data;
        if (texture->type_ == type && std::strcmp(texture->GetName(), name) == 0) {
            cache_.RemoveAt(node);
            cachedBytes_ -= texture->uploadResult_;
            RegisterInstant(texture);
            SetPriority(texture, kPriorityInUse);
            return texture;
        }
        node = next;
    }
    return nullptr;
}
EI_REPLACE_METHOD(0x00691BF0, CTextureManager::TakeFromCache);

// --- 00693C60 ----------------------------------------------------------------------------------------

// Original details kept: a texture taken back from the cache here is not subtracted from
// cachedBytes_ (00691BF0 does subtract it).
CTexture* CTextureManager::FindTexture(const char* name) {
    ObjectListIterator registered{nullptr, nullptr};
    CTexture* found = nullptr;
    for (CEngineObject* object = registered.First(this); object; object = registered.Next()) {
        if (std::strcmp(object->GetName(), name) == 0) {
            found = static_cast<CTexture*>(object);
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
                SetPriority(texture, kPriorityInUse);
                found = texture;
                break;
            }
            node = next;
        }
    }
    if (registered.manager) --registered.manager->refCount_;
    return found;
}
EI_REPLACE_METHOD(0x00693C60, CTextureManager::FindTexture);

// --- 00692D30 ----------------------------------------------------------------------------------------

CTexture* CTextureManager::FindTypedTexture(int type, const char* name) {
    ObjectListIterator registered{nullptr, nullptr};
    CTexture* found = nullptr;
    for (CEngineObject* object = registered.First(this); object; object = registered.Next()) {
        if (object->type_ == type && std::strcmp(object->GetName(), name) == 0) {
            found = static_cast<CTexture*>(object);
            break;
        }
    }
    if (!found) found = TakeFromCache(type, name);
    if (registered.manager) --registered.manager->refCount_;
    return found;
}
EI_REPLACE_METHOD(0x00692D30, CTextureManager::FindTypedTexture);

// --- 00692F00 ----------------------------------------------------------------------------------------

void CTextureManager::FreeCache() {
    for (auto* node = cache_.head; node;) {
        CTexture* texture = node->data;
        node = node->next;
        if (texture) delete texture;
    }
    cachedBytes_ = 0;
    cache_.RemoveAll();
}
EI_REPLACE_METHOD(0x00692F00, CTextureManager::FreeCache);
