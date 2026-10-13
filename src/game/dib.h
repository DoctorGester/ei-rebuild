// CDib (006D49E0-006D6AB0; MFC class "CDib", runtime class 00796CD0, vtable 00744288): a device
// independent bitmap as a .bmp file holds one. The game reads loose textures\<name>.bmp overrides
// with it (texture_bmp.cpp) and writes the screenshots and movie frames a surface capture
// (006D6D40) fills. See docs/notes/models.md ("CDib").
#pragma once

#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstddef>
#include <cstdint>

struct CDib {
    // Who frees `header` / `bits`.
    enum Owner : int { kNotOwned = 0, kNew = 1, kGlobal = 2 };

    // 006D4AA0: an empty bitmap.
    CDib* Construct();
    // 006D4B30 (RET 0x18): Create's bitmap (nothing but the vtable is set first).
    CDib* ConstructSized(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green,
                         std::uint32_t blue);
    // 006D4CD0.
    void Destruct();
    // 006D4B10 (slot 1, RET 4): the destructor, and the object's memory freed with bit 0 of `flags`.
    CDib* Delete(unsigned flags);
    // 006D4A60 (slot 0): the class's CRuntimeClass.
    const void* GetRuntimeClass() const;
    // 006D5BB0 (slot 2, RET 4): the archive is flushed, then the bitmap written to or read from its
    // file.
    void Serialize(mfc::CArchive* archive);

    // 006D4BA0 (RET 0x18). A new header for `width` x `height` pixels of `bitCount` bits, without a
    // colour table and without bits (AllocateBits). 16 and 32 bits: BI_BITFIELDS with the three
    // channel masks after the header.
    void Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);
    // 006D69B0. Frees whatever the bitmap owns and zeroes it.
    void Empty();
    // 006D4D10 (RET 4): the header's width and height, 0 x 0 without a header. Returns `out`.
    SIZE* Size(SIZE* out) const;
    // 006D54D0: a GDI palette from the colour table; 0 without colours.
    int MakePalette();
    // 006D6AB0: memory for the pixels, width * height * planes * (bits / 8) bytes: rows without
    // padding, while `imageSize` (what Write writes) counts rows padded to four bytes. Returns
    // `bits` (unchanged without a header).
    std::uint8_t* AllocateBits();
    // 006D5910 (RET 4), not rewritten (an MFC TRY / CATCH around the file's reads): the file header,
    // the info header with its colours, the bits. A short or non-"BM" file: a message box and 0.
    int Read(mfc::CFile* file);
    // 006D5AC0 (RET 4), not rewritten (an MFC TRY / CATCH around the file's writes): the file
    // header, the info header with the masks, `imageSize` bytes of bits. Returns 1.
    int Write(mfc::CFile* file);

    const void* vtable;              // 00744288: GetRuntimeClass, Delete, Serialize
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
