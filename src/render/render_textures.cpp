// Game textures in the new renderer, keyed by their surface holder: .mmp textures from the file's levels (DXT1-5
// uploaded as their blocks; RGB of any mask layout decoded; the file's levels only) and textures drawn on the GPU
// (render_compose.cpp), kept as sokol images until the holder goes, with what they were made from for a new
// device. Also the cursor pictures' pixels as RGBA8 (render::PictureFromMmp).
#include "render_internal.h"
#include "render_ui.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>

#include "../game/texture.h"

#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>

namespace render::detail {

namespace {

// What a texture was made from: the .mmp file's levels as the file has them, or the images it was composed of.
// A new Direct3D 11 device gets the texture made again from it.
struct TextureSource {
    enum Kind : std::uint8_t { None, Mmp, Composed } kind = None;
    std::vector<std::uint8_t> header;   // the file's header
    std::vector<std::uint8_t> bytes;    // the file's bytes from `firstOffset` on
    std::uint32_t firstOffset = 0;      // where `bytes` start after the header
    unsigned mip = 0, levels = 0;       // the levels decoded
    std::shared_ptr<ComposeRecipe> recipe;   // Composed
};

struct Texture {
    sg_image image{};
    sg_view view{};
    std::uint32_t checkedFrame = 0xFFFFFFFFu;
    int width = 0, height = 0;
    TextureSource source;            // what it was made from, to make it again on a new device
    std::size_t bytes = 0;           // .mmp textures: the image's bytes on the GPU
};

std::unordered_map<const void*, Texture> g_textures;   // by surface holder: textures (the only copy)
std::vector<sg_view> g_retiredViews;                   // replaced during the frame: destroyed at Present
std::vector<sg_image> g_retiredImages;
std::unordered_map<std::uint32_t, sg_sampler> g_samplers;
Texture g_white;
sg_sampler g_linear{};
int g_failures = 0;

bool Empty(const Rect& r) { return r.right <= r.left || r.bottom <= r.top; }


// An image of `format` from `levels` (level 0 first): the levels given only, as the .mmp file has them and the
// original's texture had them (RGBA8 or the file's DXT blocks).
sg_image MakeImage(sg_pixel_format format, int width, int height, std::vector<std::vector<std::uint8_t>> levels,
                   const char* label, FrameStats::Upload kind, std::size_t* madeBytes = nullptr) {
    sg_image_desc desc{};
    desc.width = width;
    desc.height = height;
    desc.num_mipmaps = static_cast<int>(levels.size());
    desc.pixel_format = format;
    std::size_t bytes = 0;
    for (std::size_t i = 0; i < levels.size(); ++i) {
        desc.data.mip_levels[i] = sg_range{levels[i].data(), levels[i].size()};
        bytes += levels[i].size();
    }
    desc.label = label;
    CountImage(ImageUse::Texture, true);
    CountUpload(kind, bytes);
    if (madeBytes) *madeBytes = bytes;
    return sg_make_image(&desc);
}

// A texture view of `image`; none when the image could not be made (sokol's validation would dereference it).
sg_view MakeTextureView(sg_image image) {
    if (!image.id || sg_query_image_state(image) != SG_RESOURCESTATE_VALID) {
        if (g_failures++ < 10) ei::Log("render: a texture image could not be made (pool full or bad data); drawn white");
        return sg_view{};
    }
    sg_view_desc v{};
    v.texture.image = image;
    return sg_make_view(&v);
}

const Texture& White() {
    if (!g_white.image.id) {
        g_white.image = MakeImage(SG_PIXELFORMAT_RGBA8, 1, 1, {std::vector<std::uint8_t>(4, 255)}, "ei-white", FrameStats::Small);
        g_white.view = MakeTextureView(g_white.image);
    }
    return g_white;
}

void Retire(Texture& t) {
    if (t.image.id) {
        g_retiredViews.push_back(t.view);
        g_retiredImages.push_back(t.image);
    }
    t.image = sg_image{};
    t.view = sg_view{};
}

// The levels of a texture of `width` x `height` down to 1 x 1.
int LevelCount(int width, int height) {
    int levels = 1;
    while ((width > 1 || height > 1) && levels < SG_MAX_MIPMAPS) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

void Remake(Texture& t);

struct Patch {
    const void* source = nullptr;
    int to[4] = {};
    int from[4] = {};
};
std::unordered_map<const void*, Patch> g_patches;
int g_patchFailures = 0;

bool HolderSize(const void* holder, int* width, int* height) {
    auto decoded = g_textures.find(holder);
    if (decoded == g_textures.end() || decoded->second.width <= 0) return false;
    *width = decoded->second.width;
    *height = decoded->second.height;
    return true;
}

bool Inside(const int r[4], int width, int height) {
    return r[0] >= 0 && r[1] >= 0 && r[0] < r[2] && r[1] < r[3] && r[2] <= width && r[3] <= height;
}

// The bytes level `mip` takes in the file as the game's fill copies it: bits per pixel x width x height / 8
// of the level's size (halved per level, down to 0), no whole DXT blocks for the levels under 4 x 4.
std::uint32_t MmpFileLevelBytes(const MmpHeader& h, unsigned mip) {
    return (h.bitsPerPixel * (h.width >> (mip & 31)) * (h.height >> (mip & 31))) >> 3;
}

// The block-compressed format an .mmp "DXTn" file's blocks are uploaded in (DXT2 / DXT4, the premultiplied
// variants, as DXT3 / DXT5); NONE for the RGB formats.
sg_pixel_format BlockFormat(std::uint32_t format) {
    if ((format & 0xFFFFFF) != 0x545844) return SG_PIXELFORMAT_NONE;   // "DXT"
    switch (static_cast<char>(format >> 24)) {
    case '1': return SG_PIXELFORMAT_BC1_RGBA;
    case '2':
    case '3': return SG_PIXELFORMAT_BC2_RGBA;
    case '4':
    case '5': return SG_PIXELFORMAT_BC3_RGBA;
    default: return SG_PIXELFORMAT_NONE;
    }
}

// One level of an RGB .mmp file as RGBA8.
bool DecodeMmpLevel(const MmpHeader& h, const std::uint8_t* pixels, int width, int height, std::vector<std::uint8_t>& out) {
    int bytes = static_cast<int>(h.bitsPerPixel / 8);
    // RGB with its masks in the header; "PNT2" / "PNT3" (item pictures, a packed format the game
    // decodes itself) and anything else are not decoded.
    if ((bytes != 2 && bytes != 4) || !h.redMask || !h.greenMask || !h.blueMask) return false;
    const Channel r(h.redMask), g(h.greenMask), b(h.blueMask), a(h.alphaMask);
    out.resize(static_cast<std::size_t>(width) * height * 4);
    for (int i = 0; i < width * height; ++i) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel, pixels + static_cast<std::size_t>(i) * bytes, static_cast<std::size_t>(bytes));
        std::uint8_t* o = &out[static_cast<std::size_t>(i) * 4];
        o[0] = static_cast<std::uint8_t>(r.Widen(pixel));
        o[1] = static_cast<std::uint8_t>(g.Widen(pixel));
        o[2] = static_cast<std::uint8_t>(b.Widen(pixel));
        o[3] = static_cast<std::uint8_t>(a.bits ? a.Widen(pixel) : 255);
    }
    return true;
}

std::uint32_t MmpLevelBytes(const MmpHeader& h, int width, int height) {
    if (BlockFormat(h.format) != SG_PIXELFORMAT_NONE) {
        auto blocks = static_cast<std::uint32_t>((width + 3) / 4) * static_cast<std::uint32_t>((height + 3) / 4);
        return blocks * (BlockFormat(h.format) == SG_PIXELFORMAT_BC1_RGBA ? 8u : 16u);
    }
    return static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * (h.bitsPerPixel / 8);
}

// The levels of an .mmp texture as uploaded: a DXT file's blocks as the file has them (`format` BC1 / BC2 / BC3),
// an RGB file's pixels decoded to RGBA8.
struct MmpLevels {
    sg_pixel_format format = SG_PIXELFORMAT_NONE;
    int width = 0, height = 0;   // the first level's
    std::vector<std::vector<std::uint8_t>> levels;
};

// The levels from `mip` on (`levels` of them, at least one). `pixels` holds the file's bytes after the header from
// offset `pixelsOffset` on; the bytes the levels take go to `*first` .. `*end`.
bool ReadMmp(const MmpHeader& h, const std::uint8_t* pixels, std::uint32_t pixelsOffset, unsigned mip, unsigned levels,
             MmpLevels* read, std::uint32_t* first, std::uint32_t* end) {
    *read = MmpLevels{};
    *first = 0xFFFFFFFFu;
    *end = 0;
    const sg_pixel_format blocks = BlockFormat(h.format);
    read->format = blocks != SG_PIXELFORMAT_NONE ? blocks : SG_PIXELFORMAT_RGBA8;
    // The game's offset of the first level (MmpHeader::MipOffset) for the level the texture starts at only: its
    // product overflows from about the sixth level of a 1024 x 1024 texture on, so the later levels step from level
    // to level (MmpFileLevelBytes).
    std::uint32_t offset = h.MipOffset(mip);
    std::vector<std::uint8_t> padded;
    for (unsigned m = mip; m < mip + (levels ? levels : 1) && m < h.mipCount && read->levels.size() < SG_MAX_MIPMAPS; ++m) {
        int w = static_cast<int>(h.width >> (m & 31)), hh = static_cast<int>(h.height >> (m & 31));
        if (w < 1) w = 1;
        if (hh < 1) hh = 1;
        if (offset < pixelsOffset) break;
        std::uint32_t bytes = MmpFileLevelBytes(h, m);
        const std::uint8_t* source = pixels + (offset - pixelsOffset);
        if (bytes < MmpLevelBytes(h, w, hh)) {
            // A level the fill copies only part of (a DXT level under 4 x 4: the start of its block): the rest as zeros.
            padded.assign(MmpLevelBytes(h, w, hh), 0);
            std::memcpy(padded.data(), source, bytes);
            source = padded.data();
        }
        std::vector<std::uint8_t> level;
        if (blocks != SG_PIXELFORMAT_NONE) level.assign(source, source + MmpLevelBytes(h, w, hh));
        else if (!DecodeMmpLevel(h, source, w, hh, level)) break;
        if (m == mip) {
            read->width = w;
            read->height = hh;
        }
        read->levels.push_back(std::move(level));
        if (offset < *first) *first = offset;
        if (offset + bytes > *end) *end = offset + bytes;
        offset += bytes;
    }
    return !read->levels.empty() && read->width > 0 && read->height > 0;
}

int g_mmpFailures = 0;

#if defined(EI_RENDER_VALIDATION)
// The .mmp textures made (render::TextureFromMmp), logged at exit: their images' bytes on the GPU, the most held at
// once, and the CPU time making them (reading the levels, decoding, the upload).
struct MmpImageCounts {
    long made = 0;
    std::uint64_t bytes = 0, mostHeld = 0;
    long long ticks = 0;
    ~MmpImageCounts() {
        ei::Log("render: .mmp textures at exit: %ld made, %llu KB uploaded, at most %llu KB held at once, %.1f ms making them",
                made, static_cast<unsigned long long>(bytes / 1024), static_cast<unsigned long long>(mostHeld / 1024),
                static_cast<double>(ticks) * kMsPerTick);
    }
} g_mmpImageCounts;

void CountMmpImage(const void* holder, long long ticks) {
    std::uint64_t held = 0;
    for (const auto& entry : g_textures)
        if (entry.second.source.kind == TextureSource::Mmp && entry.second.image.id) held += entry.second.bytes;
    auto made = g_textures.find(holder);
    if (made != g_textures.end() && made->second.image.id) g_mmpImageCounts.bytes += made->second.bytes;
    ++g_mmpImageCounts.made;
    g_mmpImageCounts.ticks += ticks;
    if (held > g_mmpImageCounts.mostHeld) g_mmpImageCounts.mostHeld = held;
}
#endif

void Remake(Texture& t) {
    const TextureSource& s = t.source;
    // (A composed texture is drawn again right after the device is made again, RemakeComposed: here a pass may be
    // in progress.)
    if (s.kind != TextureSource::Mmp || s.header.size() != sizeof(MmpHeader)) return;
    MmpHeader h{};
    std::memcpy(&h, s.header.data(), sizeof(h));
    MmpLevels read;
    std::uint32_t first = 0, end = 0;
    if (!ReadMmp(h, s.bytes.data(), s.firstOffset, s.mip, s.levels, &read, &first, &end)) return;
    t.image = MakeImage(read.format, read.width, read.height, std::move(read.levels), "ei-texture-again", FrameStats::Mmp);
    t.view = MakeTextureView(t.image);
    if (!t.view.id) {
        if (t.image.id) sg_destroy_image(t.image);
        t.image = sg_image{};
    }
}

std::unordered_map<const void*, Picture> g_pictures;

// `step` (its rectangle `to` relative to the texture, clipped to it) drawn into the texture BlankTexture made for
// `holder` and kept in its recipe.
void DrawStep(const void* holder, ComposeStep step) {
    auto found = holder ? g_textures.find(holder) : g_textures.end();
    if (found == g_textures.end() || found->second.source.kind != TextureSource::Composed || !found->second.source.recipe) {
        if (g_failures++ < 10) ei::Log("render: a texture drawn into that BlankTexture did not make (holder %p)", holder);
        return;
    }
    Texture& t = found->second;
    ComposeRecipe& recipe = *t.source.recipe;
    const Rect wanted = step.to;
    Rect to = wanted;
    if (to.left < 0) to.left = 0;
    if (to.top < 0) to.top = 0;
    if (to.right > recipe.width) to.right = recipe.width;
    if (to.bottom > recipe.height) to.bottom = recipe.height;
    if (Empty(to)) return;
    step.fromX = to.left - wanted.left;
    step.fromY = to.top - wanted.top;
    step.to = to;
    // Steps this one covers whole are left out of the recipe (an atlas's cells are drawn again and again, the clouds
    // every frame).
    auto& steps = recipe.steps;
    for (auto it = steps.begin(); it != steps.end();)
        it = it->to.left >= to.left && it->to.top >= to.top && it->to.right <= to.right && it->to.bottom <= to.bottom
                 ? steps.erase(it)
                 : std::next(it);
    steps.push_back(std::move(step));
    if (!t.image.id) return;   // (a lost device: drawn with the rest when the texture is made again)
    GpuWorkAlone alone;
    DrawComposed(recipe, t.image, &steps.back(), 1);
}

sg_wrap Wrap(std::uint8_t address) {
    switch (address) {
    case 2: return SG_WRAP_MIRRORED_REPEAT;
    case 3: return SG_WRAP_CLAMP_TO_EDGE;
    case 4: return SG_WRAP_CLAMP_TO_BORDER;
    default: return SG_WRAP_REPEAT;
    }
}

} // namespace

