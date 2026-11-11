#include "game/scene/device.h"
#include "game/models/texture.h"

#include "game/app/app.h"
#include "game/core/engine_globals.h"
#include "game/mods/mod_folders.h"
#include "game/app/options.h"
#include "game/world/world.h"
#include "lib/bytes.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <new>
#include <string>

namespace {
// Game-wide paths set at startup. Names provisional.
const std::string& GameRoot() { return options::g_options.workPath; }

constexpr std::uint32_t kDxtCode = 'D' | ('X' << 8) | ('T' << 16);   // a DXT format's code without its digit

// Texture format of a .bmp texture, by texture kind.
MmpFormat BmpFormatFor(int type) {
    switch (type) {
    case 1: case 2: case 3: case 4: case 8: return MmpFormat::A1R5G5B5;   // 1-bit alpha
    case 6: case 13: return MmpFormat::A4R4G4B4;                          // 4-bit alpha
    default: return MmpFormat::R5G6B5;                                   // no alpha
    }
}

} // namespace

// The list the old device offered. Rebirth's battle screen texture (battle00, 1536x1536) and footprints are DXT5:
// without it no format is found and the battle screen misses its texture.
bool IsTextureFormat(MmpFormat id) {
    if (id == MmpFormat::R5G6B5 || id == MmpFormat::A1R5G5B5 || id == MmpFormat::A4R4G4B4 || id == MmpFormat::A8R8G8B8) return true;
    const auto code = static_cast<std::uint32_t>(id);
    const std::uint32_t digit = code >> 24;
    return (code & 0x00FFFFFF) == kDxtCode && digit >= '1' && digit <= '5';
}

render::PixelFormat TextureFormat(MmpFormat id) {
    switch (id) {
    case MmpFormat::R5G6B5: return render::PixelFormat{16, 0xF800, 0x07E0, 0x001F, 0};
    case MmpFormat::A1R5G5B5: return render::PixelFormat{16, 0x7C00, 0x03E0, 0x001F, 0x8000};
    case MmpFormat::A4R4G4B4: return render::PixelFormat{16, 0x0F00, 0x00F0, 0x000F, 0xF000};
    case MmpFormat::A8R8G8B8: return render::PixelFormat{32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000u};
    default: return render::PixelFormat{};
    }
}

MmpFormat MmpHeader::HeldFormat() const {
    if (IsTextureFormat(format)) return format;
    if ((static_cast<std::uint32_t>(format) & 0x00FFFFFF) != kDxtCode) return MmpFormat::None;
    // A DXT code outside the list: the RGB format its channel bit counts name (the loader decodes into it).
    if (redBits == 5 && greenBits == 6 && blueBits == 5 && alphaKind == 0) return MmpFormat::R5G6B5;
    if (redBits == 5 && greenBits == 5 && blueBits == 5 && alphaKind == 1) return MmpFormat::A1R5G5B5;
    if (redBits == 4 && greenBits == 4 && blueBits == 4 && alphaKind == 4) return MmpFormat::A4R4G4B4;
    if (redBits == 8 && greenBits == 8 && blueBits == 8 && alphaKind == 8) return MmpFormat::A8R8G8B8;
    return MmpFormat::None;
}

namespace {

// A texture whose file holds a format the renderer cannot take: a warning, and the texture stays unloaded as a
// missing one does (1).
int NoTextureFormat(Texture* texture) {
    game::Warning("CTextureManager::LoadTexture: no texture format for texture \"%s\": not loaded", texture->GetName());
    return 1;
}

} // namespace

int TextureManager::LoadTexture(Texture* texture) {
    if (!texture) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't load texture to NULL pointer.\n");
        return 0;
    }
    if (texture->flags & TextureFlag::Loaded) {
        game::DebugLog("Load already loaded texture \"%s\".\n", texture->GetName());
        return 1;
    }

    std::string path = GameRoot() + "textures\\" + texture->name + ".bmp";
    int result;

    if (platform::PathExists(path)) {
        // A loose .bmp overrides the archived texture.
        texture->format = BmpFormatFor(texture->type);
        return texture->LoadBmp(device, path);
    }

    path = texture->name + ".mmp";
    if (const MmpFile file(GetFile(path, nullptr)); file.data) {
        texture->format = file.header.HeldFormat();
        if (texture->format == MmpFormat::None) {
            CloseFile(file.data);
            return NoTextureFormat(texture);
        }
        unsigned mip = mipLevel;
        if (mip >= file.header.mipCount) mip = file.header.mipCount - 1;
        result = texture->CreateFromMmp(device, file, mip);
        CloseFile(file.data);
        return result;
    }

    // Not in any archive: a composed texture (the original's file in the data directory).
    const ComposedTexture* composed = FindComposed(texture->GetName());
    if (!composed) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't find texture \"%s\"\n", texture->GetName());
        return 1;
    }
    texture->format = composed->header.HeldFormat();
    if (texture->format == MmpFormat::None) return NoTextureFormat(texture);
    texture->CreateComposed(device, *composed, mipLevel);
    return 1;
}

