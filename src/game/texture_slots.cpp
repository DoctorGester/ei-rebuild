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

void MmpHeader::SetFormat8888() {
    alphaMask = 0xFF000000, alphaKind = 8, alphaShift = 24;
    redMask = 0x00FF0000, redBits = 8, redShift = 16;
    greenMask = 0x0000FF00, greenBits = 8, greenShift = 8;
    blueMask = 0x000000FF, blueBits = 8, blueShift = 0;
    bitsPerPixel = 32;
    format = 0x8888;
}

namespace {
// The counted surface pointer set to null: the holder loses a reference (deleted at 0).
void DropSurface(CTexture* texture) {
    Surface* held = texture->surface_;
    texture->surface_ = nullptr;
    if (held) held->Release();
}

} // namespace

const char* CTexture::GetName() { return name_.c_str(); }
const char* CTexture::ClassName() { return "CTexture"; }
ClassId CTexture::GetClassId() { return kClassTexture; }

int CTexture::Create() {
    name_.clear();
    DropSurface(this);
    height_ = 0;
    width_ = 0;
    uploadResult_ = 0;
    format_ = 0;
    extra_.clear();
    return 1;
}

void CTexture::Destroy() {
    DropSurface(this);
    flags_ = 0;
}

CTexture::CTexture() {
    ClearFields();
    surface_ = nullptr;
    name_.clear();
    DropSurface(this);
    height_ = 0;
    width_ = 0;
    uploadResult_ = 0;
    format_ = 0;
    extra_.clear();
}

// The name goes after the body (its destructor), then CEngineObject's.
CTexture::~CTexture() {
    DropSurface(this);
    flags_ = 0;
}

MmpHeader* CTextureManager::GetFileData(std::string_view name) { return static_cast<MmpHeader*>(GetFile(name, nullptr)); }

CTexture* CTextureManager::NewTextureOfType(int type) { return CreateObjectAs<CTexture>(type, -1); }

CTexture* CTextureManager::FindOrCreate(int type, std::string_view name) {
    CTexture* texture = FindTypedTexture(type, name);
    if (!texture) texture = NewTextureOfType(type);
    texture->name_ = name;
    return texture;
}

CTexture* CTextureManager::FindOrLoad(int type, std::string_view name) {
    if (CTexture* found = FindTexture(name)) return found;
    CTexture* texture = FindTypedTexture(type, name);
    if (!texture) texture = NewTextureOfType(type);
    texture->name_ = name;
    LoadTexture(texture);
    return texture;
}

void CTextureManager::TrimCache() {
    const auto overLimit = [this] { return cachedBytes_ > kCacheLimit; };
    if (!overLimit()) return;
    for (auto node = cache_.begin(); node != cache_.end();) {
        auto next = std::next(node);
        CTexture* texture = *node;
        cache_.erase(node);
        cachedBytes_ -= texture->uploadResult_;
        if (texture) delete texture;
        if (!overLimit()) return;
        node = next;
    }
}
