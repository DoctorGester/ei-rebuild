// Game textures in the new renderer, keyed by their surface holder: .mmp textures from the file's levels (DXT1-5
// uploaded as their blocks; RGB of any mask layout decoded; the file's levels only) and textures drawn on the GPU
// (render_compose.cpp), kept as GPU textures until the holder goes. Also the cursor pictures' pixels as RGBA8
// (render::PictureFromMmp).
#include "render_internal.h"

#include "../platform/log.h"

#include "../game/texture.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <unordered_map>

namespace render::detail {

namespace {

struct Texture {
    gpu::Texture image;
    int width = 0, height = 0;    // the first level shown
    bool composed = false;        // drawn on the GPU (shape), else from an .mmp file
    ComposeShape shape;           // composed: what DrawIntoTexture and DrawClouds draw into
    std::uint32_t mmpFormat = 0;  // .mmp: the file's format
    int levels = 0;               // .mmp: the levels uploaded
    std::size_t bytes = 0;        // .mmp: the image's bytes on the GPU
};

std::unordered_map<const void*, Texture> g_textures;   // by surface holder: textures (the only copy)
// Replaced during the frame: released at Present (the frame's recorded commands, drawn at its end, hold them).
std::vector<gpu::Texture> g_retiredImages;
Texture g_white;
int g_failures = 0;

constexpr int kMostLevels = 16;

// The bytes of a row of a level `width` texels wide: of 4x4 blocks for the compressed formats.
std::size_t RowPitch(gpu::Format format, int width) {
    if (format == gpu::Format::Bc1) return static_cast<std::size_t>((width + 3) / 4) * 8;
    if (format == gpu::Format::Bc2 || format == gpu::Format::Bc3) return static_cast<std::size_t>((width + 3) / 4) * 16;
    return static_cast<std::size_t>(width) * 4;
}

// A texture of `format` from `levels` (level 0 first): the levels given only, as the .mmp file has them and the
// original's texture had them (RGBA8 or the file's DXT blocks).
gpu::Texture MakeImage(gpu::Format format, int width, int height, std::vector<std::vector<std::uint8_t>> levels,
                       const char* label, FrameStats::Upload kind, std::size_t* madeBytes = nullptr) {
    std::size_t bytes = 0;
    for (const std::vector<std::uint8_t>& level : levels) bytes += level.size();
    CountImage(ImageUse::Texture, true);
    CountUpload(kind, bytes);
    if (madeBytes) *madeBytes = bytes;
    gpu::Texture t = gpu::MakeTexture({width, height, static_cast<int>(levels.size()), 1, format, gpu::kSampled, label});
    bool written = static_cast<bool>(t);
    for (std::size_t i = 0; written && i < levels.size(); ++i) {
        const int w = std::max(width >> i, 1), h = std::max(height >> i, 1);
        written = gpu::Write(t, static_cast<int>(i), {0, 0, w, h}, levels[i].data(), RowPitch(format, w));
    }
    if (!written) {
        if (g_failures++ < 10) ei::Log("render: a texture image could not be made (bad data); drawn white");
        gpu::Release(t);
    }
    return t;
}

const Texture& White() {
    if (!g_white.image)
        g_white.image = MakeImage(gpu::Format::Rgba8, 1, 1, {std::vector<std::uint8_t>(4, 255)}, "ei-white", FrameStats::Small);
    return g_white;
}

void Retire(Texture& t) {
    if (t.image) g_retiredImages.push_back(t.image);
    t.image = gpu::Texture{};
}

// The levels of a texture of `width` x `height` down to 1 x 1.
int LevelCount(int width, int height) {
    int levels = 1;
    while ((width > 1 || height > 1) && levels < kMostLevels) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

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
// variants, as DXT3 / DXT5); None for the RGB formats.
gpu::Format BlockFormat(std::uint32_t format) {
    if ((format & 0xFFFFFF) != 0x545844) return gpu::Format::None;   // "DXT"
    switch (static_cast<char>(format >> 24)) {
    case '1': return gpu::Format::Bc1;
    case '2':
    case '3': return gpu::Format::Bc2;
    case '4':
    case '5': return gpu::Format::Bc3;
    default: return gpu::Format::None;
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
    if (BlockFormat(h.format) != gpu::Format::None) {
        auto blocks = static_cast<std::uint32_t>((width + 3) / 4) * static_cast<std::uint32_t>((height + 3) / 4);
        return blocks * (BlockFormat(h.format) == gpu::Format::Bc1 ? 8u : 16u);
    }
    return static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * (h.bitsPerPixel / 8);
}

// The levels of an .mmp texture as uploaded: a DXT file's blocks as the file has them (`format` BC1 / BC2 / BC3),
// an RGB file's pixels decoded to RGBA8.
struct MmpLevels {
    gpu::Format format = gpu::Format::None;
    int width = 0, height = 0;   // the first level's
    std::vector<std::vector<std::uint8_t>> levels;
};

// The levels from `mip` on (`levels` of them, at least one); `pixels`: the file's bytes after the header.
bool ReadMmp(const MmpHeader& h, const std::uint8_t* pixels, unsigned mip, unsigned levels, MmpLevels* read) {
    *read = MmpLevels{};
    const gpu::Format blocks = BlockFormat(h.format);
    read->format = blocks != gpu::Format::None ? blocks : gpu::Format::Rgba8;
    // The game's offset of the first level (MmpHeader::MipOffset) for the level the texture starts at only: its
    // product overflows from about the sixth level of a 1024 x 1024 texture on, so the later levels step from level
    // to level (MmpFileLevelBytes).
    std::uint32_t offset = h.MipOffset(mip);
    std::vector<std::uint8_t> padded;
    for (unsigned m = mip; m < mip + (levels ? levels : 1) && m < h.mipCount && read->levels.size() < kMostLevels; ++m) {
        int w = static_cast<int>(h.width >> (m & 31)), hh = static_cast<int>(h.height >> (m & 31));
        if (w < 1) w = 1;
        if (hh < 1) hh = 1;
        std::uint32_t bytes = MmpFileLevelBytes(h, m);
        const std::uint8_t* source = pixels + offset;
        if (bytes < MmpLevelBytes(h, w, hh)) {
            // A level the fill copies only part of (a DXT level under 4 x 4: the start of its block): the rest as zeros.
            padded.assign(MmpLevelBytes(h, w, hh), 0);
            std::memcpy(padded.data(), source, bytes);
            source = padded.data();
        }
        std::vector<std::uint8_t> level;
        if (blocks != gpu::Format::None) level.assign(source, source + MmpLevelBytes(h, w, hh));
        else if (!DecodeMmpLevel(h, source, w, hh, level)) break;
        if (m == mip) {
            read->width = w;
            read->height = hh;
        }
        read->levels.push_back(std::move(level));
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
        if (!entry.second.composed && entry.second.image) held += entry.second.bytes;
    auto made = g_textures.find(holder);
    if (made != g_textures.end() && made->second.image) g_mmpImageCounts.bytes += made->second.bytes;
    ++g_mmpImageCounts.made;
    g_mmpImageCounts.ticks += ticks;
    if (held > g_mmpImageCounts.mostHeld) g_mmpImageCounts.mostHeld = held;
}
#endif

std::unordered_map<const void*, Picture> g_pictures;

// `step` (its rectangle `to` relative to the texture, clipped to it) drawn into the texture BlankTexture made for
// `holder`.
void DrawStep(const void* holder, ComposeStep step) {
    auto found = holder ? g_textures.find(holder) : g_textures.end();
    if (found == g_textures.end() || !found->second.composed) {
        if (g_failures++ < 10) ei::Log("render: a texture drawn into that BlankTexture did not make (holder %p)", holder);
        return;
    }
    const Texture& t = found->second;
    const Rect wanted = step.to;
    Rect to = wanted;
    if (to.left < 0) to.left = 0;
    if (to.top < 0) to.top = 0;
    if (to.right > t.shape.width) to.right = t.shape.width;
    if (to.bottom > t.shape.height) to.bottom = t.shape.height;
    if (to.IsEmpty()) return;
    step.fromX = to.left - wanted.left;
    step.fromY = to.top - wanted.top;
    step.to = to;
    DrawComposed(t.shape, t.image, &step, 1);
}

gpu::Wrap Wrap(std::uint8_t address) {
    switch (address) {
    case 2: return gpu::Wrap::Mirror;
    case 3: return gpu::Wrap::Clamp;
    default: return gpu::Wrap::Repeat;
    }
}

} // namespace

const gpu::Texture& SurfaceView(const void* holder) {
    StatsTimer timer(&FrameStats::textureMs);
    auto decoded = holder ? g_textures.find(holder) : g_textures.end();
    if (decoded == g_textures.end() || !decoded->second.image) return White().image;
    return decoded->second.image;
}

// The entry of `holder` from an .mmp file's levels (of the file's `format`): the texture until the holder goes.
bool StoreMmp(const void* holder, MmpLevels read, std::uint32_t format) {
    if (!holder) return false;
    Texture& t = g_textures[holder];
    Retire(t);
    t.composed = false;
    t.mmpFormat = format;
    t.levels = static_cast<int>(read.levels.size());
    t.image = MakeImage(read.format, read.width, read.height, std::move(read.levels), "ei-mmp-texture", FrameStats::Mmp,
                        &t.bytes);
    t.width = read.width;
    t.height = read.height;
    return static_cast<bool>(t.image);
}

const gpu::Texture& WhiteView() { return White().image; }

bool StoreComposed(const void* holder, const ComposeShape& shape, const ComposeStep* steps, std::size_t count) {
    if (!holder) return false;
    Texture& t = g_textures[holder];
    Retire(t);
    t.image = MakeComposed(shape, steps, count);
    t.width = shape.width >> shape.firstLevel;
    t.height = shape.height >> shape.firstLevel;
    t.composed = true;
    t.shape = shape;
    return static_cast<bool>(t.image);
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

bool TextureFromMmpData(const void* holder, const MmpFile& file, unsigned mip, unsigned levels) {
    if (!holder || !file.data) return false;
    const MmpHeader& h = file.header;
    const std::uint8_t* base = file.Body();
    if (h.width == 0 || h.height == 0 || h.width > 8192 || h.height > 8192 || h.mipCount == 0 || h.mipCount > 16) {
        if (g_mmpFailures++ < 10) ei::Log("render: .mmp header not understood; the texture is not decoded");
        return false;
    }
#if defined(EI_RENDER_VALIDATION)
    const long long started = Ticks();
#endif
    MmpLevels read;
    if (!ReadMmp(h, base, mip, levels, &read)) {
        if (g_mmpFailures++ < 10)
            ei::Log("render: .mmp format %08X (%u bits) not decoded",
                    static_cast<unsigned>(h.format), static_cast<unsigned>(h.bitsPerPixel));
        return false;
    }
    bool made = StoreMmp(holder, std::move(read), h.format);
#if defined(EI_RENDER_VALIDATION)
    CountMmpImage(holder, Ticks() - started);
#endif
    return made;
}

bool PictureFromMmpData(const void* holder, const MmpFile& file) {
    g_pictures.erase(holder);
    const MmpHeader& h = file.header;
    if (h.width == 0 || h.height == 0 || h.width > 4096 || h.height > 4096) return false;
    Picture p;
    p.width = static_cast<int>(h.width);
    p.height = static_cast<int>(h.height);
    if (!DecodeMmpLevel(h, file.Body() + h.MipOffset(0), p.width, p.height, p.rgba)) {
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

const gpu::Texture& TextureView(const void* texture) {
    if (!texture) return White().image;
    return SurfaceView(static_cast<const ::Texture*>(texture)->surface);
}

gpu::Sampler LinearSampler() {
    // The game's D3DTFP_POINT mips.
    static const gpu::Sampler linear = gpu::MakeSampler({gpu::Filter::Linear, gpu::Filter::Linear, gpu::Filter::Nearest});
    return linear;
}

namespace {
// The stages' samplers by their filters and addresses (gpu.h keeps them by description; this skips the lookup).
std::unordered_map<std::uint32_t, gpu::Sampler> g_samplers;
} // namespace

gpu::Sampler StageSampler(const StageState& s) {
    std::uint32_t key = static_cast<std::uint32_t>(s.magFilter & 7) | (static_cast<std::uint32_t>(s.minFilter & 7) << 3) |
                        (static_cast<std::uint32_t>(s.mipFilter & 7) << 6) | (static_cast<std::uint32_t>(s.addressU & 7) << 9) |
                        (static_cast<std::uint32_t>(s.addressV & 7) << 12);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end()) return it->second;
    gpu::SamplerDesc d;
    d.mag = s.magFilter == 1 ? gpu::Filter::Nearest : gpu::Filter::Linear;   // D3DTFG_POINT
    d.min = s.minFilter == 1 ? gpu::Filter::Nearest : gpu::Filter::Linear;   // D3DTFN_POINT
    d.mip = s.mipFilter == 3 ? gpu::Filter::Linear : gpu::Filter::Nearest;
    if (s.mipFilter <= 1) d.maxLod = 0.0f;   // D3DTFP_NONE: level 0 only
    d.u = Wrap(s.addressU);
    d.v = Wrap(s.addressV);
    gpu::Sampler sampler = gpu::MakeSampler(d);
    g_samplers.emplace(key, sampler);
    return sampler;
}

gpu::Sampler AnisotropicSampler(const StageState& s, std::uint32_t level) {
    // Point sampling asked for (the options' Filtering 0): as the stage says.
    if (s.minFilter == 1 || s.magFilter == 1) return StageSampler(s);
    std::uint32_t key = 0x80000000u | (s.mipFilter <= 1 ? 1u : 0u) | (static_cast<std::uint32_t>(s.addressU & 7) << 9) |
                        (static_cast<std::uint32_t>(s.addressV & 7) << 12) | ((level & 31u) << 16);
    auto it = g_samplers.find(key);
    if (it != g_samplers.end()) return it->second;
    // Anisotropic with linear mips: the level of detail moves smoothly across the tiles, whose texture
    // coordinates change direction and scale from tile to tile (D3DTFP_POINT's level steps showed as seams;
    // renderer.md section 11). The render quality's anisotropy samples the world's models so too.
    gpu::SamplerDesc d;
    d.min = gpu::Filter::Linear;
    d.mag = gpu::Filter::Linear;
    d.mip = gpu::Filter::Linear;
    d.anisotropy = static_cast<int>(level);
    if (s.mipFilter <= 1) d.maxLod = 0.0f;   // D3DTFP_NONE (the options' Mipmapping 0): level 0 only
    d.u = Wrap(s.addressU);
    d.v = Wrap(s.addressV);
    gpu::Sampler sampler = gpu::MakeSampler(d);
    ei::Log("render: anisotropic sampler: %u with linear mips%s (the stage's filters %u / %u / %u)", level,
            s.mipFilter <= 1 ? ", level 0 only" : "", s.magFilter, s.minFilter, s.mipFilter);
    g_samplers.emplace(key, sampler);
    return sampler;
}

void RetireTextures() {
    for (gpu::Texture& t : g_retiredImages) gpu::Release(t);
    g_retiredImages.clear();
}

void ForgetHolder(const void* holder) {
    auto it = g_textures.find(holder);
    if (it == g_textures.end()) return;
    Retire(it->second);
    g_textures.erase(it);
}

} // namespace render::detail

namespace render {

bool TextureFromMmp(const void* holder, const MmpFile& file, unsigned mip, unsigned levels) {
    if (!holder || !file.data) return false;
    detail::Start();
    detail::StatsTimer timer(&FrameStats::textureMs);
    return detail::TextureFromMmpData(holder, file, mip, levels);
}

bool ComposeTexture(const void* holder, int size, unsigned mip, unsigned levels, const TextureImage* images, int count) {
    using namespace detail;
    if (!holder || size <= 0 || size > 4096) return false;
    Start();
    StatsTimer timer(&FrameStats::textureMs);
    ComposeShape shape;
    shape.width = shape.height = size;
    shape.firstLevel = static_cast<int>(mip);
    shape.levels = static_cast<int>(mip + (levels ? levels : 1));
    if (shape.levels > LevelCount(size, size) || shape.firstLevel >= shape.levels) {
        if (g_failures++ < 10)
            ei::Log("render: a composed %dx%d texture from level %u (%u levels) has no such level; it shows nothing", size,
                    size, mip, levels);
        ForgetHolder(holder);
        return false;
    }
    std::vector<ComposeImage> decoded(static_cast<std::size_t>(count > 0 ? count : 0));
    std::vector<ComposeStep> steps;
    for (int k = 0; k < count; ++k) {
        if (!DecodeComposeImage(images[k], &decoded[static_cast<std::size_t>(k)])) continue;
        ComposeStep step;
        step.image = &decoded[static_cast<std::size_t>(k)];
        step.to = Rect{0, 0, size, size};
        steps.push_back(step);
    }
    return StoreComposed(holder, shape, steps.data(), steps.size());
}

bool BlankTexture(const void* holder, int width, int height, BlankAlpha alpha, bool mipmapped) {
    using namespace detail;
    if (!holder || width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    Start();
    StatsTimer timer(&FrameStats::textureMs);
    ComposeShape shape;
    shape.width = width;
    shape.height = height;
    shape.levels = mipmapped ? LevelCount(width, height) : 1;
    shape.alpha = alpha;
    return StoreComposed(holder, shape, nullptr, 0);
}

void DrawIntoTexture(const void* holder, const TextureImage* image, int x, int y, int width, int height) {
    using namespace detail;
    Start();
    StatsTimer timer(&FrameStats::textureMs);
    ComposeImage decoded;
    ComposeStep step;
    step.over = false;
    if (image) {
        if (!DecodeComposeImage(*image, &decoded)) return;
        step.image = &decoded;
    }
    step.to = Rect{x, y, x + width, y + height};
    DrawStep(holder, step);
}

void DrawClouds(const void* holder, const CloudFrame& from, const CloudFrame& to, int blend, const std::uint8_t ramp[256]) {
    using namespace detail;
    Start();
    StatsTimer timer(&FrameStats::textureMs);
    CloudsDraw clouds;
    clouds.from = from;
    clouds.to = to;
    clouds.blend = blend;
    std::memcpy(clouds.ramp, ramp, sizeof(clouds.ramp));
    ComposeStep step;
    step.over = false;
    step.clouds = &clouds;
    step.to = Rect{0, 0, 256, 256};
    DrawStep(holder, step);
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
}

void PictureFromMmp(const void* holder, const MmpFile& file) {
    if (!holder || !file.data) return;
    detail::PictureFromMmpData(holder, file);
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
    for (const auto& entry : g_textures) {
        const Texture& t = entry.second;
        if (t.composed || !t.image) continue;
        // The levels as the GPU reads them, down to 4 x 4 (DXT levels under 4 x 4 are only partly in the file).
        const int width = t.width, height = t.height;
        std::vector<std::vector<std::uint8_t>> levels;
        int w = width, hh = height;
        bool all = true;
        for (int l = 0; l < t.levels && w >= 4 && hh >= 4 && all; ++l) {
            levels.emplace_back();
            all = ReadImageLevel(t.image, l, w, hh, &levels.back());
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
        }
        if (!all || levels.empty()) continue;
        ++c.textures;
        // Each level and level 0 box-filtered down to 4 x 4 (or less): the file's own filtering of
        // a level (1-bit alpha kept 0 or 1, sharpened colours) changes its texels, not those averages.
        auto summary = [](std::vector<std::uint8_t> level, int w, int hh) {
            while (w > 4 || hh > 4) level = HalveLevel(level, &w, &hh);
            return level;
        };
        const std::vector<std::uint8_t> reference = summary(levels[0], width, height);
        w = width;
        hh = height;
        for (std::size_t l = 1; l < levels.size(); ++l) {
            w = w > 1 ? w / 2 : 1;
            hh = hh > 1 ? hh / 2 : 1;
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
                c.worstFormat = t.mmpFormat;
            }
        }
    }
    return c;
}
#endif

} // namespace render
