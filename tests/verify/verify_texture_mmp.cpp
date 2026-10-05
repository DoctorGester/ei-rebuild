// Checks for the .mmp texture code (texture_mmp.cpp). Surface creation and file reading cannot run
// twice: those run ours only and check the result; the mip offset is pure.
#include "verify.h"

#include "../../src/game/texture.h"
#include "../../src/render/render.h"

#include <windows.h>
#include <ddraw.h>

#include <cstring>
#include <vector>

// 006908E0: MOV EAX,[ESP+4]; PUSH ESI (5 bytes).
static std::uint32_t(__fastcall* s_mipOffset)(const MmpHeader*, void*, unsigned);
static std::uint32_t __fastcall MipOffsetVerify(const MmpHeader* self, void*, unsigned mip) {
    std::uint32_t expected = s_mipOffset(self, nullptr, mip);
    ei::verify::Check("MmpHeader::MipOffset", expected, self->MipOffset(mip), "%ux%u bpp %u mip %u", self->width,
                      self->height, self->bitsPerPixel, mip);
    return expected;
}
EI_VERIFY(0x006908E0, MipOffsetVerify, 5, s_mipOffset);

namespace {

int MipmapsEnabled() { return *reinterpret_cast<const int*>(0x0079BCE0); }

// 004EDC00 (thiscall on a ChannelLayout, RET 4): the conversion ChannelLayout::ToNative copies.
__declspec(naked) std::uint32_t __fastcall ToNativeOriginal(const ChannelLayout*, void*, std::uint32_t)
    EI_ORIGINAL(0x004EDC00)

// ChannelLayout::ToNative (texture_bmp.cpp; for the .bmp textures, which no scenario has) against
// 004EDC00, once for every RGB texture format a loaded texture has: 64 colours each.
void CheckToNative(const PixelFormat& format) {
    static std::vector<PixelFormat> checked;
    if (!(format.words[1] & DDPF_RGB)) return;
    for (const PixelFormat& f : checked)
        if (std::memcmp(&f, &format, sizeof(f)) == 0) return;
    checked.push_back(format);
    ChannelLayout layout;
    if (!layout.Split(&format)) return;
    std::uint32_t colour = 0x12345678;
    for (int i = 0; i < 64; ++i) {
        std::uint32_t expected = ToNativeOriginal(&layout, nullptr, colour);
        ei::verify::Check("ChannelLayout::ToNative", expected, layout.ToNative(colour),
                          "%08X (%u bits, masks %08X %08X %08X %08X)", colour, format.words[3], format.words[4],
                          format.words[5], format.words[6], format.words[7]);
        colour = colour * 1664525u + 1013904223u;
    }
}

} // namespace

// 00690430 creates Direct3D surfaces: ours only. The texture ends loaded with a surface, its size
// is the file's at some level at or below `mip`, and the extra block is copied. With a DirectDraw
// texture its byte size (+0x28) is the original's fill's result (0068FA10), compared with
// MmpHeader::FilledBytes, which gives it in the switch-over; there ("present" with "cpuwindows")
// the holder has no DirectDraw texture and no memory (the new renderer decoded the file) unless
// the new renderer could not decode it. Every scenario loads textures: frames_basic, render_scene
// (DirectDraw), render_present, render_present_cpuwindows, ui_saveload (the switch-over).
static void* s_createFromMmp;
static int __fastcall CreateFromMmpVerify(CTexture* self, void*, CTextureDevice* device, const void* data,
                                          unsigned mip) {
    bool wasLoaded = (self->flags_ & kTextureLoaded) != 0;
    int result = self->CreateFromMmp(device, data, mip);
    const auto* header = static_cast<const MmpHeader*>(data);
    bool ok = result == 1;
    unsigned level = mip;
    if (ok && !wasLoaded) {
        while (level < 32 && (header->width >> level) != self->width_) ++level;
        ok = (self->flags_ & kTextureLoaded) && self->surface_ && level < 32 &&
             self->height_ == header->height >> level &&
             self->extra_.size == static_cast<int>(header->extraSize) &&
             (header->extraSize == 0 || std::memcmp(self->extra_.data, header + 1, header->extraSize) == 0);
    }
    ei::verify::Check("CTexture::CreateFromMmp", true, ok, "%ux%u mip %u -> %ux%u", header->width, header->height,
                      mip, self->width_, self->height_);
    if (ok && !wasLoaded) {
        const CDXSurface* holder = self->surface_;
        unsigned levels = MipmapsEnabled() ? header->mipCount - level : 1;
        std::uint32_t bytes = header->FilledBytes(self->format_, level, levels);
        if (holder->surface) {
            ei::verify::Check("MmpHeader::FilledBytes", static_cast<std::uint32_t>(self->uploadResult_), bytes,
                              "format %08X %u bits -> %08X / %u bits, %ux%u mip %u levels %u", header->format,
                              header->bitsPerPixel, self->format_.words[2], self->format_.words[3], header->width,
                              header->height, level, levels);
        } else {
            bool decoded = render::Presenting() && render::CpuWindows() && !render::SurfaceMemoryOf(holder) &&
                           holder->width == static_cast<int>(self->width_) &&
                           holder->height == static_cast<int>(self->height_) &&
                           self->uploadResult_ == static_cast<int>(bytes);
            ei::verify::Check("CTexture::CreateFromMmp decoded", true, decoded, "%ux%u: holder %dx%d, %d bytes",
                              self->width_, self->height_, holder->width, holder->height, self->uploadResult_);
        }
        CheckToNative(self->format_);
    }
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
