// CTextureManager's lifetime, Destroy and CreateObject. See texture.h and
// docs/notes/items_spells.md ("Textures").
#include "texture.h"

#include "mod_folders.h"
#include "world_data.h"   // the text resources' folder

#include "game_data.h"

#include <list>
#include <new>
#include <string>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

namespace {

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
    shadowRenderSurface_ = nullptr;
    device_ = nullptr;
    cachedBytes_ = 0;
    mipLevel_ = 0;
    limitTo512_ = 0;
}

// The part Destroy and the destructor share: the shadow surface, the cache, the composed textures
// and the device.
static void ReleaseTextures(CTextureManager* manager) {
    CTexture* shadow = manager->shadowRenderSurface_;
    manager->shadowRenderSurface_ = nullptr;
    if (shadow) shadow->Release();
    for (auto node = manager->cache_.begin(); node != manager->cache_.end();) {
        CTexture* texture = *node;
        ++node;
        texture->Destroy();
        if (texture) delete texture;
    }
    manager->cache_.clear();
    manager->cachedBytes_ = 0;
    DeleteComposedTextures();
    CTextureDevice* device = manager->device_;
    manager->device_ = nullptr;
    if (device) device->Release();
}

int CTextureManager::Create() {
    if (!CBaseManager::Create()) return 0;
    DeleteComposedTextures();
    const std::string folder = TheTextFolder();
    BOOL opened = AddArchive((folder + "textures.res").c_str());
    opened = AddArchive((folder + "redress.res").c_str()) && opened ? 1 : 0;
    if (!AddArchive((folder + "menus.res").c_str()) || !opened) return 0;
    // Rebirth's ResOverride: when all three opened, the mod's texture archives follow.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& path : modfolders::TextureArchives()) CResourceManager::AddArchive(path.c_str());
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
    cache_.clear();
}

CEngineObject* CTextureManager::CreateObject(int type, int id) {
    CEngineObject* object = CBaseManager::CreateObject(type, id);
    object->flags_ |= CreateFlag(type);
    return object;
}

const char* CTextureManager::GetClassName() { return kClassName; }
int CTextureManager::GetClassId() { return 0x26; }
const char* CTextureManager::GetFolderName() { return kFolder; }
