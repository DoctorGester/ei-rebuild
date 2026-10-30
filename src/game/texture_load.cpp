// How a texture gets its pixels (Texture, TextureManager): blank textures the game draws into, .mmp files, loose
// .bmp files with their alpha and composed textures. See docs/notes/renderer.md (section 7).
#include "texture.h"

#include "console.h"
#include "dib.h"
#include "game_singletons.h"
#include "game_strings.h"
#include "options.h"
#include "surface.h"
#include "text_split.h"
#include "unit_client_models.h"
#include "../hook/log.h"
#include "lib/crt.h"
#include "lib/str.h"
#include "platform/platform.h"
#include "render/render.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <format>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kMmpMagic = 0x504D4D;   // "MMP"
constexpr std::uint32_t kAtlasFormat = 0x5551;   // the item pictures and image atlases

// The formats of the texture kinds: the texture format and the channels it writes into the header (mask, shift,
// bits; the header's other fields stay zero).
struct Channel {
    std::uint32_t mask, shift, bits;
};
struct BlankFormat {
    TextureKind kind;
    std::uint32_t id;
    Channel alpha, red, green, blue;
};
constexpr BlankFormat kBlankFormats[] = {
    {TextureKind::Blank565, 0x5650, {0, 0, 0}, {0xF800, 11, 5}, {0x7E0, 5, 6}, {0x1F, 0, 5}},
    {TextureKind::Blank1555, 0x5551, {0x8000, 15, 1}, {0x7C00, 10, 5}, {0x3E0, 5, 5}, {0x1F, 0, 5}},
    {TextureKind::Blank4444, 0x4444, {0xF000, 12, 4}, {0xF00, 8, 4}, {0xF0, 4, 4}, {0xF, 0, 4}},
};

const BlankFormat* FormatOfKind(TextureKind kind) {
    for (const BlankFormat& f : kBlankFormats)
        if (f.kind == kind) return &f;
    return nullptr;
}

} // namespace

void Texture::TakeBlankHolder(TextureDevice* device, int newWidth, int newHeight, bool mipmapped) {
    const render::PixelFormat pixelFormat = TextureFormat(format);
    // Without a format (another kind's texture) the error for a surface that cannot be made ends the game.
    Surface* holder = pixelFormat.bitsPerPixel ? NewDecodedTextureHolder(device, newWidth, newHeight, pixelFormat.bitsPerPixel) : nullptr;
    if (!holder) ThrowSurfaceNotMade(0, 0);
    const render::BlankAlpha alpha = !pixelFormat.alphaMask           ? render::BlankAlpha::None
                                     : pixelFormat.alphaMask == 0x8000 ? render::BlankAlpha::OneBit
                                                                  : render::BlankAlpha::Full;
    render::BlankTexture(holder, newWidth, newHeight, alpha, mipmapped);
    surface.Assign(holder);
}

int Texture::CreateBlank(TextureDevice* device, const MmpHeader* header) {
    // A square of header->width (the height is not read). The atlases keep one level, as the original's every blank
    // texture (a level below would mix neighbouring cells); the others get their levels made after each draw.
    const auto size = static_cast<int>(header->width);
    TakeBlankHolder(device, size, size, format != kAtlasFormat);
    // The original meant bits * width * height / 8 but took the bits from a field its channel split never writes.
    uploadResult = 0;
    return 1;
}

Texture* TextureManager::NewTexture(int width, int height, TextureKind kind) {
    Texture* texture = NewTextureOfType(static_cast<int>(kind));
    MmpHeader header;
    std::memset(&header, 0, sizeof(header));
    texture->width = static_cast<std::uint32_t>(width);
    texture->height = static_cast<std::uint32_t>(height);
    if (const BlankFormat* f = FormatOfKind(kind)) {
        texture->format = f->id;
        header.alphaMask = f->alpha.mask;
        header.alphaShift = f->alpha.shift;
        header.alphaKind = f->alpha.bits;
        header.redMask = f->red.mask;
        header.redShift = f->red.shift;
        header.redBits = f->red.bits;
        header.greenMask = f->green.mask;
        header.greenShift = f->green.shift;
        header.greenBits = f->green.bits;
        header.blueMask = f->blue.mask;
        header.blueShift = f->blue.shift;
        header.blueBits = f->blue.bits;
        header.bitsPerPixel = 16;
        header.format = f->id;
    }
    header.magic = kMmpMagic;
    header.width = static_cast<std::uint32_t>(width);
    header.height = static_cast<std::uint32_t>(height);
    header.mipCount = 1;
    if (!texture->CreateBlank(device, &header)) {
        texture->Release();
        return nullptr;
    }
    return texture;
}

