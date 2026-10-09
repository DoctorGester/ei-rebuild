// Texture loading creates Direct3D surfaces, so it cannot run twice. These wrappers only count calls
// and check that a texture reported as loaded has its pixel format filled in.
#include "verify.h"

#include "../../src/game/texture.h"

#include <cstring>

namespace {
void* s_unused;

bool FormatSet(const CTexture* t) {
    for (std::uint32_t w : t->format_.words)
        if (w) return true;
    return false;
}
} // namespace

static int __fastcall LoadTextureVerify(CTextureManager* self, void*, CTexture* texture) {
    bool wasLoaded = texture && (texture->flags_ & kTextureLoaded);
    int result = self->LoadTexture(texture);
    bool ok = !texture || wasLoaded || result == 0 || FormatSet(texture);
    ei::verify::Check("CTextureManager::LoadTexture", true, ok, "\"%s\" result %d", texture ? texture->GetName() : "(null)", result);
    return result;
}
EI_VERIFY(0x006922A0, LoadTextureVerify, 0, s_unused);

static int __fastcall LoadTextureFromVerify(CTextureManager* self, void*, CTexture* texture, const char* dir) {
    int result = self->LoadTextureFrom(texture, dir);
    ei::verify::Check("CTextureManager::LoadTextureFrom", true, true, "");
    return result;
}
EI_VERIFY(0x006926F0, LoadTextureFromVerify, 0, s_unused);

// 00691810 (manager slot 14, every texture the game creates): ours only. A fresh texture of the
// requested type: CTexture's vtable, the base fields cleared, an empty name, no surface or block,
// an empty extra block and a pixel format with only its size (0x20).
static CEngineObject* __fastcall NewObjectVerify(CTextureManager* self, void*, int type) {
    CEngineObject* object = self->CTextureManager::NewObject(type);
    auto* t = static_cast<CTexture*>(object);
    bool ok = !t || (*reinterpret_cast<std::uintptr_t*>(t) == CTexture::kVtableAddress && t->refCount_ == 0 &&
                     t->id_ == -1 && t->flags_ == 0 && t->owner_ == nullptr && t->type_ == type && t->name_[0] == 0 &&
                     !t->surface_ && !t->block_ && !t->extra_.data && t->extra_.size == 0 && t->extra_.capacity == 0 &&
                     t->format_.words[0] == 0x20 && t->format_.words[3] == 0 && t->width_ == 0 && t->height_ == 0);
    ei::verify::Check("CTextureManager::NewObject", true, ok, "type %X", type);
    return object;
}
EI_VERIFY(0x00691810, NewObjectVerify, 0, s_unused);

// 00694300: MOV EAX,[ESP+8]; PUSH EBX (5). The original on a copy with its own block, ours on the
// real buffer; sizes, capacity, step and the kept bytes compared (the extra blocks: every .mmp
// texture, every new texture).
static void(__fastcall* s_bufferSetSize)(AlignedBuffer*, void*, int, int);
static void __fastcall BufferSetSizeVerify(AlignedBuffer* self, void*, int count, int growBy) {
    AlignedBuffer scratch = *self;
    if (self->data) {
        scratch.data = AlignedBuffer::Allocate(self->capacity);
        std::memcpy(scratch.data, self->data, static_cast<std::size_t>(self->size));
    }
    int oldSize = self->size;
    s_bufferSetSize(&scratch, nullptr, count, growBy);
    self->SetSize(count, growBy);
    int kept = oldSize < self->size ? oldSize : self->size;
    bool same = scratch.size == self->size && scratch.capacity == self->capacity && scratch.growBy == self->growBy &&
                !scratch.data == !self->data &&
                (!self->data || kept <= 0 || std::memcmp(scratch.data, self->data, static_cast<std::size_t>(kept)) == 0);
    ei::verify::Check("lib::AlignedArray<uint8>::SetSize", true, same, "%d -> %d: size %d/%d cap %d/%d", oldSize, count,
                      scratch.size, self->size, scratch.capacity, self->capacity);
    if (scratch.data) AlignedBuffer::Free(scratch.data);
}
EI_VERIFY(0x00694300, BufferSetSizeVerify, 5, s_bufferSetSize);

// 006913A0 (the client world's setup, every scenario): side by side. The original runs first with
// the old device protected by an extra reference; then the manager's fields are put back and the
// references it moved undone, and ours runs. Device, mip level, size limit and the video memory
// share compared. (A failed GetCaps exits through the DirectX report.)
// MOV EAX,[ESP+4] (4); SUB ESP,0x104 (6).
static void(__fastcall* s_setDevice)(CTextureManager*, void*, CTextureDevice*);
static void __fastcall SetDeviceVerify(CTextureManager* self, void*, CTextureDevice* device) {
    CTextureDevice* old = self->device_;
    const std::uint16_t mip = self->mipLevel_;
    const int limit = self->limitTo512_;
    const std::uint32_t memory = self->videoMemoryTenth_;
    if (old) old->AddRef();
    s_setDevice(self, nullptr, device);
    CTextureDevice* expectedDevice = self->device_;
    const std::uint16_t expectedMip = self->mipLevel_;
    const int expectedLimit = self->limitTo512_;
    const std::uint32_t expectedMemory = self->videoMemoryTenth_;
    if (device) --device->refs_;   // the original's reference on the new device
    self->device_ = old;           // keeps the reference the original dropped (ours drops it again)
    self->mipLevel_ = mip;
    self->limitTo512_ = limit;
    self->videoMemoryTenth_ = memory;
    self->SetDevice(device);
    bool same = self->device_ == expectedDevice && self->mipLevel_ == expectedMip &&
                self->limitTo512_ == expectedLimit && self->videoMemoryTenth_ == expectedMemory;
    ei::verify::Check("CTextureManager::SetDevice", true, same, "mip %u/%u limit %d/%d memory %u/%u",
                      static_cast<unsigned>(expectedMip), static_cast<unsigned>(self->mipLevel_), expectedLimit,
                      self->limitTo512_, expectedMemory, self->videoMemoryTenth_);
}
EI_VERIFY(0x006913A0, SetDeviceVerify, 10, s_setDevice);
