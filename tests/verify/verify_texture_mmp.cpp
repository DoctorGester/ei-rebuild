// Checks for the .mmp texture code (texture_mmp.cpp). Surface creation and file reading cannot run
// twice: those run ours only and check the result; the mip offset is pure.
#include "verify.h"

#include "../../src/game/texture.h"

#include <cstring>

// 006908E0: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static std::uint32_t(__fastcall* s_mipOffset)(const MmpHeader*, void*, unsigned);
static std::uint32_t __fastcall MipOffsetVerify(const MmpHeader* self, void*, unsigned mip) {
    std::uint32_t expected = s_mipOffset(self, nullptr, mip);
    ei::verify::Check("MmpHeader::MipOffset", expected, self->MipOffset(mip), "%ux%u bpp %u mip %u", self->width,
                      self->height, self->bitsPerPixel, mip);
    return expected;
}
EI_VERIFY(0x006908E0, MipOffsetVerify, 5, s_mipOffset);

// 00690430 creates Direct3D surfaces: ours only. The texture ends loaded with a surface, its size
// is the file's at some level at or below `mip`, and the extra block is copied.
static void* s_createFromMmp;
static int __fastcall CreateFromMmpVerify(CTexture* self, void*, CTextureDevice* device, const void* data,
                                          unsigned mip) {
    bool wasLoaded = (self->flags_ & kTextureLoaded) != 0;
    int result = self->CreateFromMmp(device, data, mip);
    const auto* header = static_cast<const MmpHeader*>(data);
    bool ok = result == 1;
    if (ok && !wasLoaded) {
        unsigned level = mip;
        while (level < 32 && (header->width >> level) != self->width_) ++level;
        ok = (self->flags_ & kTextureLoaded) && self->surface_ && level < 32 &&
             self->height_ == header->height >> level &&
             self->extra_.size == static_cast<int>(header->extraSize) &&
             (header->extraSize == 0 || std::memcmp(self->extra_.data, header + 1, header->extraSize) == 0);
    }
    ei::verify::Check("CTexture::CreateFromMmp", true, ok, "%ux%u mip %u -> %ux%u", header->width, header->height,
                      mip, self->width_, self->height_);
    return result;
}
EI_VERIFY(0x00690430, CreateFromMmpVerify, 0, s_createFromMmp);

// 006928C0 reads files: ours only. Without a file the buffer ends empty.
static void* s_loadMmpExtra;
static int __fastcall LoadMmpExtraVerify(CTextureManager* self, void*, const char* name, AlignedBuffer* out) {
    int result = self->LoadMmpExtra(name, out);
    bool ok = (result == 1 || (result == 0 && out->size == 0)) && out->data && out->size <= out->capacity;
    ei::verify::Check("CTextureManager::LoadMmpExtra", true, ok, "\"%s\" result %d size %d", name, result, out->size);
    return result;
}
EI_VERIFY(0x006928C0, LoadMmpExtraVerify, 0, s_loadMmpExtra);
