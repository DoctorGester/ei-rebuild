// CDib's file reader and writer (dib.h): a .bmp file read from and written to a lib::File.
// A lib::FileError from the file's calls ends in a message box and a result of 0, as any error of the
// game's file calls did.
#include "dib.h"
#include "app.h"

#include "lib/file_io.h"

#include <cstdint>

namespace {


constexpr WORD kBitmapFileType = 0x4D42;   // "BM"

// The colour table's entries for a header: its biClrUsed, or by the depth: all of them for 1, 4 and
// 8 bits, none for 16, 24 and 32. Any other depth keeps `current`.
int ColourCount(const BITMAPINFOHEADER* header, int current) {
    if (header->biClrUsed != 0) return static_cast<int>(header->biClrUsed);
    switch (header->biBitCount) {
    case 1:
    case 4:
    case 8:
        return 1 << header->biBitCount;
    case 16:
    case 24:
    case 32:
        return 0;
    default:
        return current;
    }
}

} // namespace

int CDib::Read(lib::File* input) {
    Empty();
    try {
        BITMAPFILEHEADER fileHeader;
        if (input->Read(&fileHeader, sizeof(fileHeader)) != sizeof(fileHeader)) {
            app::ShowLibraryMessageBox("read error 1");
            return 0;
        }
        if (fileHeader.bfType != kBitmapFileType) {
            app::ShowLibraryMessageBox("Invalid bitmap file");
            return 0;
        }
        // Everything between the file header and the bits: the info header, then masks or colours.
        const std::uint32_t headerBytes = fileHeader.bfOffBits - sizeof(fileHeader);
        header = static_cast<BITMAPINFOHEADER*>(::operator new(headerBytes));
        input->Read(header, headerBytes);
        imageSize = DibImageSize(header);
        colours = reinterpret_cast<RGBQUAD*>(header + 1);
        bits.resize(imageSize);
        input->Read(bits.data(), imageSize);
    } catch (const lib::FileError&) {
        app::ShowLibraryMessageBox("Read error 1");
        return 0;
    }
    colourCount = ColourCount(header, colourCount);
    return 1;
}

int CDib::Write(lib::File* output) {
    BITMAPFILEHEADER fileHeader;
    fileHeader.bfType = kBitmapFileType;
    fileHeader.bfSize = imageSize + maskBytes + sizeof(BITMAPINFOHEADER) + sizeof(fileHeader);
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = maskBytes + sizeof(BITMAPINFOHEADER) + sizeof(fileHeader);
    try {
        output->WriteValue(fileHeader);
        output->Write(header, maskBytes + sizeof(BITMAPINFOHEADER));
        output->Write(bits.data(), imageSize);
    } catch (const lib::FileError&) {
        app::ShowLibraryMessageBox("write error");
        return 0;
    }
    return 1;
}
