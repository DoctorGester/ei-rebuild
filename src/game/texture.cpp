#include "texture.h"

#include "app.h"
#include "game_data.h"
#include "game_singletons.h"
#include "lib/crt.h"
#include "lib/file_io.h"
#include "lib/str.h"
#include "mod_folders.h"
#include "options.h"
#include "world_data.h"   // the text resources' folder

#include <cstring>
#include <list>
#include <new>
#include <string>

namespace {
// Game-wide paths set at startup. Names provisional.
const std::string& GameRoot() { return options::g_options.workPath; }

constexpr std::uint32_t kDxtCode = 'D' | ('X' << 8) | ('T' << 16);   // a DXT format's code without its digit
constexpr std::uint32_t kFormat565 = 0x5650, kFormat1555 = 0x5551, kFormat4444 = 0x4444, kFormat8888 = 0x8888;

// Texture format of a .bmp texture, by texture kind.
std::uint32_t BmpFormatFor(int type) {
    switch (type) {
    case 1: case 2: case 3: case 4: case 8: return 0x5551;   // 1-bit alpha
    case 6: case 13: return 0x4444;                         // 4-bit alpha
    default: return 0x5650;                                 // no alpha
    }
}


} // namespace

// The list the old device offered. Rebirth's battle screen texture (battle00, 1536x1536) and footprints are DXT5:
// without it no format is found, LoadTexture fails (Rebirth's DisableExitOnCrashReport goes on), the battle screen
// misses a texture and its sprites bind garbage.
bool IsTextureFormat(std::uint32_t id) {
    if (id == kFormat565 || id == kFormat1555 || id == kFormat4444 || id == kFormat8888) return true;
    const std::uint32_t digit = id >> 24;
    return (id & 0x00FFFFFF) == kDxtCode && digit >= '1' && digit <= '5';
}

render::PixelFormat TextureFormat(std::uint32_t id) {
    switch (id) {
    case kFormat565: return render::PixelFormat{16, 0xF800, 0x07E0, 0x001F, 0};
    case kFormat1555: return render::PixelFormat{16, 0x7C00, 0x03E0, 0x001F, 0x8000};
    case kFormat4444: return render::PixelFormat{16, 0x0F00, 0x00F0, 0x000F, 0xF000};
    case kFormat8888: return render::PixelFormat{32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000u};
    default: return render::PixelFormat{};
    }
}

std::uint32_t MmpHeader::HeldFormat() const {
    if (IsTextureFormat(format)) return format;
    if ((format & 0x00FFFFFF) != kDxtCode) return 0;
    // A DXT code outside the list: the RGB format its channel bit counts name (the loader decodes into it).
    if (redBits == 5 && greenBits == 6 && blueBits == 5 && alphaKind == 0) return kFormat565;
    if (redBits == 5 && greenBits == 5 && blueBits == 5 && alphaKind == 1) return kFormat1555;
    if (redBits == 4 && greenBits == 4 && blueBits == 4 && alphaKind == 4) return kFormat4444;
    if (redBits == 8 && greenBits == 8 && blueBits == 8 && alphaKind == 8) return kFormat8888;
    return 0;
}

int CTextureManager::LoadTexture(CTexture* texture) {
    if (!texture) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return 0;
    }
    if (texture->flags & TextureFlag::Loaded) {
        game::DebugLog("Load already loaded texture \"%s\".\n", texture->GetName());
        return 1;
    }

    lib::FileFind finder;
    std::string path = GameRoot() + "textures\\" + texture->GetName() + ".bmp";
    int result;

    if (finder.FindFile(path)) {
        // A loose .bmp overrides the archived texture.
        texture->format = BmpFormatFor(texture->type);
        return texture->LoadBmp(device, path.c_str());
    }

    path = std::string(texture->GetName()) + ".mmp";
    if (auto* header = static_cast<MmpHeader*>(GetFile(path, nullptr))) {
        texture->format = header->HeldFormat();
        if (!texture->format)
            return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                    texture->GetName());
        unsigned mip = mipLevel;
        if (mip >= header->mipCount) mip = header->mipCount - 1;
        result = texture->CreateFromMmp(device, header, mip);
        CloseFile(header);
        return result;
    }

    // Not in any archive: a composed texture (the original's file in the data directory).
    const ComposedTexture* composed = FindComposed(texture->GetName());
    if (!composed) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't find texture \"%s\"\n", texture->GetName());
        return 1;
    }
    texture->format = composed->header.HeldFormat();
    if (!texture->format)
        return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                texture->GetName());
    texture->CreateComposed(device, *composed, mipLevel);
    return 1;
}

void CTextureManager::SetDevice(TextureDevice* newDevice) {
    TextureDevice* old = device;
    device = newDevice;
    if (newDevice) newDevice->AddRef();
    if (old) old->Release();
    if (!device) {
        limitTo512 = 1;
        mipLevel = 2;
        return;
    }
    switch (options::g_options.textureQuality) {
    case 0: mipLevel = 0; break;
    case 1: mipLevel = 1; break;
    case 2: mipLevel = 2; break;
    default: mipLevel = 3; break;
    }
    limitTo512 = 0;    // (the game set 1 for a card whose textures were at most 256 pixels wide or high)
}