// --- .mmp textures -----------------------------------------------------------------------------------

// .mmp textures: a texture's holder for the levels the new renderer decodes from the file data (keyed by the holder,
// render::TextureFromMmp; the holder has no memory: NewDecodedTextureHolder, surface.cpp), the mip offsets and the
// file's extra block. The byte size the texture cache counts is what the original's fill returned
// (MmpHeader::FilledBytes). A texture the new renderer does not decode (a format it does not know) keeps its holder
// and shows nothing. See docs/notes/renderer.md (section 7).

namespace {

constexpr std::uint32_t kDxtTag = 0x545844;          // "DXT" (the format's low three bytes)

bool g_undecodedLogged = false;

// A holder with no DirectDraw texture for the levels the new renderer decodes from the file (keyed
// by the holder). One it does not decode keeps the holder and shows nothing (logged once). Its
// allocation failing ends the game, like a surface that could not be made.
Surface* DecodedHolder(TextureDevice* device, const Texture* texture, const MmpHeader* data, unsigned mip,
                       unsigned levels) {
    Surface* holder = NewDecodedTextureHolder(device, static_cast<int>(texture->width), static_cast<int>(texture->height),
                                              TextureFormat(texture->format).bitsPerPixel);
    if (!holder) ThrowSurfaceNotMade(0, 0);
    if (!render::TextureFromMmp(holder, data, mip, levels) && !g_undecodedLogged) {
        g_undecodedLogged = true;
        ei::Log("cpuwindows: an .mmp texture the new renderer does not decode (format %08X) shows nothing",
                data->format);
    }
    return holder;
}

} // namespace

void Texture::MarkLoaded(const MmpHeader* header) {
    flags &= ~TextureFlag::AlphaBits;
    switch (header->alphaKind) {
    case 0: flags |= TextureFlag::Alpha0; break;
    case 1: flags |= TextureFlag::Alpha1; break;
    case 4: flags |= TextureFlag::Alpha4; break;
    case 8: flags |= TextureFlag::Alpha8; break;
    default: break;
    }
    flags |= TextureFlag::Loaded;
}

std::uint32_t MmpHeader::MipOffset(unsigned mip) const {
    if (mip == 0) return extraSize;
    std::uint32_t levelBytes = (bitsPerPixel * height * width) >> 3;
    std::uint32_t scale = 1u << ((2 * mip) & 31);   // 4^mip (SHL masks the count)
    std::uint32_t larger = levelBytes * (scale - 1) * 4 / (scale * 3);
    return larger + extraSize;
}

std::uint32_t MmpHeader::FilledBytes(std::uint32_t heldFormat, unsigned mip, unsigned levels) const {
    std::uint32_t bits = bitsPerPixel;
    if ((format & 0xFFFFFF) == kDxtTag && heldFormat != format) bits = 16;   // decoded into an RGB format
    std::uint32_t w = width >> (mip & 31);
    std::uint32_t h = height >> (mip & 31);
    std::uint32_t total = 0;
    for (unsigned m = mip; m < mipCount && m < mip + levels; ++m) {
        total += (bits * h * w) >> 3;
        w >>= 1;
        h >>= 1;
    }
    return total;
}

int Texture::CreateFromMmp(TextureDevice* device, const MmpHeader* header, unsigned mip) {
    if (flags & TextureFlag::Loaded) return 1;
    header->MipOffset(mip);   // the result is not used

    std::uint32_t mipWidth = header->width >> (mip & 31);
    std::uint32_t mipHeight = header->height >> (mip & 31);
    std::uint32_t levels = header->mipCount - mip;
    if (Manager()->limitTo512) {
        while (mipWidth >= 0x200 || mipHeight >= 0x200) {
            ++mip;
            mipWidth = header->width >> (mip & 31);
            mipHeight = header->height >> (mip & 31);
        }
        levels = header->mipCount - mip;
    }
    if (!options::g_options.mipmapping) levels = 1;
    height = mipHeight;
    width = mipWidth;

    // The new renderer decodes the levels from the file and the holder has no DirectDraw texture
    // (a system-memory staging surface filled from the file, loaded into a managed
    // texture with the kind's priority hints, its priority set to 100, is not made).
    Surface* decoded = DecodedHolder(device, this, header, mip, levels);
    surface.Assign(decoded);
    uploadResult = static_cast<int>(header->FilledBytes(format, mip, levels));
    MarkLoaded(header);
    extra.resize(static_cast<int>(header->extraSize));
    std::memcpy(extra.data(), header + 1, header->extraSize);
    return 1;
}

