// CDib's file reader and writer (dib.h; A-E2): a .bmp file read from and written to an MFC file.
// Both run the file's calls inside MFC's TRY / CATCH(CException) (lib/mfc_try.h): what the file
// throws ends in a message box and a result of 0.
#include "dib.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/mfc_try.h"
#include "originals_library.h"

#include <cstdint>

namespace {

const char* Text(std::uintptr_t address) { return reinterpret_cast<const char*>(address); }

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

int CDib::Read(mfc::CFile* source) {
    auto* input = reinterpret_cast<mfc::CFileVirtuals*>(source);
    Empty();
    {
        mfc::ExceptionLink link;
        try {
            BITMAPFILEHEADER fileHeader;
            if (input->Read(&fileHeader, sizeof(fileHeader)) != sizeof(fileHeader)) {
                AfxMessageBox(Text(0x00796D7C), 0, 0);   // "read error 1"
                return 0;
            }
            if (fileHeader.bfType != kBitmapFileType) {
                AfxMessageBox(Text(0x00796CF0), 0, 0);   // "Invalid bitmap file"
                return 0;
            }
            // Everything between the file header and the bits: the info header, then masks or colours.
            const std::uint32_t headerBytes = fileHeader.bfOffBits - sizeof(fileHeader);
            header = static_cast<BITMAPINFOHEADER*>(gamecrt::operator_new(headerBytes));
            bitsOwner = kNew;
            headerOwner = kNew;
            input->Read(header, headerBytes);
            imageSize = DibImageSize(header);
            colours = reinterpret_cast<RGBQUAD*>(header + 1);
            bits = static_cast<std::uint8_t*>(gamecrt::operator_new(imageSize));
            input->Read(bits, imageSize);
        } catch (CException* caught) {
            link.exception = caught;
            AfxMessageBox(Text(0x00796D6C), 0, 0);   // "Read error 1"
            return 0;
        }
    }
    colourCount = ColourCount(header, colourCount);
    MakePalette();
    return 1;
}
EI_REPLACE_METHOD(0x006D5910, CDib::Read);

int CDib::Write(mfc::CFile* target) {
    auto* output = reinterpret_cast<mfc::CFileVirtuals*>(target);
    BITMAPFILEHEADER fileHeader;
    fileHeader.bfType = kBitmapFileType;
    fileHeader.bfSize = imageSize + maskBytes + sizeof(BITMAPINFOHEADER) + sizeof(fileHeader);
    fileHeader.bfReserved1 = 0;
    fileHeader.bfReserved2 = 0;
    fileHeader.bfOffBits = maskBytes + sizeof(BITMAPINFOHEADER) + sizeof(fileHeader);
    mfc::ExceptionLink link;
    try {
        output->Write(&fileHeader, sizeof(fileHeader));
        output->Write(header, maskBytes + sizeof(BITMAPINFOHEADER));
        output->Write(bits, imageSize);
    } catch (CException* caught) {
        link.exception = caught;
        AfxMessageBox(Text(0x00796D8C), 0, 0);   // "write error"
        return 0;
    }
    return 1;
}
EI_REPLACE_METHOD(0x006D5AC0, CDib::Write);
