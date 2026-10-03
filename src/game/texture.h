#pragma once

#include "engine_object.h"
#include "resource_manager.h"

// Pixel format of a texture surface (32 bytes), as filled in by the texture device.
struct PixelFormat {
    std::uint32_t words[8];
};
static_assert(sizeof(PixelFormat) == 32);

// Owns the Direct3D texture formats; held by CTextureManager. Name provisional; not rewritten yet.
class CTextureDevice {
public:
    int ParseMmpHeader(const void* data, PixelFormat* format);   // 004E7C40
    int FindFormat(int id, PixelFormat* format);                 // 004E7DA0

    std::uint8_t unknown_00[8];
    int lastLoadResult_;        // +0x08 result of the last IDirect3DDevice7::Load
    std::uint8_t unknown_0C[0x54 - 0x0C];
    void* d3dDevice_;           // +0x54 IDirect3DDevice7*
};
static_assert(offsetof(CTextureDevice, d3dDevice_) == 0x54);

// A DirectDraw surface wrapper (004EFA60 constructs it from a DDSURFACEDESC2; 0x20 bytes):
// vtable (slot 0 the deleting destructor), +0x04 reference count, +0x10 IDirectDrawSurface7*.
struct CDXSurface {
    void* vtable;
    int refCount;
    std::uint8_t unknown_08[8];
    void* surface;              // +0x10
    std::uint8_t unknown_14[0x0C];
};
static_assert(sizeof(CDXSurface) == 0x20);

// A byte buffer aligned to 32 bytes (the raw block pointer is stored just before the data), with
// MFC CByteArray-style size, capacity and growth step.
struct AlignedBuffer {
    std::uint8_t* data;         // +0x00
    int size;                   // +0x04
    int capacity;               // +0x08
    int growBy;                 // +0x0C 0: an eighth of the size, 4..1024
};

// Header of an .mmp file (0x4C bytes); the extra block of +0x48 bytes follows it, then the mips.
struct MmpHeader {
    std::uint32_t magic;        // +0x00 "MMP"
    std::uint32_t width;        // +0x04
    std::uint32_t height;       // +0x08
    std::uint32_t mipCount;     // +0x0C
    std::uint32_t format;       // +0x10 pixel format id (0x8888, 0x4444, 0x5551, 0x5650, ...)
    std::uint32_t bitsPerPixel; // +0x14
    std::uint32_t unknown_18[2];
    std::uint32_t alphaKind;    // +0x20 0, 1, 4 or 8 (texture flags 0x4000..0x20000)
    std::uint32_t unknown_24[9];
    std::uint32_t extraSize;    // +0x48

    // 006908E0. Offset of mip level `mip`'s pixels after the header: the extra block plus the
    // sizes of the larger levels (a geometric sum computed in 32-bit integers).
    std::uint32_t MipOffset(unsigned mip) const;
};
static_assert(sizeof(MmpHeader) == 0x4C);

// vtable 00743D24, class id 6.
class CTexture : public CEngineObject {
public:
    // 00690430. Creates the surface from .mmp data, starting at mip level `mip`: a system-memory
    // surface filled from the file (0068FA10) and loaded into a managed texture surface; copies
    // the file's extra block. Returns 1.
    int CreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);
    int CallCreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);   // through 00690430
    // 00690260. Creates the surface from a .bmp file on disk.
    int LoadBmp(CTextureDevice* device, const char* path);

    char* name_;                // +0x1C CString
    std::uint32_t width_;       // +0x20
    std::uint32_t height_;      // +0x24
    int uploadResult_;          // +0x28 result of 0068FA10
    AlignedBuffer extra_;       // +0x2C the .mmp file's extra block
    PixelFormat format_;        // +0x3C
    CDXSurface* surface_;       // +0x5C the texture surface (counted)
};
static_assert(offsetof(CTexture, extra_) == 0x2C);
static_assert(offsetof(CTexture, format_) == 0x3C);
static_assert(offsetof(CTexture, surface_) == 0x5C);

constexpr std::uint32_t kTextureLoaded = 0x08;    // CEngineObject::flags_

// vtable 00743CB0, class id 0x26. Slots 16-22 are CResourceManager's.
class CTextureManager : public CResourceManager {
public:
    // 006922A0. Loads a texture: textures\<name>.bmp on disk if present, else <name>.mmp from the
    // archives, else <data path>textures\<name>.mmp on disk.
    int LoadTexture(CTexture* texture);
    // 006926F0. Loads <directory><name>.mmp from disk.
    int LoadTextureFrom(CTexture* texture, const char* directory);
    // 006928C0. Reads the extra block of <name>.mmp into `out` (sized to it, old contents not
    // kept): from <data path>textures\ on disk, else from the archives. Returns 0 (and an empty
    // buffer) when neither has the file.
    int LoadMmpExtra(const char* name, AlignedBuffer* out);

    std::uint8_t unknown_BC[0x28];
    CTextureDevice* device_;    // +0xE4
    std::uint16_t mipLevel_;    // +0xE8 first mip level to load (texture quality setting)
    std::uint16_t unknown_EA;
    int limitTo512_;            // +0xEC nonzero: textures are loaded at most 511 pixels wide and high
};
static_assert(offsetof(CTextureManager, device_) == 0xE4);
static_assert(offsetof(CTextureManager, limitTo512_) == 0xEC);
