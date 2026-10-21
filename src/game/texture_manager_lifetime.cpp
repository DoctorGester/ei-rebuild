// CTextureManager's lifetime, Destroy and CreateObject (A-H23). See texture.h and
// docs/notes/items_spells.md ("Textures").
#include "texture.h"

#include "mod_folders.h"
#include "world_data.h"   // the text resources' folder

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/crt.h"
#include "game_data.h"

#include <new>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

constexpr int kListBlockSize = 10;
constexpr const char* kClassName = "CTextureManager";
constexpr const char* kFolder = "textures";

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

CTextureManager::CTextureManager() {
    cache_ = mfc::CTypedPtrList<CTexture>{ei::GamePointer<void>(kCacheVtableAddress), nullptr, nullptr, 0,
                                          nullptr, nullptr, kListBlockSize};
    shadowRenderSurface_ = nullptr;
    device_ = nullptr;
    cachedBytes_ = 0;
    mipLevel_ = 0;
    limitTo512_ = 0;
}

CTextureManager* CTextureManager::Construct() { return new (this) CTextureManager(); }
EI_REWRITTEN(0x00691140, CTextureManager::Construct);

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
    if (!AddArchive(folder + "menus.res") || !opened) return 0;
    // Rebirth's ResOverride (006915EF): when all three opened, the mod's texture archives follow.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& path : modfolders::TextureArchives()) AddArchiveImpl(path.c_str());
    }
    return 1;
}

void CTextureManager::Destroy() {
    ReleaseTextures(this);
    CResourceManager::Destroy();
}

CTextureManager::~CTextureManager() {
    ReleaseTextures(this);
    CResourceManager::Destroy();
    SetDevice(nullptr);
    // The members' destructors: both counted pointers are empty by now; the cache's.
    cache_.vtable = ei::GamePointer<void>(kCacheVtableAddress);
    cache_.RemoveAll();
}

void CTextureManager::Destruct() { this->CTextureManager::~CTextureManager(); }
EI_REWRITTEN(0x00691250, CTextureManager::Destruct);

CTextureManager* CTextureManager::DeletingDestruct(unsigned flags) {
    this->CTextureManager::~CTextureManager();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00691230, CTextureManager::DeletingDestruct);

CEngineObject* CTextureManager::CreateObjectImpl(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObjectImpl(type, id);
    object->flags_ |= CreateFlag(type);
    return object;
}

namespace {
int __fastcall TextureManagerCreateHook(CTextureManager* self) { return self->CTextureManager::Create(); }
void __fastcall TextureManagerDestroyHook(CTextureManager* self) { self->CTextureManager::Destroy(); }
} // namespace
EI_REWRITTEN(0x00691500, TextureManagerCreateHook);
EI_REWRITTEN(0x00691630, TextureManagerDestroyHook);
EI_REWRITTEN(0x00691AA0, CTextureManager::CreateObjectImpl);

CEngineObject* CTextureManager::CreateObject(int type, int id) { return CreateObjectImpl(type, id); }
int CTextureManager::ReleaseObject(CEngineObject* object) { return ReleaseObjectImpl(object); }
CEngineObject* CTextureManager::NewObject(int type) { return NewObjectImpl(type); }
const char* CTextureManager::GetClassName() { return kClassName; }
int CTextureManager::GetClassId() { return 0x26; }
const char* CTextureManager::GetFolderName() { return kFolder; }

