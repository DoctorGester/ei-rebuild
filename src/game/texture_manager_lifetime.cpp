// CTextureManager's lifetime, Destroy and CreateObject (A-H23). See texture.h and
// docs/notes/items_spells.md ("Textures").
#include "texture.h"

#include "world_data.h"   // the text resources' folder

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"

namespace {

constexpr int kListBlockSize = 10;
constexpr std::uintptr_t kClassName = std::uintptr_t(0x00795D08);   // "CTextureManager"
constexpr std::uintptr_t kFolder = std::uintptr_t(0x00795CFC);      // "textures"

// The flag CreateObject gives a new texture of `type` (0x3D and unknown types 0x40).
std::uint32_t CreateFlag(int type) {
    switch (type) {
    case 0x3E: return 0x80;
    case 0x3F: return 0x100;
    case 0x40: return 0x200;
    default: return 0x40;
    }
}

} // namespace

CTextureManager* CTextureManager::Construct() {
    CResourceManager::Construct();
    cache_ = mfc::CTypedPtrList<CTexture>{reinterpret_cast<void*>(kCacheVtableAddress), nullptr, nullptr, 0,
                                          nullptr, nullptr, kListBlockSize};
    shadowRenderSurface_ = nullptr;
    device_ = nullptr;
    ei::SetVtable(this, kVtableAddress);
    cachedBytes_ = 0;
    mipLevel_ = 0;
    limitTo512_ = 0;
    return this;
}
EI_REPLACE_METHOD(0x00691140, CTextureManager::Construct);

// The part Destroy and the destructor share: the shadow surface, the cache, the composed textures
// and the device.
static void ReleaseTextures(CTextureManager* manager) {
    CTexture* shadow = manager->shadowRenderSurface_;
    manager->shadowRenderSurface_ = nullptr;
    if (shadow) shadow->Release();
    for (auto* node = manager->cache_.head; node;) {
        CTexture* texture = node->data;
        node = node->next;
        texture->Destroy();
        if (texture) delete texture;
    }
    manager->cache_.RemoveAll();
    manager->cachedBytes_ = 0;
    DeleteComposedTextures();
    CTextureDevice* device = manager->device_;
    manager->device_ = nullptr;
    if (device) device->Release();
}

int CTextureManager::Create() {
    if (!CBaseManager::Create()) return 0;
    DeleteComposedTextures();
    const mfc::CString folder(*static_cast<const mfc::CString*>(worlddata::TextPathString()));
    BOOL opened = AddArchive(folder + "textures.res");
    opened = AddArchive(folder + "redress.res") && opened ? 1 : 0;
    return AddArchive(folder + "menus.res") && opened ? 1 : 0;
}

void CTextureManager::Destroy() {
    ReleaseTextures(this);
    CResourceManager::Destroy();
}

void CTextureManager::Destruct() {
    ei::SetVtable(this, kVtableAddress);
    ReleaseTextures(this);
    CResourceManager::Destroy();
    SetDevice(nullptr);
    // The members' destructors: both counted pointers are empty by now; the cache's.
    cache_.vtable = reinterpret_cast<void*>(kCacheVtableAddress);
    cache_.RemoveAll();
    CResourceManager::Destruct();
}
EI_REPLACE_METHOD(0x00691250, CTextureManager::Destruct);

CTextureManager* CTextureManager::DeletingDestruct(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x00691230, CTextureManager::DeletingDestruct);

CEngineObject* CTextureManager::CreateObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObject(type, id);
    object->flags_ |= CreateFlag(type);
    return object;
}

namespace {
int __fastcall TextureManagerCreateHook(CTextureManager* self) { return self->CTextureManager::Create(); }
void __fastcall TextureManagerDestroyHook(CTextureManager* self) { self->CTextureManager::Destroy(); }
CEngineObject* __fastcall TextureManagerCreateObjectHook(CTextureManager* self, void*, int type, int id) {
    return self->CTextureManager::CreateObject(type, id);
}
} // namespace
EI_REPLACE(0x00691500, TextureManagerCreateHook);
EI_REPLACE(0x00691630, TextureManagerDestroyHook);
EI_REPLACE(0x00691AA0, TextureManagerCreateObjectHook);

const char* __fastcall TextureManagerClassName(CTextureManager*) { return reinterpret_cast<const char*>(kClassName); }
int __fastcall TextureManagerClassId(CTextureManager*) { return 0x26; }
const char* __fastcall TextureManagerFolder(CTextureManager*) { return reinterpret_cast<const char*>(kFolder); }
EI_REPLACE(0x00691210, TextureManagerClassName);
EI_REPLACE(0x00691220, TextureManagerClassId);
EI_REPLACE(0x00691200, TextureManagerFolder);
