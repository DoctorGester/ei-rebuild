// CDib (MFC class "CDib", CObject's three slots): a device independent bitmap as a .bmp file holds one. The game reads loose
// CObject's three slots): a device independent bitmap as a .bmp file holds one. The game reads loose
// textures\<name>.bmp overrides with it (texture_bmp.cpp) and writes the screenshots and movie
// frames a surface capture (below) fills. See docs/notes/models.md ("CDib").
#pragma once

#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

class CDib {
public:
    // Who frees `header` / `bits`.
    enum Owner : int { kNotOwned = 0, kNew = 1, kGlobal = 2 };

    // An empty bitmap.
    CDib();
    // Create's bitmap (the fields are left to Create).
    CDib(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);

    // The class's CRuntimeClass.
    virtual const void* GetRuntimeClass() const;
    // 1: empty.
    virtual ~CDib();
    // The archive is flushed, then the bitmap written to or read from its file.
    virtual void Serialize(mfc::CArchive* archive);

    // On the game's heap (dib.cpp): null without memory, and then no constructor runs.
    static void* operator new(std::size_t size) noexcept;
    static void* operator new(std::size_t, void* place) noexcept { return place; }
    static void operator delete(void* block);
    static void operator delete(void*, void*) noexcept {}

    // A new header for `width` x `height` pixels of `bitCount` bits, without a
    // colour table and without bits (AllocateBits). 16 and 32 bits: BI_BITFIELDS with the three
    // channel masks after the header.
    void Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);
    // Frees whatever the bitmap owns and zeroes it.
    void Empty();
    // The header's width and height, 0 x 0 without a header. Returns `out`.
    SIZE* Size(SIZE* out) const;
    // A GDI palette from the colour table; 0 without colours.
    int MakePalette();
    // Memory for the pixels, width * height * planes * (bits / 8) bytes: rows without
    // padding, while `imageSize` (what Write writes) counts rows padded to four bytes. Returns
    // `bits` (unchanged without a header).
    std::uint8_t* AllocateBits();
    // dib_file.cpp: the bitmap emptied, then the file header, the info header
    // with what follows it up to the bits (masks or colours), the bits; the colour count and the
    // palette. A short or non-"BM" file, or an MFC exception from the reads (inside TRY / CATCH): a
    // message box and 0. Returns 1.
    int Read(mfc::CFile* file);
    // dib_file.cpp: the file header, the info header with the masks, `imageSize`
    // bytes of bits. An MFC exception from the writes: a message box and 0. Returns 1.
    int Write(mfc::CFile* file);

    RGBQUAD* colours;                // +0x04: after the header
    HBITMAP bitmap;                  // +0x08
    std::uint8_t* bits;              // +0x0C
    BITMAPINFOHEADER* header;        // +0x10
    HGLOBAL headerMemory;            // +0x14: the header's block when headerOwner is kGlobal
    int headerOwner;                 // +0x18
    int bitsOwner;                   // +0x1C
    std::uint32_t imageSize;         // +0x20: bytes of bits
    int colourCount;                 // +0x24
    int maskBytes;                   // +0x28: bytes of channel masks after the header (0 or 12)
    HANDLE file;                     // +0x2C: a mapped file, its mapping and its view
    HANDLE mapping;                  // +0x30
    void* view;                      // +0x34
    HPALETTE palette;                // +0x38
};
static_assert(offsetof(CDib, bits) == 0x0C);
static_assert(offsetof(CDib, header) == 0x10);
static_assert(offsetof(CDib, imageSize) == 0x20);
static_assert(offsetof(CDib, palette) == 0x38);
static_assert(sizeof(CDib) == 0x3C);

// The bytes of a header's pixels: its biSizeImage, or (when that is 0) its rows padded to four bytes.
std::uint32_t DibImageSize(const BITMAPINFOHEADER* header);

struct CDXSurface;   // texture.h

// (surface_capture.cpp; the holder in ECX, the rectangle in EDX): a new bitmap of the surface's
// pixels inside `rect`, in the surface's depth (16 and 32 bits with its channel masks), rows
// bottom-up. The surface is locked for reading meanwhile.
CDib* __fastcall CaptureSurfaceRect(CDXSurface* surface, const RECT* rect);
// (thiscall): the same of the whole surface.
CDib* __fastcall CaptureSurface(CDXSurface* surface);
