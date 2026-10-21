// The texture manager's composed textures (00693180, thiscall, RET 0x1C): a party unit's skin is
// made of layers (the skin and the armour / weapon textures), drawn into one square image and
// cached on disk as <game folder>textures\<name>.mmp. The cached file keeps the request's layer
// bytes after its header (the extra block), so an unchanged request reuses the file. Callers:
// 00536670 / 005370F0 (CUnitClientSpecific::ComposeSkin), 00568800 (BuildUnitSkins). See
// docs/notes/items_spells.md ("Composed textures").
#include "texture.h"
#include "game_strings.h"
#include "unit_client_models.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include "lib/mfc.h"

#include <cstdint>
#include <cstring>
#include "originals_library.h"
#include "originals_render.h"
#include "game_data.h"

#pragma warning(disable : 4740) // naked forwarders

namespace {

const mfc::CString& GameFolder() { return ei::GameData<const mfc::CString>(0x007AF460); }
const char* const kTextures = "textures\\";
const char* const kMmp = ".mmp";
const char* const kCantWrite = "Can't open file \"%s\" to write result texture.\n";    // "Can't open file \"%s\" to write result texture.\n"

constexpr std::uint32_t kMmpMagic = 0x504D4D;       // "MMP"
constexpr unsigned kWriteCreate = 0x1001;           // CFile::modeCreate | modeWrite


// The aligned byte allocation [004ECB50].
std::uint8_t* AllocBytes(int count) { return lib::AlignedArray<std::uint8_t>::Allocate(count); }
const char* const kCantFindLayer = "Can't find texture \"%s\" for layer composition.\n";    // "Can't find texture \"%s\" for layer composition.\n"
const char* const kWrongLayerSize = "Texture \"%s\" dimentions are incorrect.\n";   // "Texture \"%s\" dimentions are incorrect.\n"

// A block from the game heap aligned to 32 bytes; the raw pointer is kept just before it.
template <typename T>
T* NewAligned(std::size_t bytes) {
    char* raw = static_cast<char*>(gamecrt::operator_new(bytes + 0x24));
    char* data = reinterpret_cast<char*>((reinterpret_cast<std::uintptr_t>(raw) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<char**>(data)[-1] = raw;
    return reinterpret_cast<T*>(data);
}
void DeleteAligned(void* data) { gamecrt::operator_delete(static_cast<void**>(data)[-1]); }

// The cached file's extra block (the original keeps it as an aligned byte array).
struct ReadBuffer {
    std::uint8_t* data = nullptr;
    int size = 0;
    int capacity = 0;
    int growBy = 0;
    ~ReadBuffer() {
        if (data) DeleteAligned(data);
    }
};

mfc::CString CachePath(const mfc::CString& name) { return GameFolder() + kTextures + name + kMmp; }

// Reads an opened cached file's header and extra block, closes it, and tells whether the extra
// block is the request's layer bytes.
bool CacheMatches(mfc::CFile& file, MmpHeader& header, ReadBuffer& buffer, const SkinRequest* request) {
    file.Read(&header, sizeof(MmpHeader));
    int size = static_cast<int>(header.extraSize);
    buffer.data = AllocBytes(size);
    buffer.capacity = size;
    buffer.size = size;
    if (size != 0) file.Read(buffer.data, static_cast<unsigned>(size));
    file.Close();
    return size == request->layerBytes && std::memcmp(buffer.data, request->layers, static_cast<std::size_t>(size)) == 0;
}

// The pixel format fields for `format`; 0x5650 (5-6-5) is filled in place.
void SetPixelFormat(MmpHeader& header, std::uint32_t format) {
    std::uint32_t* words = reinterpret_cast<std::uint32_t*>(&header);
    switch (format) {
    case 0x8888: header.SetFormat8888(); return;
    case 0x4444: header.SetFormat4444(); return;
    case 0x5551: header.SetFormat5551(); return;
    case 0x5650:
        // Alpha, red, green, blue: mask, shift, bits each (+0x18..+0x44).
        words[0x18 / 4] = 0;
        words[0x20 / 4] = 0;
        words[0x1C / 4] = 0;
        words[0x24 / 4] = 0xF800;
        words[0x2C / 4] = 5;
        words[0x28 / 4] = 0xB;
        words[0x30 / 4] = 0x7E0;
        words[0x38 / 4] = 6;
        words[0x34 / 4] = 5;
        words[0x3C / 4] = 0x1F;
        words[0x44 / 4] = 5;
        words[0x40 / 4] = 0;
        header.bitsPerPixel = 0x10;
        header.format = format;
        return;
    case 0x32544E50:   // "PNT2"
    case 0x33544E50:   // "PNT3"
        header.SetNoFormat();
        header.bitsPerPixel = 0x20;
        header.format = format;
        return;
    default:
        header.SetNoFormat();
        return;
    }
}

// The converter's kind for the 16-bit formats; -1 for the others.
int ConversionKind(std::uint32_t format) {
    if (format == 0x4444) return 3;
    if (format == 0x5551) return 2;
    if (format == 0x5650) return 0;
    return -1;
}

// Writes the levels: the image converted to 16 bits, then `mipCount - 1` times halved and
// converted again. Frees the three blocks.
void WriteLevels(mfc::CFile& file, const MmpHeader& header, std::uint32_t* image, int kind, unsigned mipCount) {
    unsigned width = header.width;
    unsigned height = header.height;
    std::uint16_t* converted = NewAligned<std::uint16_t>(height * width * 2);
    unsigned count = height * width;
    std::uint32_t* halved = NewAligned<std::uint32_t>(count * 4);
    CPixelConverter* converter = PixelConverter();
    converter->ConvertTo16(converted, image, kind, count);
    file.Write(converted, count * 2);
    std::uint32_t* source = image;
    std::uint32_t* target = halved;
    if (mipCount > 1) {
        for (unsigned left = mipCount - 1; left != 0; --left) {
            width >>= 1;
            height >>= 1;
            converter->Halve(target, source, width);
            count = height * width;
            converter->ConvertTo16(converted, target, kind, count);
            file.Write(converted, count * 2);
            std::uint32_t* done = target;
            target = source;
            source = done;
        }
    }
    file.Close();
    DeleteAligned(converted);
    DeleteAligned(source);
    DeleteAligned(target);
}

} // namespace

// --- 00693010 ----------------------------------------------------------------------------------------

int CTextureManager::DrawLayers(std::uint32_t* image, const mfc::CStringArray* names, int width) {
    CPixelConverter* converter = PixelConverter();
    int drawn = 0;
    for (int i = 0; i < names->GetSize(); ++i) {
        const auto* data = static_cast<const MmpHeader*>(GetFile((*names)[i], nullptr));
        if (!data) {
            game::DebugLog(kCantFindLayer, static_cast<const char*>((*names)[i]));
            continue;
        }
        if (data->width != static_cast<std::uint32_t>(width) || data->height != static_cast<std::uint32_t>(width))
            return game::FatalError(kWrongLayerSize, static_cast<const char*>((*names)[i]));
        const auto* pixels = reinterpret_cast<const std::uint8_t*>(data + 1) + data->extraSize;
        converter->UnpackTo32(image, pixels, static_cast<unsigned>(width * width), data->format == kMmpFormatPnt3);
        ++drawn;
        CloseFile(const_cast<MmpHeader*>(data));
    }
    return drawn;
}
EI_REPLACE_METHOD(0x00693010, CTextureManager::DrawLayers);

// 00693180. `out` receives the name of the texture used; `name` is the cache file to use, `fallback`
// another one tried when `name` has no file. A cached file whose extra block equals the request's
// layer bytes is reused (1). Otherwise the image (`request->flags` pixels square, cleared to
// `fill`) is drawn from the layers and written with `mipCount` levels in `format` (1), or, without
// layer textures, the file is deleted (0). 0 when the drawing fails; FatalError's result when the
// file cannot be written.
int __fastcall ComposeTexture(void* manager, void*, mfc::CString* out, const SkinRequest* request,
                              const mfc::CString* name, const mfc::CString* fallback, unsigned mipCount,
                              std::uint32_t fill, std::uint32_t format) {
    mfc::CString path = CachePath(*name);
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    ReadBuffer buffer;
    mfc::CFile file;
    *out = *name;
    if (file.Open(path, mfc::CFile::modeRead, nullptr)) {
        if (CacheMatches(file, header, buffer, request)) return 1;
    } else if (gamecrt::mbscmp(reinterpret_cast<const unsigned char*>(static_cast<const char*>(*name)),
                               reinterpret_cast<const unsigned char*>(static_cast<const char*>(*fallback))) != 0) {
        *out = *fallback;
        path = CachePath(*fallback);
        if (file.Open(path, mfc::CFile::modeRead, nullptr)) {
            if (CacheMatches(file, header, buffer, request)) return 1;
            *out = *name;
            path = CachePath(*name);
        }
    }

    if (request->textures.GetSize() == 0) {
        gameapi::DeleteFileA(path);
        return 0;
    }
    int width = request->flags;
    std::uint32_t* image = NewAligned<std::uint32_t>(static_cast<std::size_t>(width * width * 4));
    for (int i = 0; i < width * width; ++i) image[i] = fill;
    if (static_cast<CTextureManager*>(manager)->DrawLayers(image, &request->textures, request->flags) == 0) {
        DeleteAligned(image);
        return 0;
    }

    header.magic = kMmpMagic;
    SetPixelFormat(header, format);
    header.width = static_cast<std::uint32_t>(request->flags);
    header.height = static_cast<std::uint32_t>(request->flags);
    header.mipCount = mipCount;
    header.extraSize = static_cast<std::uint32_t>(request->layerBytes);
    int kind = ConversionKind(format);
    if (!file.Open(path, kWriteCreate, nullptr)) return game::FatalError(kCantWrite, static_cast<const char*>(path));
    file.Write(&header, sizeof(MmpHeader));
    if (header.extraSize != 0) file.Write(request->layers, header.extraSize);
    WriteLevels(file, header, image, kind, mipCount);
    return 1;
}
EI_REPLACE(0x00693180, ComposeTexture);