sg_view SurfaceView(const void* holder) {
    StatsTimer timer(&FrameStats::textureMs);
    std::uint32_t frame = FrameNumber();
    auto decoded = holder ? g_textures.find(holder) : g_textures.end();
    if (decoded == g_textures.end()) return White().view;
    Texture& t = decoded->second;
    if (!t.view.id && t.checkedFrame != frame) {
        // Lost with the old device (DropTextures): made again from what it was decoded from, once a frame.
        t.checkedFrame = frame;
        Remake(t);
    }
    if (!t.view.id) return White().view;
    return t.view;
}

// The entry of `holder` from an .mmp file's levels: the texture until the holder goes, `source` kept to make it
// again on a new device.
bool StoreMmp(const void* holder, MmpLevels read, TextureSource source) {
    if (!holder) return false;
    Texture& t = g_textures[holder];
    Retire(t);
    t.image = MakeImage(read.format, read.width, read.height, std::move(read.levels), "ei-mmp-texture", FrameStats::Mmp,
                        &t.bytes);
    t.view = MakeTextureView(t.image);
    if (!t.view.id) {
        if (t.image.id) sg_destroy_image(t.image);
        t.image = sg_image{};
    }
    t.checkedFrame = FrameNumber();
    t.width = read.width;
    t.height = read.height;
    t.source = std::move(source);
    return t.view.id != 0;
}

