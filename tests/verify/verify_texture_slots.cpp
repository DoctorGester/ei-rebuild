// Checks for texture_slots.cpp (A-H25).
// The .mmp header's format presets only write the header: the original runs on the caller's header,
// ours on a copy, and the two are compared (reached when a unit's skin is composed: ComposeTexture,
// every scenario with a new game). Everything else frees, releases, registers or loads, so it runs
// ours only with the state that must hold afterwards: CTexture's Create / Destroy / destructor for
// every texture made and dropped (zone loads and changes), the manager's find-or-create entries from
// the original callers (interface images, skins), TrimCache from the world client every 32 ticks;
// ReloadAll only after a lost device (no scenario).
#include "verify.h"

#include "../../src/game/texture.h"
#include "../../src/lib/crt.h"

#include <cstring>

namespace {
void* s_unused;

std::uintptr_t VtableOf(const void* object) {
    std::uintptr_t v;
    std::memcpy(&v, object, 4);
    return v;
}

// --- MmpHeader -------------------------------------------------------------------------------------

template <void (MmpHeader::*Ours)(), int Tag>
struct FormatCheck {
    static inline void(__fastcall* original)(MmpHeader*);
    static void __fastcall Verify(MmpHeader* self) {
        MmpHeader ours = *self;
        original(self);
        (ours.*Ours)();
        ei::verify::Check("MmpHeader format preset", true, std::memcmp(self, &ours, sizeof(MmpHeader)) == 0,
                          "preset %d: format %X / %X, %u / %u bits", Tag, self->format, ours.format, self->bitsPerPixel,
                          ours.bitsPerPixel);
    }
};
using NoFormatCheck = FormatCheck<&MmpHeader::SetNoFormat, 0>;
using Format5551Check = FormatCheck<&MmpHeader::SetFormat5551, 1>;
using Format4444Check = FormatCheck<&MmpHeader::SetFormat4444, 2>;
using Format8888Check = FormatCheck<&MmpHeader::SetFormat8888, 3>;

// --- CTexture --------------------------------------------------------------------------------------

bool Dropped(const CTexture* t) { return t->block_ == nullptr && t->surface_ == nullptr && t->unknown_64 == 0; }

void __fastcall FreeBlockVerify(CTexture* self) {
    self->FreeBlock();
    ei::verify::Check("CTexture::FreeBlock", true, self->block_ == nullptr, "texture %p", static_cast<void*>(self));
}

int __fastcall CreateVerify(CTexture* self) {
    int result = self->CTexture::Create();
    bool format = self->format_.words[0] == sizeof(PixelFormat);
    for (int i = 1; i < 8; ++i) format = format && self->format_.words[i] == 0;
    const bool ok = result == 1 && Dropped(self) && self->name_[0] == 0 && self->width_ == 0 && self->height_ == 0 &&
                    self->uploadResult_ == 0 && format && self->extra_.data == nullptr && self->extra_.size == 0 &&
                    self->extra_.capacity == 0;
    ei::verify::Check("CTexture::Create", true, ok, "texture %p -> %d", static_cast<void*>(self), result);
    return result;
}

void __fastcall DestroyVerify(CTexture* self) {
    self->CTexture::Destroy();
    ei::verify::Check("CTexture::Destroy", true, Dropped(self) && self->flags_ == 0, "texture %p",
                      static_cast<void*>(self));
}

bool Destructed(const CTexture* t) {
    return VtableOf(t) == CEngineObject::kVtableAddress && Dropped(t) && t->flags_ == 0 && t->owner_ == nullptr;
}

void __fastcall DestructVerify(CTexture* self) {
    self->DestructBody();
    ei::verify::Check("CTexture::DestructBody", true, Destructed(self), "texture %p", static_cast<void*>(self));
}

CTexture* __fastcall DeleteVerify(CTexture* self, void*, unsigned flags) {
    CTexture* result = self->DeletingDestruct(flags & ~1u);
    ei::verify::Check("CTexture::DeletingDestruct", true, result == self && Destructed(self), "texture %p",
                      static_cast<void*>(self));
    if (flags & 1) gamecrt::operator_delete(self);
    return result;
}

// --- CTextureManager -------------------------------------------------------------------------------

bool Registered(CTextureManager* manager, const CEngineObject* object) {
    return manager->FindObject(static_cast<unsigned>(object->id_)) == object;
}

void* __fastcall GetFileDataVerify(CTextureManager* self, void*, const char* name) { return self->GetFileData(name); }

CTexture* __fastcall NewOfTypeVerify(CTextureManager* self, void*, int type) {
    CTexture* texture = self->NewTextureOfType(type);
    ei::verify::Check("CTextureManager::NewTextureOfType", true,
                      texture && texture->type_ == type && Registered(self, texture), "type 0x%X -> %p", type,
                      static_cast<void*>(texture));
    return texture;
}

CTexture* __fastcall FindOrCreateVerify(CTextureManager* self, void*, int type, const char* name) {
    const int references = self->refCount_;
    CTexture* texture = self->FindOrCreate(type, name);
    const bool ok = texture && texture->type_ == type && std::strcmp(texture->name_, name) == 0 &&
                    Registered(self, texture) && self->refCount_ == references;
    ei::verify::Check("CTextureManager::FindOrCreate", true, ok, "type 0x%X \"%s\" -> %p", type, name,
                      static_cast<void*>(texture));
    return texture;
}

CTexture* __fastcall FindOrLoadVerify(CTextureManager* self, void*, int type, const char* name) {
    const int references = self->refCount_;
    CTexture* texture = self->FindOrLoad(type, name);
    const bool ok = texture && std::strcmp(texture->name_, name) == 0 && Registered(self, texture) &&
                    self->refCount_ == references;
    ei::verify::Check("CTextureManager::FindOrLoad", true, ok, "type 0x%X \"%s\" -> %p", type, name,
                      static_cast<void*>(texture));
    return texture;
}

bool __fastcall ReloadAllVerify(CTextureManager* self) {
    const int references = self->refCount_;
    const int cached = self->cache_.count;
    bool result = self->ReloadAll();
    ei::verify::Check("CTextureManager::ReloadAll", true, self->refCount_ == references && self->cache_.count == cached,
                      "manager %p -> %d", static_cast<void*>(self), result);
    return result;
}

void __fastcall TrimCacheVerify(CTextureManager* self) {
    const int cached = self->cache_.count;
    const int bytes = self->cachedBytes_;
    const int limit = static_cast<int>(self->videoMemoryTenth_);
    self->TrimCache();
    const bool ok = bytes <= limit ? self->cache_.count == cached && self->cachedBytes_ == bytes
                                   : self->cache_.count < cached || cached == 0;
    const bool done = self->cachedBytes_ <= limit || self->cache_.count == 0;
    ei::verify::Check("CTextureManager::TrimCache", true, ok && done, "%d textures, %d bytes over %d -> %d, %d", cached,
                      bytes, limit, self->cache_.count, self->cachedBytes_);
}

} // namespace

