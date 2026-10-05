#pragma once

#include "engine_object.h"
#include "resource_manager.h"

#include <windows.h>

// Pixel format of a texture surface (32 bytes), as filled in by the texture device.
struct PixelFormat {
    std::uint32_t words[8];
};
static_assert(sizeof(PixelFormat) == 32);

struct CDXSurface;

// Owns the Direct3D texture formats; held by CTextureManager. Name provisional; not rewritten yet.
// The same object is the renderer's device object (the global at 007AF080): its DirectDraw
// interface, the primary surface's and the render target's holders and the screen rectangle.
class CTextureDevice {
public:
    int ParseMmpHeader(const void* data, PixelFormat* format);   // 004E7C40
    int FindFormat(int id, PixelFormat* format);                 // 004E7DA0

    std::uint8_t unknown_00[8];
    int lastLoadResult_;        // +0x08 result of the last IDirect3DDevice7::Load (and other calls)
    void* ddraw_;               // +0x0C IDirectDraw7*
    std::uint8_t unknown_10[4];
    CDXSurface* primary_;       // +0x14 the primary surface's holder
    CDXSurface* renderTarget_;  // +0x18 the render target's holder
    std::uint8_t unknown_1C[4];
    std::uint8_t flags_;        // +0x20 bit 0: full screen (the present flips)
    std::uint8_t unknown_21[3];
    RECT screen_;               // +0x24 the screen rectangle
    std::uint8_t unknown_34[0x4C - 0x34];
    void* converter_;           // +0x4C 2DintMMX's graphics processor (pixel conversions)
    std::uint8_t unknown_50[4];
    void* d3dDevice_;           // +0x54 IDirect3DDevice7*
};
static_assert(offsetof(CTextureDevice, ddraw_) == 0x0C);
static_assert(offsetof(CTextureDevice, converter_) == 0x4C);
static_assert(offsetof(CTextureDevice, primary_) == 0x14);
static_assert(offsetof(CTextureDevice, renderTarget_) == 0x18);
static_assert(offsetof(CTextureDevice, flags_) == 0x20);
static_assert(offsetof(CTextureDevice, screen_) == 0x24);
static_assert(offsetof(CTextureDevice, d3dDevice_) == 0x54);

// A DirectDraw surface wrapper (004EFA60 constructs it from a DDSURFACEDESC2, 004EF7B0 for an
// off-screen surface of the screen's format; 0x20 bytes): vtable (slot 0 the deleting destructor),
// the reference count, the last DirectDraw result, the device object, the IDirectDrawSurface7*,
// width, height and bits per pixel.
struct CDXSurface {
    void* vtable;
    int refCount;               // +0x04
    long result;                // +0x08
    CTextureDevice* device;     // +0x0C
    void* surface;              // +0x10
    int width;                  // +0x14
    int height;                 // +0x18
    int bitsPerPixel;           // +0x1C

    // 004E6BA0: drops a reference; the deleting destructor (vtable slot 0, with 1) at zero.
    void Release() {
        if (--refCount != 0) return;
        using Destroy = void(__fastcall*)(CDXSurface*, void*, int);
        (*static_cast<Destroy*>(vtable))(this, nullptr, 1);
    }
};
static_assert(offsetof(CDXSurface, result) == 0x08);
static_assert(offsetof(CDXSurface, device) == 0x0C);
static_assert(offsetof(CDXSurface, surface) == 0x10);
static_assert(offsetof(CDXSurface, bitsPerPixel) == 0x1C);
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
// Each channel: its mask, the shift of its lowest bit and its bit count.
struct MmpHeader {
    std::uint32_t magic;        // +0x00 "MMP"
    std::uint32_t width;        // +0x04
    std::uint32_t height;       // +0x08
    std::uint32_t mipCount;     // +0x0C
    std::uint32_t format;       // +0x10 pixel format id (0x8888, 0x4444, 0x5551, 0x5650, ...)
    std::uint32_t bitsPerPixel; // +0x14
    std::uint32_t alphaMask;    // +0x18
    std::uint32_t alphaShift;   // +0x1C
    std::uint32_t alphaKind;    // +0x20 the alpha bit count: 0, 1, 4 or 8 (texture flags 0x4000..0x20000)
    std::uint32_t redMask;      // +0x24
    std::uint32_t redShift;     // +0x28
    std::uint32_t redBits;      // +0x2C
    std::uint32_t greenMask;    // +0x30
    std::uint32_t greenShift;   // +0x34
    std::uint32_t greenBits;    // +0x38
    std::uint32_t blueMask;     // +0x3C
    std::uint32_t blueShift;    // +0x40
    std::uint32_t blueBits;     // +0x44
    std::uint32_t extraSize;    // +0x48