EngineObject* CTextureManager::NewObject(int textureType) {
    auto* texture = new CTexture();
    texture->type = textureType;
    return texture;
}

// --- The texture's own virtuals (destructor, Create, Destroy), the manager's small entry points (find or
// create by name, reload after a lost device, the cache's size limit) and the .mmp header's pixel format
// presets.

void MmpHeader::SetNoFormat() {
    alphaMask = alphaKind = alphaShift = 0;
    redMask = redBits = redShift = 0;
    greenMask = greenBits = greenShift = 0;
    blueMask = blueBits = blueShift = 0;
    bitsPerPixel = 0;
    format = 0;
}

void MmpHeader::SetFormat5551() {
    alphaMask = 0x8000, alphaKind = 1, alphaShift = 15;
    redMask = 0x7C00, redBits = 5, redShift = 10;
    greenMask = 0x03E0, greenBits = 5, greenShift = 5;
    blueMask = 0x001F, blueBits = 5, blueShift = 0;
    bitsPerPixel = 16;
    format = 0x5551;
}

void MmpHeader::SetFormat4444() {
    alphaMask = 0xF000, alphaKind = 4, alphaShift = 12;
    redMask = 0x0F00, redBits = 4, redShift = 8;
    greenMask = 0x00F0, greenBits = 4, greenShift = 4;
    blueMask = 0x000F, blueBits = 4, blueShift = 0;
    bitsPerPixel = 16;
    format = 0x4444;
}

namespace {
// The counted surface pointer set to null: the holder loses a reference (deleted at 0).
void DropSurface(CTexture* texture) {
    texture->surface.Reset();
}

} // namespace

const char* CTexture::GetName() { return name.c_str(); }
const char* CTexture::ClassName() { return "CTexture"; }
ClassId CTexture::GetClassId() { return ClassId::Texture; }

void CTexture::Create() {
    name.clear();
    DropSurface(this);
    height = 0;
    width = 0;
    uploadResult = 0;
    format = 0;
    extra.clear();
}

void CTexture::Destroy() {
    DropSurface(this);
    flags = 0;
}

CTexture::CTexture() {
    name.clear();
    DropSurface(this);
    height = 0;
    width = 0;
    uploadResult = 0;
    format = 0;
    extra.clear();
}

// The name goes after the body (its destructor), then EngineObject's.
CTexture::~CTexture() {
    DropSurface(this);
    flags = 0;
}

MmpHeader* CTextureManager::GetFileData(std::string_view name) { return static_cast<MmpHeader*>(GetFile(name, nullptr)); }

CTexture* CTextureManager::NewTextureOfType(int textureType) { return CreateObjectAs<CTexture>(textureType, -1); }

CTexture* CTextureManager::FindOrCreate(int textureType, std::string_view name) {
    CTexture* texture = FindTypedTexture(textureType, name);
    if (!texture) texture = NewTextureOfType(textureType);
    texture->name = name;
    return texture;
}

CTexture* CTextureManager::FindOrLoad(int textureType, std::string_view name) {
    if (CTexture* found = FindTexture(name)) return found;
    CTexture* texture = FindTypedTexture(textureType, name);
    if (!texture) texture = NewTextureOfType(textureType);
    texture->name = name;
    LoadTexture(texture);
    return texture;
}

void CTextureManager::TrimCache() {
    const auto overLimit = [this] { return cachedBytes > kCacheLimit; };
    if (!overLimit()) return;
    for (auto node = cache.begin(); node != cache.end();) {
        auto next = std::next(node);
        CTexture* texture = *node;
        cache.erase(node);
        cachedBytes -= texture->uploadResult;
        if (texture) delete texture;
        if (!overLimit()) return;
        node = next;
    }
}

// --- The manager's lifetime, Destroy and CreateObject ----------------------------------------------------

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
    TextureDevice* device = manager->device;
    manager->device = nullptr;
    if (device) device->Release();
}

void CTextureManager::Create() {
    BaseManager::Create();
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
        for (const std::string& path : modfolders::TextureArchives()) ResourceManager::AddArchive(path);
    }
}

void CTextureManager::Destroy() {
    ReleaseTextures(this);
    ResourceManager::Destroy();
}

CTextureManager::~CTextureManager() {
    ReleaseTextures(this);
    ResourceManager::Destroy();
    SetDevice(nullptr);
    // The members' destructors: both counted pointers are empty by now; the cache's.
    cache.clear();
}

EngineObject* CTextureManager::CreateObject(int newType, int newId) {
    EngineObject* object = BaseManager::CreateObject(newType, newId);
    object->flags |= CreateFlag(newType);
    return object;
}

const char* CTextureManager::ClassName() { return kClassName; }
ClassId CTextureManager::GetClassId() { return ClassId::TextureManager; }
const char* CTextureManager::GetFolderName() { return kFolder; }
