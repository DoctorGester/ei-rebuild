// Dib: construction, the header of a new bitmap and its memory. The file
// reader and writer are in dib_file.cpp.
#include "dib.h"

#include "lib/crt.h"

#include <cstring>

namespace {

constexpr int kMaskBytes = 3 * sizeof(std::uint32_t);   // the channel masks of a kDibBitFields header

bool HasMasks(int bitCount) { return bitCount == 16 || bitCount == 32; }

// The bytes of the header's pixels: its imageSize, or (when that is 0) its rows padded to four bytes.
std::uint32_t ImageSize(const DibHeader* header) {
    if (header->imageSize != 0) return header->imageSize;
    return header->height * DibRowBytes(header);
}

} // namespace

std::uint32_t DibImageSize(const DibHeader* header) { return ImageSize(header); }

std::uint32_t DibRowBytes(const DibHeader* header) {
    const std::uint32_t rowBits = static_cast<std::uint32_t>(header->bitCount) * header->width;
    return ((rowBits + 31) >> 5) * 4;
}


Dib::Dib(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    Create(width, height, bitCount, red, green, blue);
}

void Dib::Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    Empty();
    maskBytes = HasMasks(bitCount) ? kMaskBytes : 0;
    header = static_cast<DibHeader*>(::operator new(maskBytes + sizeof(DibHeader)));
    header->size = sizeof(DibHeader);
    header->width = width;
    header->height = height;
    header->planes = 1;
    header->bitCount = static_cast<std::uint16_t>(bitCount);
    header->imageSize = 0;
    header->xPixelsPerMeter = 0;
    header->yPixelsPerMeter = 0;
    header->coloursUsed = colourCount;   // 0: Empty has just cleared it
    header->coloursImportant = colourCount;
    if (HasMasks(bitCount)) {
        header->compression = kDibBitFields;
        auto* masks = reinterpret_cast<std::uint32_t*>(header + 1);
        masks[0] = red;
        masks[1] = green;
        masks[2] = blue;
    } else {
        header->compression = kDibRgb;
    }
    imageSize = ImageSize(header);
    colours = reinterpret_cast<DibColour*>(header + 1);
    std::memset(colours, 0, static_cast<std::size_t>(colourCount) * sizeof(DibColour));
}

void Dib::Empty() {
    ::operator delete(header);
    bits = std::vector<std::uint8_t>();
    header = nullptr;
    colours = nullptr;
    colourCount = 0;
    imageSize = 0;
    maskBytes = 0;
}

SIZE* Dib::Size(SIZE* out) const {
    if (!header) {
        out->cx = 0;
        out->cy = 0;
        return out;
    }
    out->cx = header->width;
    out->cy = header->height;
    return out;
}

std::uint8_t* Dib::AllocateBits() {
    if (header) bits.resize(static_cast<std::size_t>(header->height) * DibRowBytes(header));
    return bits.data();
}
