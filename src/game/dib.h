// CDib (the game's class "CDib"): a device independent bitmap as a .bmp file holds one. The game reads loose
// textures\<name>.bmp overrides with it and writes the screenshots and movie frames a surface capture fills. See
// docs/notes/models.md ("CDib").
#pragma once

#include "lib/gameapi.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lib {
class File;
}

class CDib {
public:
    CDib() = default;
    // Create's bitmap.
    CDib(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);
    CDib(const CDib&) = delete;
    CDib& operator=(const CDib&) = delete;
    ~CDib() { Empty(); }

    // The bitmap emptied, then a new header for `width` x `height` pixels of `bitCount` bits, without a colour table and without bits
    // (AllocateBits). 16 and 32 bits: BI_BITFIELDS with the three channel masks after the header.
    void Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);
    // The header, the bits and the palette freed; every field cleared.
    void Empty();
    // The header's width and height, 0 x 0 without a header. Returns `out`.
    SIZE* Size(SIZE* out) const;
    int MakePalette();
    // Memory for the pixels, width * height * planes * (bits / 8) bytes: rows without
    // padding, while `imageSize` (what Write writes) counts rows padded to four bytes. Returns
    // the bits (unchanged without a header).
    std::uint8_t* AllocateBits();
    // The bitmap emptied, then the file header, the info header with what follows it up to the bits (masks or
    // colours), the bits; the colour count and the palette. A short or non-"BM" file, or a lib::FileError
    // from the reads: a message box and 0. Returns 1.
    int Read(lib::File* file);
    // The file header, the info header with the masks, `imageSize`
    // bytes of bits. A lib::FileError from the writes: a message box and 0. Returns 1.
    int Write(lib::File* file);

    RGBQUAD* colours = nullptr;              // after the header
    std::vector<std::uint8_t> bits;
    BITMAPINFOHEADER* header = nullptr;      // with what follows it (::operator new), or null
    std::uint32_t imageSize = 0;             // bytes of bits
    int colourCount = 0;
    int maskBytes = 0;                       // bytes of channel masks after the header (0 or 12)
    HPALETTE palette = nullptr;
};

// The bytes of a header's pixels: its biSizeImage, or (when that is 0) its rows padded to four bytes.
std::uint32_t DibImageSize(const BITMAPINFOHEADER* header);

class CTextureDevice;

// A new bitmap of the screen of `device` (the new renderer's last frame), in the screen's depth (16 and 32 bits with
// its channel masks), rows bottom-up; null when the new renderer has no frame.
CDib* CaptureScreen(const CTextureDevice* device);