sg_view WhiteView() { return White().view; }

// The entry of `holder` composed on the GPU from `recipe` (render::ComposeTexture, BlankTexture): the recipe kept to
// draw it again on a new device.
bool StoreComposed(const void* holder, std::shared_ptr<ComposeRecipe> recipe) {
    if (!holder) return false;
    Texture& t = g_textures[holder];
    Retire(t);
    {
        GpuWorkAlone alone;
        MakeComposed(*recipe, &t.image, &t.view);
    }
    t.checkedFrame = FrameNumber();
    t.width = recipe->width >> recipe->firstLevel;
    t.height = recipe->height >> recipe->firstLevel;
    t.source = TextureSource{};
    t.source.kind = TextureSource::Composed;
    t.source.recipe = std::move(recipe);
    return t.view.id != 0;
}

void RemakeComposed() {
    for (auto& entry : g_textures) {
        Texture& t = entry.second;
        if (t.source.kind != TextureSource::Composed || !t.source.recipe || t.view.id) continue;
        MakeComposed(*t.source.recipe, &t.image, &t.view);
    }
}

StagePatch PatchOf(const void* holder) {
    StagePatch p;
    if (g_patches.empty() || !holder) return p;
    auto it = g_patches.find(holder);
    if (it == g_patches.end()) return p;
    const Patch& patch = it->second;
    int tw = 0, th = 0, sw = 0, sh = 0;
    if (!HolderSize(holder, &tw, &th) || !HolderSize(patch.source, &sw, &sh)) return p;
    // Texture coordinates: the target's rectangle; a texel of it at (x, y) takes the source's texel
    // at (x - to + from) scaled by the rectangles' sizes (the same size from the blink).
    float kx = static_cast<float>(patch.from[2] - patch.from[0]) / static_cast<float>(patch.to[2] - patch.to[0]);
    float ky = static_cast<float>(patch.from[3] - patch.from[1]) / static_cast<float>(patch.to[3] - patch.to[1]);
    p.to[0] = static_cast<float>(patch.to[0]) / static_cast<float>(tw);
    p.to[1] = static_cast<float>(patch.to[1]) / static_cast<float>(th);
    p.to[2] = static_cast<float>(patch.to[2]) / static_cast<float>(tw);
    p.to[3] = static_cast<float>(patch.to[3]) / static_cast<float>(th);
    p.map[0] = kx * tw / sw;
    p.map[1] = ky * th / sh;
    p.map[2] = (patch.from[0] - kx * patch.to[0]) / sw;
    p.map[3] = (patch.from[1] - ky * patch.to[1]) / sh;
    // Half a texel in from the source rectangle's edges: no texel next to it filtered in (the face's
    // open and closed rectangles touch).
    p.inside[0] = (patch.from[0] + 0.5f) / sw;
    p.inside[1] = (patch.from[1] + 0.5f) / sh;
    p.inside[2] = (patch.from[2] - 0.5f) / sw;
    p.inside[3] = (patch.from[3] - 0.5f) / sh;
    p.texture = SurfaceView(patch.source);
    return p;
}

