// Filling a texture's system-memory surface from .mmp data (0068FA10 and its three fills 00690970,
// 00690B30, 00690D30; CreateFromMmp, texture_mmp.cpp, calls it) and loading a .bmp texture with its
// alpha file (00690260).
#include "texture.h"

#include "app.h"
#include "console.h"
#include "text_split.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc.h"

#include <ddraw.h>

#include <cstdint>
#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }
const char* CopyCantLockText() { return Text(0x00795C48); }      // "CTexture::CopyToSurface\n Can't lock surface\n"
const char* CopyCantUnlockText() { return Text(0x00795C18); }    // "... Can't unlock surface\n"
const char* UnpackCantLockText() { return Text(0x00795CB0); }    // "CTexture::UnpackToSurface\n Can't lock render surface\n"
const char* UnpackCantUnlockText() { return Text(0x00795C78); }  // "... Can't unlock render surface\n"
const char* NoDecodeFormatText() { return Text(0x00795AF0); }    // "Can't find decode format.\n"
const char* AlreadyLoadedText() { return Text(0x00795BE8); }     // "CTexture::Load.\n Texture \"%s\" already loaded.\n"
const char* AlphaPrefixText() { return Text(0x00795BE4); }       // "a_"

constexpr DWORD kLockWriteWait = DDLOCK_WAIT | DDLOCK_WRITEONLY | DDLOCK_NOSYSLOCK;   // 0x821
constexpr DWORD kMipLevelCaps = DDSCAPS_TEXTURE | DDSCAPS_MIPMAP;                     // 0x401000

constexpr std::uint32_t kDxtTag = 0x545844;   // "DXT" (the format's low three bytes)
constexpr std::uint32_t kDxt1 = 0x31545844, kDxt2 = 0x32545844, kDxt3 = 0x33545844;
constexpr std::uint32_t kFormat8888 = 0x8888;
constexpr std::uint32_t kPixelFormatFourCC = 0x4;   // DDPIXELFORMAT flags (PixelFormat word 1)

// The 16-bit formats a DXT file is decoded into, by the surface's channel bits (the device's
// texture kinds): 5:6:5, 5:5:5, 1:5:5:5, 4:4:4:4, 8:8:8:8.
constexpr int kKind565 = 0x1E, kKind555 = 0x1F, kKind1555 = 0x20, kKind4444 = 0x21, kKind8888 = 0x22;

// 2DintMMX's graphics processor (CTextureDevice::converter_), slot 10: decodes a DXT block image
// (thiscall: target, source, mode, width, height).
constexpr int kConverterDecode = 10;
void ConverterDecode(void* converter, void* target, const void* source, int mode, std::uint32_t width,
                     std::uint32_t height) {
    using Fn = void(__fastcall*)(void*, void*, void*, const void*, int, std::uint32_t, std::uint32_t);
    reinterpret_cast<Fn>((*static_cast<void***>(converter))[kConverterDecode])(converter, nullptr, target, source,
                                                                               mode, width, height);
}

// The level loop the three fills share: from `mip` while the file has levels, lock the surface,
// `write(target, source, width, height)` (returning the bytes it counts), unlock it, step the
// source by the file's level size and move to the attached level, whose reference is never
// released (the original's leak). Bytes counted, or the DirectX report's result.
template <typename Write>
int FillLevels(CDXSurface* holder, const MmpHeader* header, int mip, const char* cantLock, const char* cantUnlock,
               Write write) {
    const auto* source = reinterpret_cast<const std::uint8_t*>(header + 1) + header->MipOffset(static_cast<unsigned>(mip));
    auto* surface = static_cast<IDirectDrawSurface7*>(holder->surface);
    std::uint32_t width = header->width >> (mip & 31);
    std::uint32_t height = header->height >> (mip & 31);
    int total = 0;
    for (int level = mip; level < static_cast<int>(header->mipCount); ++level) {
        DDSURFACEDESC2 desc;
        std::memset(&desc, 0, sizeof(desc));
        desc.dwSize = sizeof(desc);
        HRESULT result = surface->Lock(nullptr, &desc, kLockWriteWait, nullptr);
        if (result != DD_OK) return app::FatalErrorCode(result, cantLock);
        total += write(desc.lpSurface, source, width, height);
        source += (header->bitsPerPixel * height * width) >> 3;
        result = surface->Unlock(nullptr);
        if (result != DD_OK) return app::FatalErrorCode(result, cantUnlock);
        DDSCAPS2 caps;
        std::memset(&caps, 0, sizeof(caps));
        caps.dwCaps = kMipLevelCaps;
        if (surface->GetAttachedSurface(&caps, &surface) != DD_OK) return total;
        width >>= 1;
        height >>= 1;
    }
    return total;
}

} // namespace

// The fills and the .bmp loader through their addresses, so their verify wrappers see the calls.
__declspec(naked) int CTexture::CallFillSurface(CDXSurface*, const void*, int) EI_ORIGINAL(0x0068FA10)
__declspec(naked) int CTexture::CallLoadBmp(CTextureDevice*, const char*) EI_ORIGINAL(0x00690260)
namespace {
__declspec(naked) int __fastcall CopyToSurfaceAt(CTexture*, void*, CDXSurface*, const MmpHeader*, int)
    EI_ORIGINAL(0x00690970)
__declspec(naked) int __fastcall ConvertToSurfaceAt(CTexture*, void*, CDXSurface*, const MmpHeader*, int)
    EI_ORIGINAL(0x00690B30)
__declspec(naked) int __fastcall UnpackToSurfaceAt(CTexture*, void*, CDXSurface*, const MmpHeader*, int, int)
    EI_ORIGINAL(0x00690D30)
} // namespace

