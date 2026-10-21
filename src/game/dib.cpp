// CDib (dib.h): construction, the header of a new bitmap, its memory and its palette. The file
// reader and writer are in dib_file.cpp.
#include "dib.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "game_data.h"

#include <cstring>

#pragma warning(disable : 4740) // naked forwarders

namespace {

const void* DibVtable() { return ei::GamePointer<const void>(0x00744288); }
const void* DibRuntimeClass() { return ei::GamePointer<const void>(0x00796CD0); }

constexpr int kMaskBytes = 3 * sizeof(std::uint32_t);   // the channel masks of a BI_BITFIELDS header
constexpr std::uint16_t kPaletteVersion = 0x300;

bool HasMasks(int bitCount) { return bitCount == 16 || bitCount == 32; }

// The bytes of the header's pixels: its biSizeImage, or (when that is 0) its rows padded to four
// bytes.
std::uint32_t ImageSize(const BITMAPINFOHEADER* header) {
    if (header->biSizeImage != 0) return header->biSizeImage;
    const std::uint32_t rowBits = static_cast<std::uint32_t>(header->biBitCount) * header->biWidth;
    std::uint32_t rowDwords = rowBits >> 5;
    if (rowBits & 0x1F) ++rowDwords;
    return header->biHeight * rowDwords * 4;
}

// 006D49E0: the runtime class's CreateObject.
CDib* CreateDib() {
    auto* dib = static_cast<CDib*>(gamecrt::operator_new(sizeof(CDib)));
    return dib ? dib->Construct() : nullptr;
}
EI_REPLACE(0x006D49E0, CreateDib);

} // namespace

std::uint32_t DibImageSize(const BITMAPINFOHEADER* header) { return ImageSize(header); }

CDib* CDib::Construct() {
    vtable = DibVtable();
    bitsOwner = kNotOwned;
    headerOwner = kNotOwned;
    headerMemory = nullptr;
    bits = nullptr;
    header = nullptr;
    colours = nullptr;
    colourCount = 0;
    imageSize = 0;
    view = nullptr;
    mapping = nullptr;
    file = nullptr;
    bitmap = nullptr;
    palette = nullptr;
    Empty();
    return this;
}
EI_REPLACE_METHOD(0x006D4AA0, CDib::Construct);

CDib* CDib::ConstructSized(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green,
                           std::uint32_t blue) {
    vtable = DibVtable();
    Create(width, height, bitCount, red, green, blue);
    return this;
}
EI_REPLACE_METHOD(0x006D4B30, CDib::ConstructSized);

void CDib::Destruct() {
    vtable = DibVtable();
    Empty();
}
EI_REPLACE_METHOD(0x006D4CD0, CDib::Destruct);

CDib* CDib::Delete(unsigned flags) {
    Destruct();
    if (flags & 1) gamecrt::operator_delete(this);
    return this;
}
EI_REPLACE_METHOD(0x006D4B10, CDib::Delete);

const void* CDib::GetRuntimeClass() const { return DibRuntimeClass(); }
EI_REPLACE_METHOD(0x006D4A60, CDib::GetRuntimeClass);

void CDib::Serialize(mfc::CArchive* archive) {
    archive->Flush();
    if (archive->IsLoading()) Read(archive->File());
    else Write(archive->File());
}
EI_REPLACE_METHOD(0x006D5BB0, CDib::Serialize);

void CDib::Create(int width, int height, int bitCount, std::uint32_t red, std::uint32_t green, std::uint32_t blue) {
    // What Empty looks at (the constructor above leaves the object raw).
    file = nullptr;
    bitmap = nullptr;
    palette = nullptr;
    bitsOwner = kNotOwned;
    headerOwner = kNotOwned;
    Empty();
    maskBytes = HasMasks(bitCount) ? kMaskBytes : 0;
    header = static_cast<BITMAPINFOHEADER*>(gamecrt::operator_new(maskBytes + sizeof(BITMAPINFOHEADER)));
    headerOwner = kNew;
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
    bits = nullptr;
}
EI_REPLACE_METHOD(0x006D4BA0, CDib::Create);

void CDib::Empty() {
    if (file) {
        UnmapViewOfFile(view);
        CloseHandle(mapping);
        CloseHandle(file);
        file = nullptr;
    }
    if (headerOwner == kNew) {
        gamecrt::operator_delete(header);
    } else if (headerOwner == kGlobal) {
        GlobalUnlock(headerMemory);
        GlobalFree(headerMemory);
    }
    if (bitsOwner == kNew) gamecrt::operator_delete(bits);
    if (palette) DeleteObject(palette);
    palette = nullptr;
    if (bitmap) DeleteObject(bitmap);
    bitsOwner = kNotOwned;
    headerOwner = kNotOwned;
    headerMemory = nullptr;
    bits = nullptr;
    header = nullptr;
    colours = nullptr;
    colourCount = 0;
    imageSize = 0;
    view = nullptr;
    mapping = nullptr;
    file = nullptr;
    bitmap = nullptr;
    palette = nullptr;
    maskBytes = 0;
}
EI_REPLACE_METHOD(0x006D69B0, CDib::Empty);

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
EI_REPLACE_METHOD(0x006D4D10, CDib::Size);

int CDib::MakePalette() {
    if (colourCount == 0) return 0;
    if (palette) DeleteObject(palette);
    // A LOGPALETTE's header is four bytes; its entries follow.
    auto* description = static_cast<LOGPALETTE*>(
        gamecrt::operator_new(static_cast<std::size_t>(colourCount) * sizeof(PALETTEENTRY) + 4));
    description->palVersion = kPaletteVersion;
    description->palNumEntries = static_cast<WORD>(colourCount);
    for (int i = 0; i < colourCount; ++i) {
        PALETTEENTRY& entry = description->palPalEntry[i];
        entry.peRed = colours[i].rgbRed;
        entry.peGreen = colours[i].rgbGreen;
        entry.peBlue = colours[i].rgbBlue;
        entry.peFlags = 0;
    }
    palette = CreatePalette(description);
    gamecrt::operator_delete(description);
    return 1;
}
EI_REPLACE_METHOD(0x006D54D0, CDib::MakePalette);

std::uint8_t* CDib::AllocateBits() {
    if (header) {
        const std::uint32_t bytes = static_cast<std::uint32_t>(header->biBitCount >> 3) * header->biPlanes *
                                    header->biHeight * header->biWidth;
        bits = static_cast<std::uint8_t*>(gamecrt::operator_new(bytes));
        bitsOwner = kNew;
    }
    return bits;
}
EI_REPLACE_METHOD(0x006D6AB0, CDib::AllocateBits);
