// CDib (MFC class "CDib"): a device independent bitmap as a .bmp file holds one. The game reads loose
// textures\<name>.bmp overrides with it and writes the screenshots and movie frames a surface capture fills. See
// docs/notes/models.md ("CDib").
#pragma once

#include "lib/gameapi.h"

#include <cstddef>
#include <cstdint>

namespace lib {
class File;
}

class CDib {
public:
    enum Owner : int { kNotOwned = 0, kNew = 1, kGlobal = 2 };

    CDib();
    // Create's bitmap (the fields are left to Create).
    CDib(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);

    virtual ~CDib();

    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    // A new header for `width` x `height` pixels of `bitCount` bits, without a colour table and without bits
    // (AllocateBits). 16 and 32 bits: BI_BITFIELDS with the three channel masks after the header.
    void Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);
    void Empty();
    // The header's width and height, 0 x 0 without a header. Returns `out`.
    SIZE* Size(SIZE* out) const;
    int MakePalette();
    // Memory for the pixels, width * height * planes * (bits / 8) bytes: rows without
    // padding, while `imageSize` (what Write writes) counts rows padded to four bytes. Returns
    // `bits` (unchanged without a header).
    std::uint8_t* AllocateBits();
    // The bitmap emptied, then the file header, the info header with what follows it up to the bits (masks or
    // colours), the bits; the colour count and the palette. A short or non-"BM" file, or a lib::FileError
    // from the reads: a message box and 0. Returns 1.
    int Read(lib::File* file);
    // The file header, the info header with the masks, `imageSize`
    // bytes of bits. A lib::FileError from the writes: a message box and 0. Returns 1.
    int Write(lib::File* file);

    RGBQUAD* colours;                // after the header
    HBITMAP bitmap;
    std::uint8_t* bits;
    BITMAPINFOHEADER* header;
    HGLOBAL headerMemory;            // the header's block when headerOwner is kGlobal
    int headerOwner;
    int bitsOwner;
    std::uint32_t imageSize;         // bytes of bits
    int colourCount;
    int maskBytes;                   // bytes of channel masks after the header (0 or 12)
    HANDLE file;                     // a mapped file, its mapping and its view
    HANDLE mapping;
    void* view;
    HPALETTE palette;
};
static_assert(offsetof(CDib, bits) == 0x0C);
static_assert(offsetof(CDib, header) == 0x10);
static_assert(offsetof(CDib, imageSize) == 0x20);
static_assert(offsetof(CDib, palette) == 0x38);
static_assert(sizeof(CDib) == 0x3C);

// The bytes of a header's pixels: its biSizeImage, or (when that is 0) its rows padded to four bytes.
std::uint32_t DibImageSize(const BITMAPINFOHEADER* header);

struct CDXSurface;

// A new bitmap of the surface's pixels inside `rect`, in the surface's depth (16 and 32 bits with its channel
// masks), rows bottom-up. The surface is locked for reading meanwhile.
CDib* CaptureSurfaceRect(CDXSurface* surface, const RECT* rect);
CDib* CaptureSurface(CDXSurface* surface);
