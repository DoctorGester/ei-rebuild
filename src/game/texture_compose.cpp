// The texture manager's composed textures: a party unit's skin is made of layers (the skin and the armour / weapon
// textures) and its wounds texture of the wounded parts' overlays, drawn over each other into one square texture on
// the GPU (render::ComposeTexture). The texture manager keeps what each composed texture is made of under its name
// (the original drew it on the CPU and cached it as <data path>textures\<name>.mmp): its header, the request's layer
// bytes (the file's extra block, so an unchanged request reuses it) and the layer textures. Callers:
// CUnitClientSpecific::ComposeSkin, ComposeWoundsTexture, BuildUnitSkins. See docs/notes/items_spells.md ("Composed
// textures") and docs/notes/renderer.md (section 7).
#include "texture.h"
#include "unit_client_models.h"

#include "options.h"
#include "render/render.h"
#include "surface.h"

#include <cctype>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace {

constexpr std::uint32_t kMmpMagic = 0x504D4D;       // "MMP"

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

const ComposedTexture* CTextureManager::FindComposed(std::string_view name) {
    auto found = g_composed.find(Key(name));
    return found == g_composed.end() ? nullptr : &found->second;
}

int CTextureManager::FindLayers(const std::vector<std::string>* names, int width, std::vector<std::string>* found) {
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
int CTextureManager::Compose(std::string* out, const SkinRequest* request, const std::string* name,
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

int CTexture::CreateComposed(CTextureDevice* device, const ComposedTexture& composed, unsigned mip) {
    if (flags_ & kTextureLoaded) return 1;
    const MmpHeader& header = composed.header;
    unsigned levels = header.mipCount - mip;
    if (!options::g_options->mipmapping_) levels = 1;
    width_ = header.width >> (mip & 31);
    height_ = header.height >> (mip & 31);
    Surface* holder = NewDecodedTextureHolder(device, static_cast<int>(width_), static_cast<int>(height_),
                                              TextureFormat(format_).bitsPerPixel);
    if (!holder) ThrowSurfaceNotMade(0, 0);
    // The layers' pixels from the archives, drawn over each other by the GPU.
    CTextureManager* manager = Manager();
    std::vector<render::TextureImage> images;
    std::vector<void*> files;
    for (const std::string& name : composed.layers) {
        std::uint32_t size = 0;
        auto* data = static_cast<MmpHeader*>(manager->GetFile(name, &size));
        if (!data) continue;
        files.push_back(data);
        const std::uint32_t skipped = static_cast<std::uint32_t>(sizeof(MmpHeader)) + data->extraSize;
        const render::ImageLayout layout = data->format == kMmpFormatPnt3 ? render::ImageLayout::Pnt3 : render::ImageLayout::Argb;
        images.push_back(render::TextureImage{name.c_str(), reinterpret_cast<const std::uint8_t*>(data) + skipped,
                                              size > skipped ? size - skipped : 0, static_cast<int>(data->width),
                                              static_cast<int>(data->height), layout});
    }
    render::ComposeTexture(holder, static_cast<int>(header.width), mip, levels, images.data(),
                           static_cast<int>(images.size()));
    for (void* file : files) manager->CloseFile(file);
    surface_.Assign(holder);
    uploadResult_ = static_cast<int>(header.FilledBytes(format_, mip, levels));
    MarkLoaded(&header);
    extra_ = composed.extra;
    return 1;
}