int CTexture::FillSurface(CDXSurface* surface, const void* data, int mip) {
    const auto* header = static_cast<const MmpHeader*>(data);
    std::uint32_t format = header->format;
    if ((format & 0xFFFFFF) != kDxtTag) {
        if (format == kFormat8888 && format_.words[3] != 32) return ConvertToSurfaceAt(this, nullptr, surface, header, mip);
        return CopyToSurfaceAt(this, nullptr, surface, header, mip);
    }
    if ((format_.words[1] & kPixelFormatFourCC) && format == format_.words[2])
        return CopyToSurfaceAt(this, nullptr, surface, header, mip);

    ChannelLayout layout;
    layout.Split(&format_);
    int kind = 0;
    if (layout.redBits == 5) {
        if (layout.greenBits == 6) {
            if (layout.blueBits == 5 && layout.alphaBits == 0) kind = kKind565;
        } else if (layout.greenBits == 5 && layout.blueBits == 5) {
            if (layout.alphaBits == 0)
                kind = kKind555;
            else if (layout.alphaBits == 1)
                kind = kKind1555;
        }
    } else if (layout.redBits == 4) {
        if (layout.greenBits == 4 && layout.blueBits == 4 && layout.alphaBits == 4) kind = kKind4444;
    } else if (layout.redBits == 8 && layout.greenBits == 8 && layout.blueBits == 8 && layout.alphaBits == 8) {
        kind = kKind8888;
    }
    if (kind == 0) return game::FatalError(NoDecodeFormatText());

    // The converter's mode for the file and the surface; any other pair passes the level (kept).
    int mode = mip;
    if (format == kDxt1) {
        if (kind == kKind565)
            mode = 0;
        else if (kind == kKind555)
            mode = 1;
        else if (kind == kKind1555)
            mode = 2;
    } else if (format == kDxt2) {
        if (kind == kKind4444) mode = 3;
    } else if (format == kDxt3) {
        if (kind == kKind4444) mode = 4;
    }
    return UnpackToSurfaceAt(this, nullptr, surface, header, mip, mode);
}
EI_REPLACE_METHOD(0x0068FA10, CTexture::FillSurface);

int CTexture::CopyToSurface(CDXSurface* surface, const MmpHeader* header, int mip) {
    return FillLevels(surface, header, mip, CopyCantLockText(), CopyCantUnlockText(),
                      [header](void* target, const std::uint8_t* source, std::uint32_t width, std::uint32_t height) {
                          std::uint32_t bytes = (header->bitsPerPixel * height * width) >> 3;
                          std::memcpy(target, source, bytes);
                          return static_cast<int>(bytes);
                      });
}
EI_REPLACE_METHOD(0x00690970, CTexture::CopyToSurface);

// Each 8:8:8:8 pixel becomes a 16-bit pixel of the texture's format (ChannelLayout::ToNative, the
// conversion of 004EDC00); the bytes counted are the texture's bits per pixel.
int CTexture::ConvertToSurface(CDXSurface* surface, const MmpHeader* header, int mip) {
    ChannelLayout layout;
    layout.Split(&format_);
    const std::uint32_t surfaceBits = format_.words[3];
    return FillLevels(surface, header, mip, CopyCantLockText(), CopyCantUnlockText(),
                      [&layout, surfaceBits](void* target, const std::uint8_t* source, std::uint32_t width,
                                             std::uint32_t height) {
                          const auto* pixels = reinterpret_cast<const std::uint32_t*>(source);
                          auto* out = static_cast<std::uint16_t*>(target);
                          for (std::uint32_t i = 0, count = height * width; i < count; ++i)
                              out[i] = static_cast<std::uint16_t>(layout.ToNative(pixels[i]));
                          return static_cast<int>((surfaceBits * height * width) >> 3);
                      });
}
EI_REPLACE_METHOD(0x00690B30, CTexture::ConvertToSurface);

// The device's converter decodes each level into 16-bit pixels.
int CTexture::UnpackToSurface(CDXSurface* surface, const MmpHeader* header, int mip, int kind) {
    void* converter = surface->device->converter_;
    return FillLevels(surface, header, mip, UnpackCantLockText(), UnpackCantUnlockText(),
                      [converter, kind](void* target, const std::uint8_t* source, std::uint32_t width,
                                        std::uint32_t height) {
                          ConverterDecode(converter, target, source, kind, width, height);
                          return static_cast<int>(((height * width) << 4) >> 3);
                      });
}
EI_REPLACE_METHOD(0x00690D30, CTexture::UnpackToSurface);

// The path is split at `\` (each piece trimmed), the alpha file is "a_" + the file name in the same
// directory; both are joined again.
int CTexture::LoadBmp(CTextureDevice* device, const char* path) {
    mfc::CString file;
    mfc::CString alphaPath;
    console::CStringArray pieces;
    if ((flags_ & kTextureLoaded) &&
        gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(name_), reinterpret_cast<const unsigned char*>(path)) == 0) {
        game::DebugLog(AlreadyLoadedText(), path);
        return 1;
    }
    text::Split(path, &pieces, '\\');
    file = pieces[pieces.GetSize() - 1];
    pieces.RemoveAt(pieces.GetSize() - 1, 1);
    alphaPath = mfc::operator+(AlphaPrefixText(), file);
    pieces.SetAtGrow(pieces.GetSize(), alphaPath);
    text::Join(&alphaPath, &pieces, '\\');
    pieces.RemoveAt(pieces.GetSize() - 1, 1);
    pieces.SetAtGrow(pieces.GetSize(), file);
    text::Join(&file, &pieces, '\\');
    return LoadBmpFiles(device, file, alphaPath);
}
EI_REPLACE_METHOD(0x00690260, CTexture::LoadBmp);
