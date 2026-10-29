// CTexture's own virtuals (destructor, Create, Destroy), the texture manager's small entry
// points (find or create by name, reload after a lost device, the cache's size limit) and the .mmp
// header's pixel format presets. See texture.h and docs/notes/items_spells.md.
#include "texture.h"

#include "lib/crt.h"

#include <cstring>

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
