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

// The flag CreateObject gives a new texture of `type` (TextureKind).
TextureFlag CreateFlag(int type) {
    switch (static_cast<TextureKind>(type)) {
    case TextureKind::Type3E: return TextureFlag::Type3E;
    case TextureKind::Type3F: return TextureFlag::Type3F;
    case TextureKind::Type40: return TextureFlag::Type40;
    default: return TextureFlag::OtherType;
    }
}

} // namespace

CTextureManager::CTextureManager() {
    device = nullptr;
    cachedBytes = 0;
    mipLevel = 0;
    limitTo512 = 0;
}

// The part Destroy and the destructor share: the cache, the composed textures and the device.
static void ReleaseTextures(CTextureManager* manager) {
    for (auto node = manager->cache.begin(); node != manager->cache.end();) {
        CTexture* texture = *node;
        ++node;
        texture->Destroy();
        if (texture) delete texture;
    }
    manager->cache.clear();
    manager->cachedBytes = 0;
    ForgetComposedTextures();
    CTextureDevice* device = manager->device;
    manager->device = nullptr;
    if (device) device->Release();
}

void CTextureManager::Create() {
    CBaseManager::Create();
    ForgetComposedTextures();
    const std::string folder = g_textFolder;
    bool opened = AddArchive((folder + "textures.res"));
    opened = AddArchive((folder + "redress.res")) && opened;
    if (!AddArchive((folder + "menus.res")) || !opened) {
        context = nullptr;    // the original's Init cleared the context after this failed Create
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
    cache.clear();
}

CEngineObject* CTextureManager::CreateObject(int newType, int newId) {
    CEngineObject* object = CBaseManager::CreateObject(newType, newId);
    object->flags |= CreateFlag(newType);
    return object;
}

const char* CTextureManager::ClassName() { return kClassName; }
ClassId CTextureManager::GetClassId() { return ClassId::TextureManager; }
const char* CTextureManager::GetFolderName() { return kFolder; }