bool TextureFromMmpData(const void* holder, const void* data, unsigned mip, unsigned levels) {
    if (!holder || !data) return false;
    const auto& h = *static_cast<const MmpHeader*>(data);
    const auto* base = static_cast<const std::uint8_t*>(data) + sizeof(MmpHeader);
    if (h.width == 0 || h.height == 0 || h.width > 8192 || h.height > 8192 || h.mipCount == 0 || h.mipCount > 16) {
        if (g_mmpFailures++ < 10) ei::Log("render: .mmp header not understood; the texture is not decoded");
        return false;
    }
#if defined(EI_RENDER_VALIDATION)
    const long long started = Ticks();
#endif
    MmpLevels read;
    std::uint32_t first = 0, end = 0;
    if (!ReadMmp(h, base, 0, mip, levels, &read, &first, &end)) {
        if (g_mmpFailures++ < 10)
            ei::Log("render: .mmp format %08X (%u bits) not decoded",
                    static_cast<unsigned>(h.format), static_cast<unsigned>(h.bitsPerPixel));
        return false;
    }
    TextureSource source;
    source.kind = TextureSource::Mmp;
    source.header.assign(static_cast<const std::uint8_t*>(data), base);
    source.bytes.assign(base + first, base + end);
    source.firstOffset = first;
    source.mip = mip;
    source.levels = levels;
    bool made = StoreMmp(holder, std::move(read), std::move(source));
#if defined(EI_RENDER_VALIDATION)
    CountMmpImage(holder, Ticks() - started);
#endif
    return made;
}

