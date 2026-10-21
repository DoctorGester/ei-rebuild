// CTexture's own slots (0 destructor, 2 Create, 3 Destroy), the texture manager's small entry
// points (find or create by name, reload after a lost device, the cache's size limit) and the .mmp
// header's pixel format presets (A-H25). See texture.h and docs/notes/items_spells.md.
#include "texture.h"

#include "ei/hook.h"
#include "ei/vcall.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <cstring>

#undef GetClassName   // CEngineObject::GetClassName (engine_object.h)

// --- MmpHeader -------------------------------------------------------------------------------------

void MmpHeader::SetNoFormat() {
    alphaMask = alphaKind = alphaShift = 0;
    redMask = redBits = redShift = 0;
    greenMask = greenBits = greenShift = 0;
    blueMask = blueBits = blueShift = 0;
    bitsPerPixel = 0;
    format = 0;
}
EI_REWRITTEN(0x00693820, MmpHeader::SetNoFormat);

void MmpHeader::SetFormat5551() {
    alphaMask = 0x8000, alphaKind = 1, alphaShift = 15;
    redMask = 0x7C00, redBits = 5, redShift = 10;
    greenMask = 0x03E0, greenBits = 5, greenShift = 5;
    blueMask = 0x001F, blueBits = 5, blueShift = 0;
    bitsPerPixel = 16;
    format = 0x5551;
}
EI_REWRITTEN(0x00693850, MmpHeader::SetFormat5551);

void MmpHeader::SetFormat4444() {
    alphaMask = 0xF000, alphaKind = 4, alphaShift = 12;
    redMask = 0x0F00, redBits = 4, redShift = 8;
    greenMask = 0x00F0, greenBits = 4, greenShift = 4;
    blueMask = 0x000F, blueBits = 4, blueShift = 0;
    bitsPerPixel = 16;
    format = 0x4444;
}
EI_REWRITTEN(0x006938B0, MmpHeader::SetFormat4444);

void MmpHeader::SetFormat8888() {
    alphaMask = 0xFF000000, alphaKind = 8, alphaShift = 24;
    redMask = 0x00FF0000, redBits = 8, redShift = 16;
    greenMask = 0x0000FF00, greenBits = 8, greenShift = 8;
    blueMask = 0x000000FF, blueBits = 8, blueShift = 0;
    bitsPerPixel = 32;
    format = 0x8888;
}
EI_REWRITTEN(0x00693910, MmpHeader::SetFormat8888);

// --- CTexture --------------------------------------------------------------------------------------

namespace {
// The counted surface pointer set to null: the holder loses a reference (deleted at 0).
void DropSurface(CTexture* texture) {
    CDXSurface* held = texture->surface_;
    texture->surface_ = nullptr;
    if (held) held->Release();
}
} // namespace

const char* CTexture::GetName() { return name_; }
const char* CTexture::GetClassName() { return "CTexture"; }
int CTexture::GetClassId() { return 6; }

void CTexture::FreeBlock() {
    if (block_) lib::FreeAligned32(block_);
    block_ = nullptr;
}
EI_REWRITTEN(0x00690FA0, CTexture::FreeBlock);

int CTexture::Create() {
    Name()->Empty();
    DropSurface(this);
    block_ = nullptr;
    height_ = 0;
    width_ = 0;
    uploadResult_ = 0;
    std::memset(&format_, 0, sizeof(format_));
    format_.words[0] = sizeof(PixelFormat);   // DDPIXELFORMAT dwSize
    spare64_ = 0;
    extra_.RemoveAll();
    return 1;
}

void CTexture::Destroy() {
    FreeBlock();
    DropSurface(this);
    spare64_ = 0;
    flags_ = 0;
}

