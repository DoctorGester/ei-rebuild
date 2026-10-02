#pragma once

#include "engine_object.h"
#include "resource_manager.h"

// Pixel format of a texture surface (32 bytes), as filled in by the texture device.
struct PixelFormat {
    std::uint32_t words[8];
};
static_assert(sizeof(PixelFormat) == 32);

// Start of an .mmp texture file (EI's mipmapped texture format). Only the fields used so far.
struct MmpHeader {
    std::uint32_t unknown_00, unknown_04, unknown_08;
    std::uint32_t mipCount;     // +0x0C
    std::uint32_t format;       // +0x10 pixel format id (0x8888, 0x4444, 0x5551, 0x5650, ...)
};

// Owns the Direct3D texture formats; held by CTextureManager. Name provisional; not rewritten yet.
class CTextureDevice {
public:
    int ParseMmpHeader(const void* data, PixelFormat* format);   // 004E7C40
    int FindFormat(int id, PixelFormat* format);                 // 004E7DA0
};

// vtable 00743D24, class id 6.
class CTexture : public CEngineObject {
public:
    // 00690430. Creates the surface from .mmp data, starting at mip level `mip`.
    int CreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);
    // 00690260. Creates the surface from a .bmp file on disk.
    int LoadBmp(CTextureDevice* device, const char* path);

    std::uint8_t unknown_1C[0x20];
    PixelFormat format_;        // +0x3C
};

constexpr std::uint32_t kTextureLoaded = 0x08;    // CEngineObject::flags_

// vtable 00743CB0, class id 0x26. Slots 16-22 are CResourceManager's.
class CTextureManager : public CResourceManager {
public:
    // 006922A0. Loads a texture: textures\<name>.bmp on disk if present, else <name>.mmp from the
    // archives, else <data path>textures\<name>.mmp on disk.
    int LoadTexture(CTexture* texture);
    // 006926F0. Loads <directory><name>.mmp from disk.
    int LoadTextureFrom(CTexture* texture, const char* directory);

    std::uint8_t unknown_BC[0x28];
    CTextureDevice* device_;    // +0xE4
    std::uint16_t mipLevel_;    // +0xE8 first mip level to load (texture quality setting)
};
static_assert(offsetof(CTextureManager, device_) == 0xE4);
