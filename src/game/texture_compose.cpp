// The texture manager's composed textures: a party unit's skin is
// made of layers (the skin and the armour / weapon textures), drawn into one square image and
// cached on disk as <game folder>textures\<name>.mmp. The cached file keeps the request's layer
// bytes after its header (the extra block), so an unchanged request reuses the file. Callers:
// CUnitClientSpecific::ComposeSkin, BuildUnitSkins. See
// docs/notes/items_spells.md ("Composed textures").
#include "texture.h"
#include "game_strings.h"
#include "unit_client_models.h"

#include "lib/file_io.h"
#include "lib/containers.h"
#include "lib/crt.h"
#include "lib/gameapi.h"
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include <vector>
#include "game_singletons.h"

namespace {

const char* const kTextures = "textures\\";
const char* const kMmp = ".mmp";
const char* const kCantWrite = "Can't open file \"%s\" to write result texture.\n";    // "Can't open file \"%s\" to write result texture.\n"

constexpr std::uint32_t kMmpMagic = 0x504D4D;       // "MMP"
constexpr unsigned kWriteCreate = lib::File::modeCreate | lib::File::modeWrite;

const char* const kCantFindLayer = "Can't find texture \"%s\" for layer composition.\n";    // "Can't find texture \"%s\" for layer composition.\n"
const char* const kWrongLayerSize = "Texture \"%s\" dimentions are incorrect.\n";   // "Texture \"%s\" dimentions are incorrect.\n"

std::string CachePath(const std::string& name) { return *g_dataFolder + kTextures + name + kMmp; }

// Reads an opened cached file's header and extra block, closes it, and tells whether the extra
// block is the request's layer bytes.
bool CacheMatches(lib::File& file, MmpHeader& header, const SkinRequest* request) {
    file.Read(&header, sizeof(MmpHeader));
    int size = static_cast<int>(header.extraSize);
    std::vector<std::uint8_t> extra(static_cast<std::size_t>(size));
    if (size != 0) file.Read(extra.data(), static_cast<unsigned>(size));
    file.Close();
    return size == std::ssize(request->layers) && std::memcmp(extra.data(), request->layers.data(), static_cast<std::size_t>(size)) == 0;
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
// converted again.
void WriteLevels(lib::File& file, const MmpHeader& header, std::vector<std::uint32_t>& image, int kind, unsigned mipCount) {
    unsigned width = header.width;
    unsigned height = header.height;
    unsigned count = height * width;
    std::vector<std::uint16_t> converted(count);
    std::vector<std::uint32_t> halved(count);
    CPixelConverter* converter = g_sceneRenderer->device_->converter_;
    converter->ConvertTo16(converted.data(), image.data(), kind, count);
    file.Write(converted.data(), count * 2);
    std::uint32_t* source = image.data();
    std::uint32_t* target = halved.data();
    if (mipCount > 1) {
        for (unsigned left = mipCount - 1; left != 0; --left) {
            width >>= 1;
            height >>= 1;
            converter->Halve(target, source, width);
            count = height * width;
            converter->ConvertTo16(converted.data(), target, kind, count);
            file.Write(converted.data(), count * 2);
            std::uint32_t* done = target;
            target = source;
            source = done;
        }
    }
    file.Close();
}

} // namespace


int CTextureManager::DrawLayers(std::uint32_t* image, const std::vector<std::string>* names, int width) {
    CPixelConverter* converter = g_sceneRenderer->device_->converter_;
    int drawn = 0;
    for (const std::string& name : *names) {
        auto* data = GetFileData(name);
        if (!data) {
            game::DebugLog(kCantFindLayer, name);
            continue;
        }
        if (data->width != static_cast<std::uint32_t>(width) || data->height != static_cast<std::uint32_t>(width))
            return game::FatalError(kWrongLayerSize, name);
        const auto* pixels = reinterpret_cast<const std::uint8_t*>(data + 1) + data->extraSize;
        converter->UnpackTo32(image, pixels, static_cast<unsigned>(width * width), data->format == kMmpFormatPnt3);
        ++drawn;
        CloseFile(data);
    }
    return drawn;
}

// `out` receives the name of the texture used; `name` is the cache file to use, `fallback`
// another one tried when `name` has no file. A cached file whose extra block equals the request's
// layer bytes is reused (1). Otherwise the image (`request->flags` pixels square, cleared to
// `fill`) is drawn from the layers and written with `mipCount` levels in `format` (1), or, without
// layer textures, the file is deleted (0). 0 when the drawing fails; FatalError's result when the
// file cannot be written.
int CTextureManager::Compose(std::string* out, const SkinRequest* request, const std::string* name,
                             const std::string* fallback, unsigned mipCount, std::uint32_t fill,
                             std::uint32_t format) {
    std::string path = CachePath((*name));
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    lib::File file;
    *out = *name;
    if (file.Open(path, lib::File::modeRead)) {
        if (CacheMatches(file, header, request)) return 1;
    } else if (std::strcmp(name->c_str(),
                               fallback->c_str()) != 0) {
        *out = *fallback;
        path = CachePath((*fallback));
        if (file.Open(path, lib::File::modeRead)) {
            if (CacheMatches(file, header, request)) return 1;
            *out = *name;
            path = CachePath((*name));
        }
    }

    if (request->textures.empty()) {
        gameapi::DeleteFileA(path.c_str());
        return 0;
    }
    int width = request->flags;
    std::vector<std::uint32_t> image(static_cast<std::size_t>(width * width), fill);
    if (DrawLayers(image.data(), &request->textures, request->flags) == 0) return 0;

    header.magic = kMmpMagic;
    SetPixelFormat(header, format);
    header.width = static_cast<std::uint32_t>(request->flags);
    header.height = static_cast<std::uint32_t>(request->flags);
    header.mipCount = mipCount;
    header.extraSize = static_cast<std::uint32_t>(std::ssize(request->layers));
    int kind = ConversionKind(format);
    if (!file.Open(path, kWriteCreate)) return game::FatalError(kCantWrite, path);
    file.Write(&header, sizeof(MmpHeader));
    if (header.extraSize != 0) file.Write(request->layers.data(), header.extraSize);
    WriteLevels(file, header, image, kind, mipCount);
    return 1;
}
