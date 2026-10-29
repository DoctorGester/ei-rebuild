#include "texture.h"

#include "app.h"

#include "lib/file_io.h"
#include "lib/crt.h"
#include <cstring>
#include <new>
#include <string>
#include "game_singletons.h"
#include "options.h"
#include "lib/str.h"



namespace {
// Game-wide paths set at startup. Names provisional.
const std::string& GameRoot() { return options::g_options->workPath_; }

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
    if (texture->flags_ & TextureFlag::Loaded) {
        game::DebugLog("Load already loaded texture \"%s\".\n", texture->GetName());
        return 1;
    }

    lib::FileFind finder;
    std::string path = GameRoot() + "textures\\" + texture->GetName() + ".bmp";
    int result;

    if (finder.FindFile(path)) {
        // A loose .bmp overrides the archived texture.
        texture->format_ = BmpFormatFor(texture->type_);
        return texture->LoadBmp(device_, path.c_str());
    }

    path = std::string(texture->GetName()) + ".mmp";
    if (auto* header = static_cast<MmpHeader*>(GetFile(path, nullptr))) {
        texture->format_ = header->HeldFormat();
        if (!texture->format_)
            return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                    texture->GetName());
        unsigned mip = mipLevel_;
        if (mip >= header->mipCount) mip = header->mipCount - 1;
        result = texture->CreateFromMmp(device_, header, mip);
        CloseFile(header);
        return result;
    }

    // Not in any archive: a composed texture (the original's file in the data directory).
    const ComposedTexture* composed = FindComposed(texture->GetName());
    if (!composed) {
        game::DebugLog("CTextureManager::LoadTexture.\n Can't find texture \"%s\"\n", texture->GetName());
        return 1;
    }
    texture->format_ = composed->header.HeldFormat();
    if (!texture->format_)
        return game::FatalError("CTextureManager::LoadTexture.\n Can't find texture format for texture \"%s\".\n",
                                texture->GetName());
    texture->CreateComposed(device_, *composed, mipLevel_);
    return 1;
}

void CTextureManager::SetDevice(CTextureDevice* device) {
    CTextureDevice* old = device_;
    device_ = device;
    if (device) device->AddRef();
    if (old) old->Release();
    if (!device_) {
        limitTo512_ = 1;
        mipLevel_ = 2;
        return;
    }
    switch (options::g_options->textureQuality_) {
    case 0: mipLevel_ = 0; break;
    case 1: mipLevel_ = 1; break;
    case 2: mipLevel_ = 2; break;
    default: mipLevel_ = 3; break;
    }
    limitTo512_ = 0;   // (the game set 1 for a card whose textures were at most 256 pixels wide or high)
}

CEngineObject* CTextureManager::NewObject(int type) {
    auto* texture = new CTexture();
    texture->type_ = type;
    return texture;
}