CTexture::CTexture() {
    ClearFields();
    std::memset(&extra_, 0, sizeof(extra_));
    surface_ = nullptr;
    name_.Empty();
    DropSurface(this);
    block_ = nullptr;
    height_ = 0;
    width_ = 0;
    uploadResult_ = 0;
    std::memset(&format_, 0, sizeof(format_));
    format_.words[0] = sizeof(PixelFormat);   // DDPIXELFORMAT dwSize
    spare64_ = 0;
    extra_.SetSize(0, -1);
}

// The name goes after the body (its destructor), then CEngineObject's.
CTexture::~CTexture() {
    FreeBlock();
    DropSurface(this);
    spare64_ = 0;
    flags_ = 0;
    // The counted surface pointer is null by now; the extra block (its pointer is not cleared).
    if (extra_.data) lib::FreeAligned32(extra_.data);
}

void CTexture::DestructBody() { this->CTexture::~CTexture(); }
EI_REWRITTEN(0x00691950, CTexture::DestructBody);

CTexture* CTexture::DeletingDestruct(unsigned deleteFlags) {
    this->CTexture::~CTexture();
    if (deleteFlags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REWRITTEN(0x00691930, CTexture::DeletingDestruct);

namespace {
// The virtual slots' entry points (rewrite-guide rule 4).
int __fastcall TextureCreateHook(CTexture* self) { return self->CTexture::Create(); }
void __fastcall TextureDestroyHook(CTexture* self) { self->CTexture::Destroy(); }
} // namespace
EI_REWRITTEN(0x00691A00, TextureCreateHook);
EI_REWRITTEN(0x00691A70, TextureDestroyHook);

// --- CTextureManager -------------------------------------------------------------------------------

void* CTextureManager::GetFileData(const char* name) { return GetFile(name, nullptr); }
EI_REWRITTEN(0x00691CF0, CTextureManager::GetFileData);

CTexture* CTextureManager::NewTextureOfType(int type) { return static_cast<CTexture*>(CreateObject(type, -1)); }
EI_REWRITTEN(0x00691D40, CTextureManager::NewTextureOfType);

CTexture* CTextureManager::FindOrCreate(int type, const char* name) {
    CTexture* texture = FindTypedTexture(type, name);
    if (!texture) texture = NewTextureOfType(type);
    *texture->Name() = name;
    return texture;
}
EI_REWRITTEN(0x00691D00, CTextureManager::FindOrCreate);

CTexture* CTextureManager::FindOrLoad(int type, const char* name) {
    if (CTexture* found = FindTexture(name)) return found;
    CTexture* texture = FindTypedTexture(type, name);
    if (!texture) texture = NewTextureOfType(type);
    *texture->Name() = name;
    LoadTexture(texture);
    return texture;
}
EI_REWRITTEN(0x00693F10, CTextureManager::FindOrLoad);

bool CTextureManager::ReloadAll() {
    bool ok = true;
    ObjectListIterator registered{nullptr, nullptr};
    for (CEngineObject* object = registered.First(this); object; object = registered.Next()) {
        object->flags_ &= ~kTextureLoaded;
        bool loaded = LoadTexture(static_cast<CTexture*>(object)) != 0;
        ok = loaded && ok;
    }
    for (auto* node = cache_.head; node;) {
        auto* next = node->next;
        CTexture* texture = node->data;
        texture->flags_ &= ~kTextureLoaded;
        bool loaded = LoadTexture(texture) != 0;
        ok = loaded && ok;
        node = next;
    }
    if (registered.manager) --registered.manager->refCount_;
    return ok;
}
EI_REWRITTEN(0x00693F60, CTextureManager::ReloadAll);

void CTextureManager::TrimCache() {
    const auto overLimit = [this] { return cachedBytes_ > static_cast<int>(videoMemoryTenth_); };
    if (!overLimit()) return;
    for (auto* node = cache_.head; node;) {
        auto* next = node->next;
        CTexture* texture = node->data;
        cache_.RemoveAt(node);
        cachedBytes_ -= texture->uploadResult_;
        if (texture) delete texture;
        if (!overLimit()) return;
        node = next;
    }
}
EI_REWRITTEN(0x00694020, CTextureManager::TrimCache);
