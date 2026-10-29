// CDib: construction, the header of a new bitmap and its memory. The file
// reader and writer are in dib_file.cpp.
#include "dib.h"

#include "lib/crt.h"

#include <cstring>

namespace {

constexpr int kMaskBytes = 3 * sizeof(std::uint32_t);   // the channel masks of a BI_BITFIELDS header

bool HasMasks(int bitCount) { return bitCount == 16 || bitCount == 32; }

// The bytes of the header's pixels: its biSizeImage, or (when that is 0) its rows padded to four bytes.
std::uint32_t ImageSize(const BITMAPINFOHEADER* header) {
    if (header->biSizeImage != 0) return header->biSizeImage;
    return header->biHeight * DibRowBytes(header);
}

} // namespace

std::uint32_t DibImageSize(const BITMAPINFOHEADER* header) { return ImageSize(header); }

std::uint32_t DibRowBytes(const BITMAPINFOHEADER* header) {
    const std::uint32_t rowBits = static_cast<std::uint32_t>(header->biBitCount) * header->biWidth;
    return ((rowBits + 31) >> 5) * 4;
}


CDib::CDib(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    Create(width, height, bitCount, red, green, blue);
}

void CDib::Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    Empty();
    maskBytes = HasMasks(bitCount) ? kMaskBytes : 0;
    header = static_cast<BITMAPINFOHEADER*>(::operator new(maskBytes + sizeof(BITMAPINFOHEADER)));
    header->biSize = sizeof(BITMAPINFOHEADER);
    header->biWidth = width;
    header->biHeight = height;
    header->biPlanes = 1;
    header->biBitCount = static_cast<WORD>(bitCount);
    header->biSizeImage = 0;
    header->biXPelsPerMeter = 0;
    header->biYPelsPerMeter = 0;
    header->biClrUsed = colourCount;   // 0: Empty has just cleared it
    header->biClrImportant = colourCount;
    if (HasMasks(bitCount)) {
        header->biCompression = BI_BITFIELDS;
        auto* masks = reinterpret_cast<std::uint32_t*>(header + 1);
        masks[0] = red;
        masks[1] = green;
        masks[2] = blue;
    } else {
        header->biCompression = BI_RGB;
    }
    imageSize = ImageSize(header);
    colours = reinterpret_cast<RGBQUAD*>(header + 1);
    std::memset(colours, 0, static_cast<std::size_t>(colourCount) * sizeof(RGBQUAD));
}

void CDib::Empty() {
    ::operator delete(header);
    bits = std::vector<std::uint8_t>();
    header = nullptr;
    colours = nullptr;
    colourCount = 0;
    imageSize = 0;
    maskBytes = 0;
}

SIZE* CDib::Size(SIZE* out) const {
    if (!header) {
        out->cx = 0;
        out->cy = 0;
        return out;
    }
    out->cx = header->biWidth;
    out->cy = header->biHeight;
    return out;
}

std::uint8_t* CDib::AllocateBits() {
    if (header) bits.resize(static_cast<std::size_t>(header->biHeight) * DibRowBytes(header));
    return bits.data();
}