bool PictureFromMmpData(const void* holder, const void* data) {
    g_pictures.erase(holder);
    const auto& h = *static_cast<const MmpHeader*>(data);
    if (h.width == 0 || h.height == 0 || h.width > 4096 || h.height > 4096) return false;
    Picture p;
    p.width = static_cast<int>(h.width);
    p.height = static_cast<int>(h.height);
    const auto* base = static_cast<const std::uint8_t*>(data) + sizeof(MmpHeader);
    if (!DecodeMmpLevel(h, base + h.MipOffset(0), p.width, p.height, p.rgba)) {
        if (g_mmpFailures++ < 10)
            ei::Log("render: picture format %08X not decoded; the cursor draws nothing for it", static_cast<unsigned>(h.format));
        return false;
    }
    g_pictures.emplace(holder, std::move(p));
    return true;
}

const Picture* PictureOf(const void* holder) {
    auto it = g_pictures.find(holder);
    return it == g_pictures.end() ? nullptr : &it->second;
}

sg_view TextureView(const void* texture) {
    if (!texture) return White().view;
    return SurfaceView(static_cast<const ::Texture*>(texture)->surface);
}

sg_sampler LinearSampler() {
    if (!g_linear.id) {
        sg_sampler_desc d{};
        d.min_filter = SG_FILTER_LINEAR;
        d.mag_filter = SG_FILTER_LINEAR;
        d.mipmap_filter = SG_FILTER_NEAREST;   // the game's D3DTFP_POINT
        d.wrap_u = SG_WRAP_REPEAT;
        d.wrap_v = SG_WRAP_REPEAT;
        d.label = "ei-linear";
        g_linear = sg_make_sampler(&d);
    }
    return g_linear;
}