int TextureManager::LoadMmpExtra(const char* name, std::vector<std::uint8_t>* out) {
    if (const ComposedTexture* composed = FindComposed(name)) {
        *out = composed->extra;
        return 1;
    }
    std::string path = std::format("{}.mmp", name);
    auto* fileHeader = static_cast<MmpHeader*>(GetFile(path, nullptr));
    if (!fileHeader) {
        out->clear();
        return 0;
    }
    out->resize(static_cast<int>(fileHeader->extraSize));
    if (fileHeader->extraSize) std::memcpy(out->data(), fileHeader + 1, fileHeader->extraSize);
    CloseFile(fileHeader);
    return 1;
}

// --- Loose .bmp files --------------------------------------------------------------------------------

// A texture from loose .bmp files (from Texture::LoadBmp for a
// textures\<name>.bmp in the game folder, which overrides the archived .mmp: TextureManager::
// LoadTexture). The .bmp's 24-bit colours and, when there is an 8-bit a_<name>.bmp beside it, its
// grey levels as alpha are drawn into a texture on the GPU (Texture::TakeBlankHolder,
// render::DrawIntoTexture), which keeps what the texture's format keeps of the alpha. See
// docs/notes/renderer.md (section 7).

namespace {

const char* CantLoadText() { return "CTexture::FillSurface.\n Can't Load texture %s.\n"; }        // "CTexture::FillSurface.\n Can't Load texture %s.\n"
const char* AlphaNot8BitText() { return "Alpha-file %s must be 8bit gray instead of %d"; }

// The .bmp's pixels as 0xAARRGGBB, `width` x `height`: `width` rows of `height` pixels read (the original's loop),
// the colours read on as B, G, R bytes without the .bmp's row padding (and in its row order: a bottom-up .bmp comes out
// upside down), the alpha the alpha .bmp's byte at the pixel's index (when given, else 255). Pixels from row `height` /
// column `width` on are read but not kept.
std::vector<std::uint32_t> ReadPixels(int width, int height, const std::uint8_t* colour, const std::uint8_t* alpha) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * static_cast<std::size_t>(height));
    int index = 0;   // the colour byte read next
    for (int y = 0; y < width; ++y) {
        for (int x = 0; x < height; ++x) {
            std::uint32_t a = alpha ? alpha[index / 3] : 0xFF;
            std::uint32_t argb = (a << 24) | (static_cast<std::uint32_t>(colour[index + 2]) << 16) |
                                 (static_cast<std::uint32_t>(colour[index + 1]) << 8) | colour[index];
            index += 3;
            if (y < height && x < width) out[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + x] = argb;
        }
    }
    return out;
}

} // namespace

