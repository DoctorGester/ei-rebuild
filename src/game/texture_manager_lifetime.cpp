// CTextureManager's lifetime, Destroy and CreateObject. See texture.h and
// docs/notes/items_spells.md ("Textures").
#include "game_data.h"
#include "mod_folders.h"
#include "texture.h"
#include "world_data.h"   // the text resources' folder

#include <list>
#include <new>
#include <string>

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
    device_ = nullptr;
    cachedBytes_ = 0;
    mipLevel_ = 0;
    limitTo512_ = 0;
}

// The part Destroy and the destructor share: the cache, the composed textures and the device.
static void ReleaseTextures(CTextureManager* manager) {
    for (auto node = manager->cache_.begin(); node != manager->cache_.end();) {
        CTexture* texture = *node;
        ++node;
        texture->Destroy();
        if (texture) delete texture;
    }
    manager->cache_.clear();
    manager->cachedBytes_ = 0;
    ForgetComposedTextures();
    CTextureDevice* device = manager->device_;
    manager->device_ = nullptr;
    if (device) device->Release();
}

void CTextureManager::Create() {
    CBaseManager::Create();
    ForgetComposedTextures();
    const std::string folder = g_textFolder;
    bool opened = AddArchive((folder + "textures.res"));
    opened = AddArchive((folder + "redress.res")) && opened;
    if (!AddArchive((folder + "menus.res")) || !opened) {
        context_ = nullptr;   // the original's Init cleared the context after this failed Create
        return;
    }
    // Rebirth's ResOverride: when all three opened, the mod's texture archives follow.
    if (modfolders::Active(ei::rebirth::Patch::ResOverride)) {
        for (const std::string& path : modfolders::TextureArchives()) CResourceManager::AddArchive(path);
    }
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

const char* CTextureManager::ClassName() { return kClassName; }
ClassId CTextureManager::GetClassId() { return ClassId::TextureManager; }
const char* CTextureManager::GetFolderName() { return kFolder; }