// Plain names for the wrappers, so tools/check_stolen.py reads the registrations.
static constexpr auto& NoFormatVerify = NoFormatCheck::Verify;
static constexpr auto& Format5551Verify = Format5551Check::Verify;
static constexpr auto& Format4444Verify = Format4444Check::Verify;
static constexpr auto& Format8888Verify = Format8888Check::Verify;
EI_VERIFY(0x00693820, NoFormatVerify, 5, NoFormatCheck::original);
EI_VERIFY(0x00693850, Format5551Verify, 5, Format5551Check::original);
EI_VERIFY(0x006938B0, Format4444Verify, 5, Format4444Check::original);
EI_VERIFY(0x00693910, Format8888Verify, 5, Format8888Check::original);
EI_VERIFY(0x00690FA0, FreeBlockVerify, 0, s_unused);
EI_VERIFY(0x00691A00, CreateVerify, 0, s_unused);
EI_VERIFY(0x00691A70, DestroyVerify, 0, s_unused);
EI_VERIFY(0x00691950, DestructVerify, 0, s_unused);
EI_VERIFY(0x00691930, DeleteVerify, 0, s_unused);
EI_VERIFY(0x00691CF0, GetFileDataVerify, 0, s_unused);
EI_VERIFY(0x00691D40, NewOfTypeVerify, 0, s_unused);
EI_VERIFY(0x00691D00, FindOrCreateVerify, 0, s_unused);
EI_VERIFY(0x00693F10, FindOrLoadVerify, 0, s_unused);
EI_VERIFY(0x00693F60, ReloadAllVerify, 0, s_unused);
EI_VERIFY(0x00694020, TrimCacheVerify, 0, s_unused);
