// Dib's file reader and writer (dib.h): a .bmp file read from and written to a platform::File.
// A platform::FileError from the file's calls ends in a message box and a result of 0, as any error of the
// game's file calls did.
#include "dib.h"
#include "app.h"

#include "platform/file.h"

#include <cstdint>

namespace {


constexpr std::uint16_t kBitmapFileType = 0x4D42;   // "BM"

// The colour table's entries for a header: its coloursUsed, or by the depth: all of them for 1, 4 and
// 8 bits, none for 16, 24 and 32. Any other depth keeps `current`.
int ColourCount(const DibHeader* header, int current) {
    if (header->coloursUsed != 0) return static_cast<int>(header->coloursUsed);
    switch (header->bitCount) {
    case 1:
    case 4:
    case 8:
        return 1 << header->bitCount;
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
        if (input->Read(&fileHeader, sizeof(fileHeader)) != sizeof(fileHeader)) {
            app::ShowLibraryMessageBox("read error 1");
            return 0;
        }
        if (fileHeader.type != kBitmapFileType) {
            app::ShowLibraryMessageBox("Invalid bitmap file");
            return 0;
        }
        // Everything between the file header and the bits: the info header, then masks or colours.
        const std::uint32_t headerBytes = fileHeader.bitsOffset - sizeof(fileHeader);
        header = static_cast<DibHeader*>(::operator new(headerBytes));
        input->Read(header, headerBytes);
        imageSize = DibImageSize(header);
        colours = reinterpret_cast<DibColour*>(header + 1);
        bits.resize(imageSize);
        input->Read(bits.data(), imageSize);
    } catch (const platform::FileError&) {
        app::ShowLibraryMessageBox("Read error 1");
        return 0;
    }
    colourCount = ColourCount(header, colourCount);
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
        output->Write(header, maskBytes + sizeof(DibHeader));
        output->Write(bits.data(), imageSize);
    } catch (const platform::FileError&) {
        app::ShowLibraryMessageBox("write error");
        return 0;
    }
    return 1;
}