    // 006908E0. Offset of mip level `mip`'s pixels after the header: the extra block plus the
    // sizes of the larger levels (a geometric sum computed in 32-bit integers).
    std::uint32_t MipOffset(unsigned mip) const;
};
static_assert(offsetof(MmpHeader, alphaKind) == 0x20);
static_assert(offsetof(MmpHeader, blueBits) == 0x44);
static_assert(sizeof(MmpHeader) == 0x4C);

// 004EDAA0 (thiscall, RET 4) fills it: a DDPIXELFORMAT's channels as mask, bit count and shift
// (0x34 bytes, zeroed first; it stops early for a format without DDPF_RGB, logging "Format have no
// RGB information", or with a channel wider than 8 bits). Returns 1 when the four were split.
struct ChannelLayout {
    std::uint32_t unknown_00;                         // zeroed, never written
    std::uint32_t alphaMask, alphaBits, alphaShift;   // +0x04
    std::uint32_t redMask, redBits, redShift;         // +0x10
    std::uint32_t greenMask, greenBits, greenShift;   // +0x1C
    std::uint32_t blueMask, blueBits, blueShift;      // +0x28
    int Split(const void* format);
};
static_assert(sizeof(ChannelLayout) == 0x34);

// vtable 00743D24, class id 6. 0x68 bytes (00691810 makes it).
class CTexture : public CEngineObject {
public:
    // 00690430. Creates the surface from .mmp data, starting at mip level `mip`: a system-memory
    // surface filled from the file (0068FA10) and loaded into a managed texture surface; copies
    // the file's extra block. Returns 1.
    int CreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);
    int CallCreateFromMmp(CTextureDevice* device, const void* data, unsigned mip);   // through 00690430
    // 00690260. Creates the surface from a .bmp file on disk.
    int LoadBmp(CTextureDevice* device, const char* path);
    // 0068F6C0. A surface with no pixels yet, as `header` describes it (texture_blank.cpp): square,
    // header->width wide (the height is not read), header->mipCount levels, the texture's format_,
    // a managed texture (system memory with flag 0x400). Returns 1.
    int CreateBlank(CTextureDevice* device, const MmpHeader* header);
    // CreateBlank's body; with `memoryOnly` the holder gets no DirectDraw surface in the switch-over,
    // only the new renderer's memory (NewMemoryTextureHolder, dx_surface.h).
    int MakeBlank(CTextureDevice* device, const MmpHeader* header, bool memoryOnly);

    char* name_;                // +0x1C CString
    std::uint32_t width_;       // +0x20
    std::uint32_t height_;      // +0x24
    int uploadResult_;          // +0x28 result of 0068FA10; 0 after CreateBlank
    AlignedBuffer extra_;       // +0x2C the .mmp file's extra block
    PixelFormat format_;        // +0x3C
    CDXSurface* surface_;       // +0x5C the texture surface (counted)
    std::uint8_t* block_;       // +0x60 an aligned block (raw pointer before the data), freed when a
                                // surface is made (0068F6C0, 0068F860) and by 00690FA0
    std::uint32_t unknown_64;   // +0x64 cleared by the constructor, slot 2 and slot 3
};
static_assert(offsetof(CTexture, extra_) == 0x2C);
static_assert(offsetof(CTexture, format_) == 0x3C);
static_assert(offsetof(CTexture, surface_) == 0x5C);
static_assert(offsetof(CTexture, block_) == 0x60);
static_assert(sizeof(CTexture) == 0x68);

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
    // 00691D80. A new texture (CreateObject(kind, -1)) the game fills on the CPU (texture_blank.cpp):
    // `width` x `height` in the format of `kind` (0x1E 5:6:5, 0x20 1:5:5:5, 0x21 4:4:4:4; another
    // kind keeps a zero format), its surface square at `width` (CreateBlank). In the switch-over
    // with "cpuwindows" the surface is only the new renderer's memory. Null when CreateBlank fails;
    // FatalError's result when the device has no such format.
    CTexture* NewTexture(int width, int height, int kind);

    std::uint8_t unknown_BC[0x28];
    CTextureDevice* device_;    // +0xE4
    std::uint16_t mipLevel_;    // +0xE8 first mip level to load (texture quality setting)
    std::uint16_t unknown_EA;
    int limitTo512_;            // +0xEC nonzero: textures are loaded at most 511 pixels wide and high
};
static_assert(offsetof(CTextureManager, device_) == 0xE4);
static_assert(offsetof(CTextureManager, limitTo512_) == 0xEC);