void TextureManager::SetDevice(TextureDevice* newDevice) {
    TextureDevice* old = device;
    device = newDevice;
    if (newDevice) newDevice->AddRef();
    if (old) old->Release();
    if (!device) {
        limitTo512 = 1;
        mipLevel = 2;
        return;
    }
    mipLevel = 0;    // full size
    limitTo512 = 0;    // (the game set 1 for a card whose textures were at most 256 pixels wide or high)
}

EngineObject* TextureManager::NewObject(int textureType) {
    auto* texture = new Texture();
    texture->type = textureType;
    return texture;
}

// --- The texture's own virtuals (destructor, Create, Destroy), the manager's small entry points (find or
// create by name, reload after a lost device, the cache's size limit) and the .mmp header's pixel format
// layouts.

void MmpHeader::SetFormat(MmpFormat id) {
    struct Channel {
        std::uint32_t mask = 0, shift = 0, bits = 0;
    };
    struct Layout {
        MmpFormat id = {};
        Channel alpha, red, green, blue;
    };
    static constexpr Layout kLayouts[] = {
        {MmpFormat::R5G6B5, {0, 0, 0}, {0xF800, 11, 5}, {0x07E0, 5, 6}, {0x001F, 0, 5}},
        {MmpFormat::A1R5G5B5, {0x8000, 15, 1}, {0x7C00, 10, 5}, {0x03E0, 5, 5}, {0x001F, 0, 5}},
        {MmpFormat::A4R4G4B4, {0xF000, 12, 4}, {0x0F00, 8, 4}, {0x00F0, 4, 4}, {0x000F, 0, 4}},
    };
    alphaMask = alphaKind = alphaShift = 0;
    redMask = redBits = redShift = 0;
    greenMask = greenBits = greenShift = 0;
    blueMask = blueBits = blueShift = 0;
    bitsPerPixel = 0;
    format = MmpFormat::None;
    for (const Layout& layout : kLayouts) {
        if (layout.id != id) continue;
        alphaMask = layout.alpha.mask, alphaShift = layout.alpha.shift, alphaKind = layout.alpha.bits;
        redMask = layout.red.mask, redShift = layout.red.shift, redBits = layout.red.bits;
        greenMask = layout.green.mask, greenShift = layout.green.shift, greenBits = layout.green.bits;
        blueMask = layout.blue.mask, blueShift = layout.blue.shift, blueBits = layout.blue.bits;
        bitsPerPixel = 16;
        format = id;
    }
}

namespace {
// The counted surface pointer set to null: the holder loses a reference (deleted at 0).
void DropSurface(Texture* texture) {
    texture->surface.Reset();
}

} // namespace

std::string_view Texture::GetName() { return name; }
const char* Texture::ClassName() { return "CTexture"; }
ClassId Texture::GetClassId() { return ClassId::Texture; }

void Texture::Create() {
    name.clear();
    DropSurface(this);
    height = 0;
    width = 0;
    uploadResult = 0;
    format = MmpFormat::None;
    extra.clear();
}

void Texture::Destroy() {
    DropSurface(this);
    flags = 0;
}

Texture::Texture() = default;

// The name goes after the body (its destructor), then EngineObject's.
Texture::~Texture() {
    DropSurface(this);
    flags = 0;
}

// The client world's (it has the one texture manager).
TextureManager* Texture::Manager() const { return ClientWorld()->textureManager.get(); }

MmpFile::MmpFile(void* file) : data(file) {
    if (file) header = lib::Load<MmpHeader>(file);
}

MmpFile TextureManager::GetFileData(std::string_view name) { return MmpFile(GetFile(name, nullptr)); }

Texture* TextureManager::NewTextureOfType(int textureType) { return CreateObjectAs<Texture>(textureType, -1); }

Texture* TextureManager::FindOrCreate(int textureType, std::string_view name) {
    Texture* texture = FindTypedTexture(textureType, name);
    if (!texture) texture = NewTextureOfType(textureType);
    texture->name = name;
    return texture;
}

Texture* TextureManager::FindOrLoad(int textureType, std::string_view name) {
    if (Texture* found = FindTexture(name)) return found;
    Texture* texture = FindTypedTexture(textureType, name);
    if (!texture) texture = NewTextureOfType(textureType);
    texture->name = name;
    LoadTexture(texture);
    return texture;
}