// Details kept: the loop runs over the width, then the height (a non-square .bmp overran
// the surface's rows; here the rows and pixels past it are left out); a failed open of the alpha
// file is silent. No texture format: the error for a surface that cannot be made (the staging
// surface's: sizes zero) ends the game.
int Texture::LoadBmpFiles(TextureDevice* device, const char* path, const char* alphaPath) {
    if (flags & TextureFlag::Loaded) return 1;
    platform::File file;
    platform::File alphaFile;
    Dib colour;
    Dib alpha;
    if (!file.Open(path, platform::FileMode::Read)) {
        game::DebugLog(CantLoadText(), GetName());
        return 0;
    }
    colour.Read(&file);
    file.Close();
    Size size;
    colour.Size(&size);
    const int bmpWidth = size.cx, bmpHeight = size.cy;
    bool withAlpha = false;
    if (alphaFile.Open(alphaPath, platform::FileMode::Read)) {
        alpha.Read(&alphaFile);
        alphaFile.Close();
        std::uint16_t bits = alpha.header->bitCount;
        if (bits == 8)
            withAlpha = true;
        else
            game::DebugLog(AlphaNot8BitText(), alphaPath, static_cast<unsigned>(bits));
    }
    height = static_cast<std::uint32_t>(bmpHeight);
    width = static_cast<std::uint32_t>(bmpWidth);
    const std::uint8_t* alphaBits = withAlpha ? alpha.bits.data() : nullptr;

    // Drawn into the texture on the GPU.
    TakeBlankHolder(device, bmpWidth, bmpHeight, true);
    const std::vector<std::uint32_t> pixels = ReadPixels(bmpWidth, bmpHeight, colour.bits.data(), alphaBits);
    const render::TextureImage image{nullptr, pixels.data(), static_cast<std::uint32_t>(pixels.size() * 4), bmpWidth, bmpHeight,
                                     render::ImageLayout::Argb};
    render::DrawIntoTexture(surface, &image, 0, 0, bmpWidth, bmpHeight);
    flags |= TextureFlag::Loaded;
    return 1;
}

// --- Loading a .bmp texture with its alpha file ------------------------------------------------------

// Loading a .bmp texture with its alpha file. (The fill of a texture's system-memory
// surface from .mmp data went with
// the DirectDraw textures: the new renderer decodes the .mmp file, texture_load.cpp.)

namespace {

const char* AlreadyLoadedText() { return "CTexture::Load.\n Texture \"%s\" already loaded.\n"; }     // "CTexture::Load.\n Texture \"%s\" already loaded.\n"
const char* AlphaPrefixText() { return "a_"; }

} // namespace

// The path is split at `\` (each piece trimmed), the alpha file is "a_" + the file name in the same
// directory; both are joined again.
int Texture::LoadBmp(TextureDevice* device, const char* path) {
    std::string file;
    std::string alphaPath;
    std::vector<std::string> pieces;
    if ((flags & TextureFlag::Loaded) &&
        str::Compare(name, path) == 0) {
        game::DebugLog(AlreadyLoadedText(), path);
        return 1;
    }
    pieces = text::Split(path, '\\');
    file = pieces.back();
    pieces.pop_back();
    alphaPath = AlphaPrefixText() + file;
    pieces.push_back(alphaPath);
    alphaPath = text::Join(pieces, '\\');
    pieces.pop_back();
    pieces.push_back(file);
    file = text::Join(pieces, '\\');
    return LoadBmpFiles(device, file.c_str(), alphaPath.c_str());
}

// --- Composed textures -------------------------------------------------------------------------------

// The texture manager's composed textures: a party unit's skin is made of layers (the skin and the armour / weapon
// textures) and its wounds texture of the wounded parts' overlays, drawn over each other into one square texture on
// the GPU (render::ComposeTexture). The texture manager keeps what each composed texture is made of under its name
// (the original drew it on the CPU and cached it as <data path>textures\<name>.mmp): its header, the request's layer
// bytes (the file's extra block, so an unchanged request reuses it) and the layer textures. Callers:
// UnitClientSpecific::ComposeSkin, ComposeWoundsTexture, BuildUnitSkins. See docs/notes/items_spells.md ("Composed
// textures") and docs/notes/renderer.md (section 7).

namespace {

const char* const kCantFindLayer = "Can't find texture \"%s\" for layer composition.\n";
const char* const kWrongLayerSize = "Texture \"%s\" dimentions are incorrect.\n";

// The composed textures by name, lower case (the original's files, whose names Windows compares without case).
std::map<std::string, ComposedTexture> g_composed;

std::string Key(std::string_view name) {
    std::string key(name);
    for (char& c : key) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return key;
}

// Whether the composed texture was made from the request's layer bytes.
bool SameRequest(const ComposedTexture& composed, const SkinRequest* request) {
    return composed.extra == request->layers;
}

// The header of a composed texture in `format` (0x4444 or 0x5551, what the callers ask for).
void SetPixelFormat(MmpHeader& header, std::uint32_t format) {
    if (format == 0x4444) header.SetFormat4444();
    else if (format == 0x5551) header.SetFormat5551();
    else header.SetNoFormat();
}

} // namespace

void ForgetComposedTextures() { g_composed.clear(); }

