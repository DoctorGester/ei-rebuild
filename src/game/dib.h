// Dib (the game's class "Dib"): a device independent bitmap as a .bmp file holds one. The game reads loose
// textures\<name>.bmp overrides with it and writes the screenshots and movie frames a surface capture fills. See
// docs/notes/models.md ("Dib").
#pragma once

#include "lib/rect.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

// A .bmp file's headers and colour table entry, byte for byte as the file holds them (the system's BITMAPFILEHEADER,
// BITMAPINFOHEADER and RGBQUAD).
#pragma pack(push, 2)
struct DibFileHeader {
    std::uint16_t type = 0;     // "BM"
    std::uint32_t size = 0;     // the file's bytes
    std::uint16_t reserved1 = 0;
    std::uint16_t reserved2 = 0;
    std::uint32_t bitsOffset = 0;  // where the bits start
};
#pragma pack(pop)
static_assert(sizeof(DibFileHeader) == 14);

struct DibHeader {
    std::uint32_t size = 0;           // this header's bytes (40)
    std::int32_t width = 0;
    std::int32_t height = 0;          // positive: the rows bottom-up
    std::uint16_t planes = 0;
    std::uint16_t bitCount = 0;
    std::uint32_t compression = 0;    // kDibRgb or kDibBitFields
    std::uint32_t imageSize = 0;      // the bits' bytes, or 0
    std::int32_t xPixelsPerMeter = 0;
    std::int32_t yPixelsPerMeter = 0;
    std::uint32_t coloursUsed = 0;
    std::uint32_t coloursImportant = 0;
};
static_assert(sizeof(DibHeader) == 40);

inline constexpr std::uint32_t kDibRgb = 0;         // the bits as they are
inline constexpr std::uint32_t kDibBitFields = 3;   // the three channel masks after the header

namespace platform {
class File;
}

class Dib {
public:
    Dib() = default;
    // Create's bitmap.
    Dib(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);
    Dib(const Dib&) = delete;
    Dib& operator=(const Dib&) = delete;
    ~Dib() { Empty(); }

    // The bitmap emptied, then a new header for `width` x `height` pixels of `bitCount` bits, without a colour table and without bits
    // (AllocateBits). 16 and 32 bits: kDibBitFields with the three channel masks after the header.
    void Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue);
    // The header and the bits freed; every field cleared.
    void Empty();
    // The header's width and height, 0 x 0 without a header.
    Size Extent() const;
    // Memory for the pixels, height rows of DibRowBytes (padded to four bytes, as `imageSize` counts them). Returns
    // the bits (unchanged without a header).
    std::uint8_t* AllocateBits();
    // The bitmap emptied, then the file header, the info header with what follows it up to the bits (masks or
    // colours), the bits; the colour count. A short or non-"BM" file, or a platform::FileError
    // from the reads: a message box and 0. Returns 1.
    int Read(platform::File* file);
    // The file header, the info header with the masks, `imageSize`
    // bytes of bits. A platform::FileError from the writes: a message box and 0. Returns 1.
    int Write(platform::File* file);

    std::vector<std::uint8_t> bits;
    std::optional<DibHeader> header;         // none while empty
    std::uint32_t masks[3] = {};             // Create's channel masks (red, green, blue), written after the header
    std::uint32_t imageSize = 0;             // bytes of bits
    int colourCount = 0;
    int maskBytes = 0;                       // bytes of channel masks after the header (0 or 12)
};

// The bytes of a header's pixels: its imageSize, or (when that is 0) its rows padded to four bytes.
std::uint32_t DibImageSize(const DibHeader& header);
// The bytes of one row of a header's bitmap: width * bits per pixel, up to a multiple of four bytes.
std::uint32_t DibRowBytes(const DibHeader& header);

class TextureDevice;

// A new bitmap of the screen of `device` (the new renderer's last frame), in the screen's depth (16 and 32 bits with
// its channel masks), rows bottom-up; null when the new renderer has no frame.
Dib* CaptureScreen(const TextureDevice* device);