void TextureManager::TrimCache() {
    while (cachedBytes > kCacheLimit && !cache.empty()) {
        Texture* texture = cache.front();
        cache.erase(cache.begin());
        cachedBytes -= texture->uploadResult;
        delete texture;
    }
}

// --- The manager's lifetime, Destroy and CreateObject ----------------------------------------------------

namespace {

constexpr const char* kClassName = "CTextureManager";

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

// The part Destroy and the destructor share: the cache, the composed textures and the device.
static void ReleaseTextures(TextureManager* manager) {
    for (auto node = manager->cache.begin(); node != manager->cache.end();) {
        Texture* texture = *node;
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

void TextureManager::Create() {
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
    if (modfolders::Active()) {
        for (const std::string& path : modfolders::TextureArchives()) ResourceManager::AddArchive(path);
    }
}

void TextureManager::Destroy() {
    ReleaseTextures(this);
    ResourceManager::Destroy();
}

TextureManager::~TextureManager() {
    ReleaseTextures(this);
    ResourceManager::Destroy();
    SetDevice(nullptr);
    // The members' destructors: both counted pointers are empty by now; the cache's.
    cache.clear();
}

EngineObject* TextureManager::CreateObject(int newType, int newId) {
    EngineObject* object = BaseManager::CreateObject(newType, newId);
    object->flags |= CreateFlag(newType);
    return object;
}

const char* TextureManager::ClassName() { return kClassName; }
ClassId TextureManager::GetClassId() { return ClassId::TextureManager; }

// --- The cache of released textures ------------------------------------------------------------------

// The texture manager's cache of released textures: a texture nobody references any more is kept
// (ReleaseObject) and taken back when the game asks for it again by type and
// name or by name (FindTexture: the material stages,
// the terrain decals). A cached texture's DirectDraw
// priority was lowered to 0 and raised to 100 when it is taken back; no holder has a DirectDraw surface,
// so that is left out. See docs/notes/renderer.md (section 7).

// Returns 0 (the result is the priority call's, or a list node, on the cache path;
// EngineObject::Release, its caller, returns it but no caller reads it).
int TextureManager::ReleaseObject(EngineObject* object) {
    if (!object || object->refCount != 0) return 0;
    auto* texture = static_cast<Texture*>(object);   // the manager's objects are all textures
    if (auto node = objects.find(static_cast<unsigned>(object->id)); node != objects.end()) {
        Unregister(node->second->id);
        if (object->flags & TextureFlag::Cacheable) {
            cache.push_back(texture);
            cachedBytes += texture->uploadResult;
            return 0;
        }
    }
    object->Destroy();
    object->SetOwner(nullptr);
    return RemoveObject(object);
}

Texture* TextureManager::TakeFromCache(int textureType, std::string_view name) {
    auto node = std::ranges::find_if(cache, [&](const Texture* texture) { return texture->type == textureType && texture->name == name; });
    if (node == cache.end()) return nullptr;
    Texture* texture = *node;
    cache.erase(node);
    cachedBytes -= texture->uploadResult;
    RegisterInstant(texture);
    return texture;
}

// Details kept: a texture taken back from the cache here is not subtracted from
// cachedBytes (the by-type lookup does subtract it).
Texture* TextureManager::FindTexture(std::string_view name) {
    ObjectListIteratorOf<Texture> registered;
    Texture* found = nullptr;
    for (Texture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (texture->name == name) {
            found = texture;
            break;
        }
    }
    if (!found) {
        auto node = std::ranges::find_if(cache, [&](const Texture* texture) { return texture->name == name; });
        if (node != cache.end()) {
            found = *node;
            cache.erase(node);
            RegisterInstant(found);
        }
    }
    return found;
}

Texture* TextureManager::FindTypedTexture(int textureType, std::string_view name) {
    ObjectListIteratorOf<Texture> registered;
    Texture* found = nullptr;
    for (Texture* texture = registered.First(this); texture; texture = registered.Next()) {
        if (texture->type == textureType && texture->name == name) {
            found = texture;
            break;
        }
    }
    if (!found) found = TakeFromCache(textureType, name);
    return found;
}

bool TextureManager::IsLoaded(int textureType, std::string_view name) {
    const auto loaded = [&](const Texture* texture) {
        return texture->type == textureType && texture->name == name && (texture->flags & TextureFlag::Loaded);
    };
    ObjectListIteratorOf<Texture> registered;
    for (Texture* texture = registered.First(this); texture; texture = registered.Next())
        if (loaded(texture)) return true;
    return std::ranges::any_of(cache, loaded);
}

void TextureManager::FreeCache() {
    for (auto node = cache.begin(); node != cache.end();) {
        Texture* texture = *node;
        ++node;
        if (texture) delete texture;
    }
    cachedBytes = 0;
    cache.clear();
}