sg_sampler StageSampler(const StageState& s) {
    std::uint32_t key = static_cast<std::uint32_t>(s.magFilter & 7) | (static_cast<std::uint32_t>(s.minFilter & 7) << 3) |
                        (static_cast<std::uint32_t>(s.mipFilter & 7) << 6) | (static_cast<std::uint32_t>(s.addressU & 7) << 9) |
                        (static_cast<std::uint32_t>(s.addressV & 7) << 12);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end()) return it->second;
    sg_sampler_desc d{};
    d.mag_filter = s.magFilter == 1 ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;   // D3DTFG_POINT
    d.min_filter = s.minFilter == 1 ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;   // D3DTFN_POINT
    d.mipmap_filter = s.mipFilter == 3 ? SG_FILTER_LINEAR : SG_FILTER_NEAREST;
    if (s.mipFilter <= 1) d.max_lod = 0.0f;   // D3DTFP_NONE: level 0 only
    d.wrap_u = Wrap(s.addressU);
    d.wrap_v = Wrap(s.addressV);
    d.border_color = SG_BORDERCOLOR_TRANSPARENT_BLACK;
    d.label = "ei-stage-sampler";
    sg_sampler sampler = sg_make_sampler(&d);
    g_samplers.emplace(key, sampler);
    return sampler;
}

sg_sampler AnisotropicSampler(const StageState& s, std::uint32_t level) {
    // Point sampling asked for (the options' Filtering 0): as the stage says.
    if (s.minFilter == 1 || s.magFilter == 1) return StageSampler(s);
    std::uint32_t key = 0x80000000u | (s.mipFilter <= 1 ? 1u : 0u) | (static_cast<std::uint32_t>(s.addressU & 7) << 9) |
                        (static_cast<std::uint32_t>(s.addressV & 7) << 12) | ((level & 31u) << 16);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end()) return it->second;
    // Anisotropic with linear mips: the level of detail moves smoothly across the tiles, whose texture
    // coordinates change direction and scale from tile to tile (D3DTFP_POINT's level steps showed as seams;
    // renderer.md section 11). The render quality's anisotropy samples the world's models so too.
    sg_sampler_desc d{};
    d.min_filter = SG_FILTER_LINEAR;
    d.mag_filter = SG_FILTER_LINEAR;
    d.mipmap_filter = SG_FILTER_LINEAR;
    d.max_anisotropy = level;
    if (s.mipFilter <= 1) d.max_lod = 0.0f;   // D3DTFP_NONE (the options' Mipmapping 0): level 0 only
    d.wrap_u = Wrap(s.addressU);
    d.wrap_v = Wrap(s.addressV);
    d.border_color = SG_BORDERCOLOR_TRANSPARENT_BLACK;
    d.label = "ei-anisotropic-sampler";
    sg_sampler sampler = sg_make_sampler(&d);
    ei::Log("render: anisotropic sampler: %u with linear mips%s (the stage's filters %u / %u / %u)", level,
            s.mipFilter <= 1 ? ", level 0 only" : "", s.magFilter, s.minFilter, s.mipFilter);
    g_samplers.emplace(key, sampler);
    return sampler;
}

void RetireTextures() {
    for (sg_view v : g_retiredViews) sg_destroy_view(v);
    for (sg_image i : g_retiredImages) sg_destroy_image(i);
    g_retiredViews.clear();
    g_retiredImages.clear();
}

void ForgetHolder(const void* holder) {
    auto it = g_textures.find(holder);
    if (it == g_textures.end()) return;
    Retire(it->second);
    g_textures.erase(it);
}

void DestroyTextures() {
    RetireTextures();
    for (auto& entry : g_textures) {
        if (entry.second.image.id) {
            sg_destroy_view(entry.second.view);
            sg_destroy_image(entry.second.image);
        }
    }
    g_textures.clear();
    if (g_white.image.id) {
        sg_destroy_view(g_white.view);
        sg_destroy_image(g_white.image);
    }
    g_white = Texture{};
    if (g_linear.id) sg_destroy_sampler(g_linear);
    g_linear = sg_sampler{};
    for (auto& s : g_samplers) sg_destroy_sampler(s.second);
    g_samplers.clear();
}

void DropTextures(std::size_t* kept, std::size_t* keptBytes) {
    *kept = *keptBytes = 0;
    // The textures stay without their images (made again from their sources at their next use).
    std::unordered_map<const void*, Texture> remaining;
    for (auto& entry : g_textures) {
        Texture& t = entry.second;
        if (t.source.kind == TextureSource::None) continue;
        if (t.image.id) {
            sg_destroy_view(t.view);
            sg_destroy_image(t.image);
        }
        t.image = sg_image{};
        t.view = sg_view{};
        t.checkedFrame = 0xFFFFFFFFu;
        ++*kept;
        *keptBytes += t.source.bytes.size();
        remaining.emplace(entry.first, std::move(t));
    }
    DestroyTextures();
    g_textures = std::move(remaining);
}

} // namespace render::detail