const ComposedTexture* TextureManager::FindComposed(std::string_view name) {
    auto found = g_composed.find(Key(name));
    return found == g_composed.end() ? nullptr : &found->second;
}

int TextureManager::FindLayers(const std::vector<std::string>* names, int width, std::vector<std::string>* found) {
    int count = 0;
    for (const std::string& name : *names) {
        MmpHeader* data = GetFileData(name);
        if (!data) {
            game::DebugLog(kCantFindLayer, name);
            continue;
        }
        const bool fits = data->width == static_cast<std::uint32_t>(width) && data->height == static_cast<std::uint32_t>(width);
        CloseFile(data);
        if (!fits) return game::FatalError(kWrongLayerSize, name);
        found->push_back(name);
        ++count;
    }
    return count;
}

// `out` receives the name of the texture used; `name` is the composed texture to use, `fallback` another one tried
// when `name` has none. One made from the request's layer bytes is reused (1). Otherwise, without layer textures,
// the composed texture is forgotten (0); else it is made anew (`request->flags` texels square, `mipCount` levels in
// `format`) from the layer textures found (1; 0 when none is).
int TextureManager::Compose(std::string* out, const SkinRequest* request, const std::string* name,
                             const std::string* fallback, unsigned mipCount, std::uint32_t format) {
    std::string key = *name;
    *out = *name;
    if (const ComposedTexture* composed = FindComposed(*name)) {
        if (SameRequest(*composed, request)) return 1;
    } else if (*name != *fallback) {
        *out = *fallback;
        key = *fallback;
        if (const ComposedTexture* other = FindComposed(*fallback)) {
            if (SameRequest(*other, request)) return 1;
            *out = *name;
            key = *name;
        }
    }

    if (request->textures.empty()) {
        g_composed.erase(Key(key));
        return 0;
    }
    ComposedTexture composed{};
    if (FindLayers(&request->textures, request->flags, &composed.layers) == 0) return 0;

    MmpHeader& header = composed.header;
    std::memset(&header, 0, sizeof(header));
    header.magic = kMmpMagic;
    SetPixelFormat(header, format);
    header.width = static_cast<std::uint32_t>(request->flags);
    header.height = static_cast<std::uint32_t>(request->flags);
    header.mipCount = mipCount;
    header.extraSize = static_cast<std::uint32_t>(request->layers.size());
    composed.extra = request->layers;
    g_composed[Key(key)] = std::move(composed);
    return 1;
}

int Texture::CreateComposed(TextureDevice* device, const ComposedTexture& composed, unsigned mip) {
    if (flags & TextureFlag::Loaded) return 1;
    const MmpHeader& header = composed.header;
    unsigned levels = header.mipCount - mip;
    if (!options::g_options.mipmapping) levels = 1;
    width = header.width >> (mip & 31);
    height = header.height >> (mip & 31);
    Surface* holder = NewDecodedTextureHolder(device, static_cast<int>(width), static_cast<int>(height),
                                              TextureFormat(format).bitsPerPixel);
    if (!holder) ThrowSurfaceNotMade(0, 0);
    // The layers' pixels from the archives, drawn over each other by the GPU.
    TextureManager* manager = Manager();
    std::vector<render::TextureImage> images;
    std::vector<void*> files;
    for (const std::string& layer : composed.layers) {
        std::uint32_t size = 0;
        auto* data = static_cast<MmpHeader*>(manager->GetFile(layer, &size));
        if (!data) continue;
        files.push_back(data);
        const std::uint32_t skipped = static_cast<std::uint32_t>(sizeof(MmpHeader)) + data->extraSize;
        const render::ImageLayout layout = data->format == kMmpFormatPnt3 ? render::ImageLayout::Pnt3 : render::ImageLayout::Argb;
        images.push_back(render::TextureImage{layer.c_str(), reinterpret_cast<const std::uint8_t*>(data) + skipped,
                                              size > skipped ? size - skipped : 0, static_cast<int>(data->width),
                                              static_cast<int>(data->height), layout});
    }
    render::ComposeTexture(holder, static_cast<int>(header.width), mip, levels, images.data(),
                           static_cast<int>(images.size()));
    for (void* file : files) manager->CloseFile(file);
    surface.Assign(holder);
    uploadResult = static_cast<int>(header.FilledBytes(format, mip, levels));
    MarkLoaded(&header);
    extra = composed.extra;
    return 1;
}
