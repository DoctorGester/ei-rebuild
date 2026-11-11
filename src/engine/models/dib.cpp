// Dib: construction, the header of a new bitmap and its memory. The file
// reader and writer are in dib.cpp.
#include "engine/models/dib.h"

#include "game/app/app.h"
#include "lib/crt.h"
#include "platform/platform.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

namespace {

constexpr int kMaskBytes = 3 * sizeof(std::uint32_t);   // the channel masks of a kDibBitFields header

bool HasMasks(int bitCount) { return bitCount == 16 || bitCount == 32; }

// The bytes of the header's pixels: its imageSize, or (when that is 0) its rows padded to four bytes.
std::uint32_t ImageSize(const DibHeader& header) {
    if (header.imageSize != 0) return header.imageSize;
    return header.height * DibRowBytes(header);
}

} // namespace

std::uint32_t DibImageSize(const DibHeader& header) { return ImageSize(header); }

std::uint32_t DibRowBytes(const DibHeader& header) {
    const std::uint32_t rowBits = static_cast<std::uint32_t>(header.bitCount) * header.width;
    return ((rowBits + 31) >> 5) * 4;
}

Dib::Dib(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    Create(width, height, bitCount, red, green, blue);
}

void Dib::Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    Empty();
    maskBytes = HasMasks(bitCount) ? kMaskBytes : 0;
    header.emplace();
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
        masks[0] = red;
        masks[1] = green;
        masks[2] = blue;
    } else {
        header->compression = kDibRgb;
    }
    imageSize = ImageSize(*header);
}

void Dib::Empty() {
    bits = std::vector<std::uint8_t>();
    header.reset();
    colourCount = 0;
    imageSize = 0;
    maskBytes = 0;
}

Size Dib::Extent() const {
    if (!header) return {};
    return {header->width, header->height};
}

std::uint8_t* Dib::AllocateBits() {
    if (header) bits.resize(static_cast<std::size_t>(header->height) * DibRowBytes(*header));
    return bits.data();
}

// --- File --------------------------------------------------------------------------------------------

// Dib's file reader and writer (dib.h): a .bmp file read from and written to a platform::File.
// A platform::FileError from the file's calls ends in a message box and a result of 0, as any error of the
// game's file calls did.

namespace {

constexpr std::uint16_t kBitmapFileType = 0x4D42;   // "BM"

// The colour table's entries for a header: its coloursUsed, or by the depth: all of them for 1, 4 and
// 8 bits, none for 16, 24 and 32. Any other depth keeps `current`.
int ColourCount(const DibHeader& header, int current) {
    if (header.coloursUsed != 0) return static_cast<int>(header.coloursUsed);
    switch (header.bitCount) {
    case 1:
    case 4:
    case 8:
        return 1 << header.bitCount;
    case 16:
    case 24:
    case 32:
        return 0;
    default:
        return current;
    }
}

} // namespace

int Dib::Read(platform::File* input) {
    Empty();
    try {
        DibFileHeader fileHeader;
        if (input->ReadValue(fileHeader) != sizeof(fileHeader)) {
            app::ShowLibraryMessageBox("read error 1");
            return 0;
        }
        if (fileHeader.type != kBitmapFileType) {
            app::ShowLibraryMessageBox("Invalid bitmap file");
            return 0;
        }
        // Everything between the file header and the bits: the info header, then masks or colours (not kept).
        const std::uint32_t headerBytes = fileHeader.bitsOffset - sizeof(fileHeader);
        std::vector<std::uint8_t> headerData(headerBytes);
        input->Read(headerData.data(), headerBytes);
        header.emplace();
        std::memcpy(&*header, headerData.data(), std::min<std::size_t>(headerBytes, sizeof(DibHeader)));
        imageSize = DibImageSize(*header);
        bits.resize(imageSize);
        input->Read(bits.data(), imageSize);
    } catch (const platform::FileError&) {
        app::ShowLibraryMessageBox("Read error 1");
        return 0;
    }
    colourCount = ColourCount(*header, colourCount);
    return 1;
}

int Dib::Write(platform::File* output) {
    DibFileHeader fileHeader;
    fileHeader.type = kBitmapFileType;
    fileHeader.size = imageSize + maskBytes + sizeof(DibHeader) + sizeof(fileHeader);
    fileHeader.reserved1 = 0;
    fileHeader.reserved2 = 0;
    fileHeader.bitsOffset = maskBytes + sizeof(DibHeader) + sizeof(fileHeader);
    try {
        output->WriteValue(fileHeader);
        output->WriteValue(*header);
        if (maskBytes) output->Write(masks, maskBytes);
        output->Write(bits.data(), imageSize);
    } catch (const platform::FileError&) {
        app::ShowLibraryMessageBox("write error");
        return 0;
    }
    return 1;
}