namespace render {

bool TextureFromMmp(const void* holder, const void* data, unsigned mip, unsigned levels) {
    if (!holder || !data) return false;
    detail::Start();
    detail::FpuGuard fpu;
    detail::StatsTimer timer(&FrameStats::textureMs);
    return detail::TextureFromMmpData(holder, data, mip, levels);
}

bool ComposeTexture(const void* holder, int size, unsigned mip, unsigned levels, const TextureImage* images, int count) {
    using namespace detail;
    if (!holder || size <= 0 || size > 4096) return false;
    Start();
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::textureMs);
    auto recipe = std::make_shared<ComposeRecipe>();
    recipe->width = recipe->height = size;
    recipe->firstLevel = static_cast<int>(mip);
    recipe->levels = static_cast<int>(mip + (levels ? levels : 1));
    if (recipe->levels > LevelCount(size, size) || recipe->firstLevel >= recipe->levels) {
        if (g_failures++ < 10)
            ei::Log("render: a composed %dx%d texture from level %u (%u levels) has no such level; it shows nothing", size,
                    size, mip, levels);
        ForgetHolder(holder);
        return false;
    }
    for (int k = 0; k < count; ++k) {
        ComposeStep step;
        step.image = DecodeComposeImage(images[k]);
        if (!step.image) continue;
        step.to = Rect{0, 0, size, size};
        recipe->steps.push_back(std::move(step));
    }
    return StoreComposed(holder, std::move(recipe));
}

bool BlankTexture(const void* holder, int width, int height, BlankAlpha alpha, bool mipmapped) {
    using namespace detail;
    if (!holder || width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    Start();
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::textureMs);
    auto recipe = std::make_shared<ComposeRecipe>();
    recipe->width = width;
    recipe->height = height;
    recipe->levels = mipmapped ? LevelCount(width, height) : 1;
    recipe->alpha = alpha;
    return StoreComposed(holder, std::move(recipe));
}

void DrawIntoTexture(const void* holder, const TextureImage* image, int x, int y, int width, int height) {
    using namespace detail;
    Start();
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::textureMs);
    ComposeStep step;
    step.over = false;
    if (image) {
        step.image = DecodeComposeImage(*image);
        if (!step.image) return;
    }
    step.to = Rect{x, y, x + width, y + height};
    DrawStep(holder, std::move(step));
}

void DrawClouds(const void* holder, const CloudFrame& from, const CloudFrame& to, int blend, const std::uint8_t ramp[256]) {
    using namespace detail;
    Start();
    FpuGuard fpu;
    StatsTimer timer(&FrameStats::textureMs);
    auto clouds = std::make_shared<CloudsDraw>();
    clouds->from = from;
    clouds->to = to;
    clouds->blend = blend;
    std::memcpy(clouds->ramp, ramp, sizeof(clouds->ramp));
    ComposeStep step;
    step.over = false;
    step.clouds = std::move(clouds);
    step.to = Rect{0, 0, 256, 256};
    DrawStep(holder, std::move(step));
}

void TexturePatch(const void* target, const int to[4], const void* source, const int from[4]) {
    using namespace detail;
    if (!target || !source) return;
    Start();
    int tw = 0, th = 0, sw = 0, sh = 0;
    if (!HolderSize(target, &tw, &th) || !HolderSize(source, &sw, &sh) || !Inside(to, tw, th) || !Inside(from, sw, sh)) {
        if (g_patchFailures++ < 10)
            ei::Log("render: texture patch (%d,%d)-(%d,%d) of a %dx%d texture from (%d,%d)-(%d,%d) of a %dx%d one: "
                    "not recorded (a texture without its size, or a rectangle outside it)",
                    to[0], to[1], to[2], to[3], tw, th, from[0], from[1], from[2], from[3], sw, sh);
        return;
    }
    Patch& p = g_patches[target];
    p.source = source;
    for (int k = 0; k < 4; ++k) {
        p.to[k] = to[k];
        p.from[k] = from[k];
    }
}

} // namespace render

namespace render {

void HolderGone(const void* holder) {
    detail::g_patches.erase(holder);
    for (auto it = detail::g_patches.begin(); it != detail::g_patches.end();)
        it = it->second.source == holder ? detail::g_patches.erase(it) : std::next(it);
    detail::g_pictures.erase(holder);
    // What the renderer made for it (a later holder at the same address starts afresh).
    detail::ForgetHolder(holder);
    detail::ForgetCursor(holder);
    ui::DropCanvas(holder);
}

void PictureFromMmp(const void* holder, const void* data) {
    if (!holder || !data) return;
    detail::FpuGuard fpu;
    detail::PictureFromMmpData(holder, data);
}

#if defined(EI_RENDER_VALIDATION)
namespace {
// The next level of an RGBA8 level as a 2x2 box filter (sizes halved, at least 1; `*width`, `*height` updated).
std::vector<std::uint8_t> HalveLevel(const std::vector<std::uint8_t>& src, int* width, int* height) {
    int w = *width, h = *height;
    int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
    std::vector<std::uint8_t> dst(static_cast<std::size_t>(nw) * nh * 4);
    for (int y = 0; y < nh; ++y) {
        for (int x = 0; x < nw; ++x) {
            int x0 = x * 2, y0 = y * 2, x1 = x0 + 1 < w ? x0 + 1 : x0, y1 = y0 + 1 < h ? y0 + 1 : y0;
            for (int c = 0; c < 4; ++c) {
                int sum = src[(static_cast<std::size_t>(y0) * w + x0) * 4 + c] + src[(static_cast<std::size_t>(y0) * w + x1) * 4 + c] +
                          src[(static_cast<std::size_t>(y1) * w + x0) * 4 + c] + src[(static_cast<std::size_t>(y1) * w + x1) * 4 + c];
                dst[(static_cast<std::size_t>(y) * nw + x) * 4 + c] = static_cast<std::uint8_t>((sum + 2) / 4);
            }
        }
    }
    *width = nw;
    *height = nh;
    return dst;
}
} // namespace

MmpLevelCheck CheckMmpLevels() {
    using namespace detail;
    MmpLevelCheck c;
    GpuWorkAlone alone;   // (the block-compressed levels read back)
    for (const auto& entry : g_textures) {
        const TextureSource& s = entry.second.source;
        if (s.kind != TextureSource::Mmp || s.header.size() != sizeof(MmpHeader)) continue;
        MmpHeader h{};
        std::memcpy(&h, s.header.data(), sizeof(h));
        MmpLevels read;
        std::uint32_t first = 0, end = 0;
        if (!ReadMmp(h, s.bytes.data(), s.firstOffset, s.mip, s.levels, &read, &first, &end)) continue;
        const int width = read.width, height = read.height;
        std::vector<std::vector<std::uint8_t>>& levels = read.levels;
        if (read.format != SG_PIXELFORMAT_RGBA8) {
            // Blocks: the levels as the GPU decodes them.
            int w = width, hh = height;
            bool all = entry.second.image.id != 0;
            for (std::size_t l = 0; l < levels.size() && all; ++l) {
                if (w < 4 || hh < 4) {
                    levels.resize(l);
                    break;
                }
                all = ReadImageLevel(entry.second.image, static_cast<int>(l), w, hh, &levels[l]);
                w = w > 1 ? w / 2 : 1;
                hh = hh > 1 ? hh / 2 : 1;
            }
            if (!all || levels.empty()) continue;
        }
        ++c.textures;
        // Each level and level 0 box-filtered down to 4 x 4 (or less): the file's own filtering of
        // a level (1-bit alpha kept 0 or 1, sharpened colours) changes its texels, not those averages.
        auto summary = [](std::vector<std::uint8_t> level, int w, int hh) {
            while (w > 4 || hh > 4) level = HalveLevel(level, &w, &hh);
            return level;
        };
        const std::vector<std::uint8_t> reference = summary(levels[0], width, height);
        int w = width, hh = height;
        for (std::size_t l = 1; l < levels.size(); ++l) {
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
            if (w < 4 || hh < 4) break;   // DXT levels under 4 x 4 are only partly in the file
            const std::vector<std::uint8_t> mine = summary(levels[l], w, hh);
            std::uint32_t sum = 0;
            for (std::size_t i = 0; i < reference.size(); ++i)
                sum += static_cast<std::uint32_t>(reference[i] > mine[i] ? reference[i] - mine[i] : mine[i] - reference[i]);
            double difference = static_cast<double>(sum) / static_cast<double>(reference.size());
            ++c.levels;
            if (difference > c.worst) {
                c.worst = difference;
                c.worstWidth = width;
                c.worstHeight = height;
                c.worstLevel = static_cast<int>(l);
                c.worstFormat = h.format;
            }
        }
    }
    return c;
}
#endif

} // namespace render
