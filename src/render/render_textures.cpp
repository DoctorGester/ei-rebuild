// Game textures in the new renderer, keyed by their surface holder: .mmp textures from the file's levels (DXT1-5
// uploaded as their blocks; RGB of any mask layout decoded; the file's levels only) and textures drawn on the GPU
// (render_compose.cpp), kept as GPU textures until the holder goes. Also the cursor pictures' pixels as RGBA8
// (render::PictureFromMmp).
#include "engine/models/texture.h"
#include "../platform/log.h"
#include "render_internal.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <optional>
#include <unordered_map>

namespace render {

struct Texture {
    gpu::Texture image;
    std::uint32_t serial = 0;     // a new one each time the image is replaced (TextureSerial)
    int width = 0, height = 0;    // the first level shown
    bool composed = false;        // drawn on the GPU (shape), else from an .mmp file
    ComposeShape shape;           // composed: what DrawIntoTexture and DrawClouds draw into
    std::uint32_t mmpFormat = 0;  // .mmp: the file's format
    int levels = 0;               // .mmp: the levels uploaded
};

static std::unordered_map<const Surface*, Texture> g_textures;   // by surface holder: textures (the only copy)
// Replaced during the frame: released at Present (the frame's recorded commands, drawn at its end, hold them).
static Texture g_white;
static int g_failures = 0;

constexpr int kMostLevels = 16;

// The bytes of a row of a level `width` texels wide: of 4x4 blocks for the compressed formats.
static std::size_t RowPitch(gpu::Format format, int width) {
    if (format == gpu::Format::Bc1) return static_cast<std::size_t>((width + 3) / 4) * 8;
    if (format == gpu::Format::Bc2 || format == gpu::Format::Bc3) return static_cast<std::size_t>((width + 3) / 4) * 16;
    return static_cast<std::size_t>(width) * 4;
}

// A texture of `format` from `levels` (level 0 first): the levels given only, as the .mmp file has them and the
// original's texture had them (RGBA8 or the file's DXT blocks).
static gpu::Texture MakeImage(gpu::Format format, int width, int height, std::vector<std::vector<std::uint8_t>> levels,
                              const char* label, FrameStats::Upload kind) {
    std::size_t bytes = 0;
    for (const std::vector<std::uint8_t>& level : levels) bytes += level.size();
    CountUpload(kind, bytes);
    gpu::Texture t = gpu::MakeTexture({width, height, static_cast<int>(levels.size()), 1, format, gpu::TextureUse::Sampled, label});
    bool written = static_cast<bool>(t);
    for (std::size_t i = 0; written && i < levels.size(); ++i) {
        const int w = std::max(width >> i, 1), h = std::max(height >> i, 1);
        written = gpu::Write(t, static_cast<int>(i), {0, 0, w, h}, levels[i].data(), RowPitch(format, w));
    }
    if (!written) {
        LogFirst(g_failures, 10, "render: a texture image could not be made (bad data); drawn white");
        gpu::Release(t);
    }
    return t;
}

static const Texture& White() {
    if (!g_white.image)
        g_white.image = MakeImage(gpu::Format::Rgba8, 1, 1, {std::vector<std::uint8_t>(4, 255)}, "ei-white", FrameStats::Upload::Small);
    return g_white;
}

static std::uint32_t g_serials = 0;
static void Retire(Texture& t) {
    ReleaseAfterPresent(t.image);
    t.image = gpu::Texture{};
    t.serial = ++g_serials;
}

// The levels of a texture of `width` x `height` down to 1 x 1.
static int LevelCount(int width, int height) {
    int levels = 1;
    while ((width > 1 || height > 1) && levels < kMostLevels) {
        width = width > 1 ? width / 2 : 1;
        height = height > 1 ? height / 2 : 1;
        ++levels;
    }
    return levels;
}

struct Patch {
    const Surface* source = nullptr;
    int to[4] = {};
    int from[4] = {};
};
static std::unordered_map<const Surface*, Patch> g_patches;
static int g_patchFailures = 0;

static std::optional<Size> HolderSize(const Surface* holder) {
    auto decoded = g_textures.find(holder);
    if (decoded == g_textures.end() || decoded->second.width <= 0) return std::nullopt;
    return Size{decoded->second.width, decoded->second.height};
}

static bool Inside(const int r[4], int width, int height) {
    return r[0] >= 0 && r[1] >= 0 && r[0] < r[2] && r[1] < r[3] && r[2] <= width && r[3] <= height;
}

// The bytes level `mip` takes in the file as the game's fill copies it: bits per pixel x width x height / 8
// of the level's size (halved per level, down to 0), no whole DXT blocks for the levels under 4 x 4.
static std::uint32_t MmpFileLevelBytes(const MmpHeader& h, unsigned mip) {
    return (h.bitsPerPixel * (h.width >> (mip & 31)) * (h.height >> (mip & 31))) >> 3;
}

// The block-compressed format an .mmp "DXTn" file's blocks are uploaded in (DXT2 / DXT4, the premultiplied
// variants, as DXT3 / DXT5); None for the RGB formats.
static gpu::Format BlockFormat(MmpFormat id) {
    const auto format = static_cast<std::uint32_t>(id);
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
static bool DecodeMmpLevel(const MmpHeader& h, const std::uint8_t* pixels, int width, int height,
                           std::vector<std::uint8_t>& out) {
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

static std::uint32_t MmpLevelBytes(const MmpHeader& h, int width, int height) {
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
static bool ReadMmp(const MmpHeader& h, const std::uint8_t* pixels, unsigned mip, unsigned levels, MmpLevels* read) {
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

static int g_mmpFailures = 0;

static std::unordered_map<const Surface*, Picture> g_pictures;

// `step` (its rectangle `to` relative to the texture, clipped to it) drawn into the texture BlankTexture made for
// `holder`.
static void DrawStep(const Surface* holder, ComposeStep step) {
    auto found = holder ? g_textures.find(holder) : g_textures.end();
    if (found == g_textures.end() || !found->second.composed) {
        LogFirst(g_failures, 10, "render: a texture drawn into that BlankTexture did not make (holder %p)", holder);
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

static gpu::Wrap ToGpu(Address address) {
    switch (address) {
    case Address::Mirror: return gpu::Wrap::Mirror;
    case Address::Clamp: return gpu::Wrap::Clamp;
    default: return gpu::Wrap::Repeat;
    }
}

static gpu::Filter ToGpu(Filter filter) { return filter == Filter::Point ? gpu::Filter::Nearest : gpu::Filter::Linear; }

const gpu::Texture& SurfaceView(const Surface* holder) {
    Timer timer(FrameStats::Time::TextureMs);
    auto decoded = holder ? g_textures.find(holder) : g_textures.end();
    if (decoded == g_textures.end() || !decoded->second.image) return White().image;
    return decoded->second.image;
}

// The entry of `holder` from an .mmp file's levels (of the file's `format`): the texture until the holder goes.
bool StoreMmp(const Surface* holder, MmpLevels read, std::uint32_t format) {
    if (!holder) return false;
    Texture& t = g_textures[holder];
    Retire(t);
    t.composed = false;
    t.mmpFormat = format;
    t.levels = static_cast<int>(read.levels.size());
    t.image =
        MakeImage(read.format, read.width, read.height, std::move(read.levels), "ei-mmp-texture", FrameStats::Upload::Mmp);
    t.width = read.width;
    t.height = read.height;
    return static_cast<bool>(t.image);
}

const gpu::Texture& WhiteView() { return White().image; }

bool StoreComposed(const Surface* holder, const ComposeShape& shape, const ComposeStep* steps, std::size_t count) {
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

StagePatch PatchOf(const Surface* holder) {
    StagePatch p;
    if (g_patches.empty() || !holder) return p;
    auto it = g_patches.find(holder);
    if (it == g_patches.end()) return p;
    const Patch& patch = it->second;
    const std::optional<Size> targetSize = HolderSize(holder), sourceSize = HolderSize(patch.source);
    if (!targetSize || !sourceSize) return p;
    const auto [tw, th] = *targetSize;
    const auto [sw, sh] = *sourceSize;
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

bool TextureFromMmpData(const Surface* holder, const MmpFile& file, unsigned mip, unsigned levels) {
    if (!holder || !file.data) return false;
    const MmpHeader& h = file.header;
    const std::uint8_t* base = file.Body();
    if (h.width == 0 || h.height == 0 || h.width > 8192 || h.height > 8192 || h.mipCount == 0 || h.mipCount > 16) {
        LogFirst(g_mmpFailures, 10, "render: .mmp header not understood; the texture is not decoded");
        return false;
    }
    MmpLevels read;
    if (!ReadMmp(h, base, mip, levels, &read)) {
        LogFirst(g_mmpFailures, 10, "render: .mmp format %08X (%u bits) not decoded", static_cast<unsigned>(h.format),
                 static_cast<unsigned>(h.bitsPerPixel));
        return false;
    }
    return StoreMmp(holder, std::move(read), static_cast<std::uint32_t>(h.format));
}

bool PictureFromMmpData(const Surface* holder, const MmpFile& file) {
    g_pictures.erase(holder);
    const MmpHeader& h = file.header;
    if (h.width == 0 || h.height == 0 || h.width > 4096 || h.height > 4096) return false;
    Picture p;
    p.width = static_cast<int>(h.width);
    p.height = static_cast<int>(h.height);
    if (!DecodeMmpLevel(h, file.Body() + h.MipOffset(0), p.width, p.height, p.rgba)) {
        LogFirst(g_mmpFailures, 10, "render: picture format %08X not decoded; the cursor draws nothing for it",
                 static_cast<unsigned>(h.format));
        return false;
    }
    g_pictures.emplace(holder, std::move(p));
    return true;
}

const Picture* PictureOf(const Surface* holder) {
    auto it = g_pictures.find(holder);
    return it == g_pictures.end() ? nullptr : &it->second;
}

std::uint32_t TextureSerial(const ::Texture* texture) {
    auto found = texture ? g_textures.find(texture->surface.Get()) : g_textures.end();
    return found == g_textures.end() ? 0 : found->second.serial;
}

const gpu::Texture& TextureView(const ::Texture* texture) {
    if (!texture) return White().image;
    return SurfaceView(texture->surface);
}

gpu::Sampler LinearSampler() {
    // The game's D3DTFP_POINT mips.
    return gpu::MakeSampler({gpu::Filter::Linear, gpu::Filter::Linear, gpu::Filter::Nearest});
}

// The stages' samplers by their filters and addresses (gpu.h keeps them by description).
static gpu::SamplerDesc StageDesc(const StageState& s) {
    gpu::SamplerDesc d;
    d.mag = ToGpu(s.magFilter);
    d.min = ToGpu(s.minFilter);
    d.mip = s.mipFilter == MipFilter::Linear ? gpu::Filter::Linear : gpu::Filter::Nearest;
    if (s.mipFilter == MipFilter::None) d.maxLod = 0.0f;   // level 0 only
    d.u = ToGpu(s.addressU);
    d.v = ToGpu(s.addressV);
    return d;
}

// Anisotropic with linear mips: the level of detail moves smoothly across the tiles, whose texture coordinates change
// direction and scale from tile to tile (D3DTFP_POINT's level steps showed as seams; renderer.md section 11). The
// render quality's anisotropy samples the world's models so too.
static gpu::SamplerDesc AnisotropicDesc(const StageState& s, std::uint32_t level) {
    gpu::SamplerDesc d;
    d.min = gpu::Filter::Linear;
    d.mag = gpu::Filter::Linear;
    d.mip = gpu::Filter::Linear;
    d.anisotropy = static_cast<int>(level);
    if (s.mipFilter == MipFilter::None) d.maxLod = 0.0f;   // the options' Mipmapping 0: level 0 only
    d.u = ToGpu(s.addressU);
    d.v = ToGpu(s.addressV);
    return d;
}

// The two kinds of sampler below by a packed key of what makes them (the stage's filters and addresses, the
// anisotropic kind and its level), for the device they were made on: found without hashing a description per draw.
// Only samplers made are kept (one that could not be made is tried again, as MakeSampler does).
struct KeyedSampler {
    std::uint64_t key = 0;
    gpu::Sampler sampler;
};
static std::vector<KeyedSampler> g_keyedSamplers;
static std::uint32_t g_keyedSamplersDevice = 0;   // the gpu::DeviceSerial they were made on

static gpu::Sampler KeptSampler(const StageState& s, bool anisotropic, std::uint32_t level) {
    auto bits = [](auto v) { return static_cast<std::uint64_t>(v); };
    const std::uint64_t key = bits(s.magFilter) | bits(s.minFilter) << 8 | bits(s.mipFilter) << 16 |
                              bits(s.addressU) << 24 | bits(s.addressV) << 32 | bits(anisotropic) << 40 | bits(level) << 41;
    if (g_keyedSamplersDevice != gpu::DeviceSerial()) {
        g_keyedSamplers.clear();
        g_keyedSamplersDevice = gpu::DeviceSerial();
    }
    for (const KeyedSampler& k : g_keyedSamplers)
        if (k.key == key) return k.sampler;
    const gpu::Sampler made = gpu::MakeSampler(anisotropic ? AnisotropicDesc(s, level) : StageDesc(s));
    if (made) g_keyedSamplers.push_back({key, made});
    return made;
}

gpu::Sampler StageSampler(const StageState& s) { return KeptSampler(s, false, 0); }

gpu::Sampler AnisotropicSampler(const StageState& s, std::uint32_t level) {
    // Point sampling asked for (the options' Filtering 0): as the stage says.
    if (s.minFilter == Filter::Point || s.magFilter == Filter::Point) return StageSampler(s);
    return KeptSampler(s, true, level);
}

void ForgetHolder(const Surface* holder) {
    auto it = g_textures.find(holder);
    if (it == g_textures.end()) return;
    Retire(it->second);
    g_textures.erase(it);
}

bool TextureFromMmp(const Surface* holder, const MmpFile& file, unsigned mip, unsigned levels) {
    if (!holder || !file.data) return false;
    Start();
    Timer timer(FrameStats::Time::TextureMs);
    return TextureFromMmpData(holder, file, mip, levels);
}

bool ComposeTexture(const Surface* holder, int size, unsigned mip, unsigned levels, const TextureImage* images, int count) {
    if (!holder || size <= 0 || size > 4096) return false;
    Start();
    Timer timer(FrameStats::Time::TextureMs);
    ComposeShape shape;
    shape.width = shape.height = size;
    shape.firstLevel = static_cast<int>(mip);
    shape.levels = static_cast<int>(mip + (levels ? levels : 1));
    if (shape.levels > LevelCount(size, size) || shape.firstLevel >= shape.levels) {
        LogFirst(g_failures, 10,
                 "render: a composed %dx%d texture from level %u (%u levels) has no such level; it shows nothing", size,
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

bool BlankTexture(const Surface* holder, int width, int height, BlankAlpha alpha, bool mipmapped) {
    if (!holder || width <= 0 || height <= 0 || width > 4096 || height > 4096) return false;
    Start();
    Timer timer(FrameStats::Time::TextureMs);
    ComposeShape shape;
    shape.width = width;
    shape.height = height;
    shape.levels = mipmapped ? LevelCount(width, height) : 1;
    shape.alpha = alpha;
    return StoreComposed(holder, shape, nullptr, 0);
}

void DrawIntoTexture(const Surface* holder, const TextureImage* image, int x, int y, int width, int height) {
    Start();
    Timer timer(FrameStats::Time::TextureMs);
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

// The clouds' texture's size for a frame `height` pixels high: the power of two at least as large (the map's cloud
// layers stretch the texture's height over about the frame's), within 512 .. 2048.
static int CloudTextureSize(int height) {
    int size = 512;
    while (size < height && size < 2048) size *= 2;
    return size;
}

void DrawClouds(const Surface* holder, const MapClouds& clouds) {
    Start();
    Timer timer(FrameStats::Time::TextureMs);
    auto found = holder ? g_textures.find(holder) : g_textures.end();
    if (found == g_textures.end() || !found->second.composed) {
        LogFirst(g_failures, 10, "render: the clouds' texture did not make (holder %p)", holder);
        return;
    }
    Texture& t = found->second;
    const int size = CloudTextureSize(FrameSize().value_or(Size{0, 720}).h);
    if (t.shape.width != size || t.shape.height != size) {
        ComposeShape shape = t.shape;
        shape.width = shape.height = size;
        shape.firstLevel = 0;
        if (shape.levels > 1) shape.levels = LevelCount(size, size);
        StoreComposed(holder, shape, nullptr, 0);
    }
    DrawCloudTexture(t.shape, t.image, clouds);
}

void TexturePatch(const Surface* target, const int to[4], const Surface* source, const int from[4]) {
    if (!target || !source) return;
    Start();
    const std::optional<Size> targetSize = HolderSize(target), sourceSize = HolderSize(source);
    const auto [tw, th] = targetSize.value_or(Size{});
    const auto [sw, sh] = sourceSize.value_or(Size{});
    if (!targetSize || !sourceSize || !Inside(to, tw, th) || !Inside(from, sw, sh)) {
        LogFirst(g_patchFailures, 10,
                 "render: texture patch (%d,%d)-(%d,%d) of a %dx%d texture from (%d,%d)-(%d,%d) of a %dx%d one: "
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

void HolderGone(const Surface* holder) {
    g_patches.erase(holder);
    for (auto it = g_patches.begin(); it != g_patches.end();)
        it = it->second.source == holder ? g_patches.erase(it) : std::next(it);
    g_pictures.erase(holder);
    // What the renderer made for it (a later holder at the same address starts afresh).
    ForgetHolder(holder);
    ForgetCursor(holder);
}

void PictureFromMmp(const Surface* holder, const MmpFile& file) {
    if (!holder || !file.data) return;
    PictureFromMmpData(holder, file);
}

std::vector<HeldMmp> HeldMmpTextures() {
    std::vector<HeldMmp> held;
    for (const auto& entry : g_textures) {
        const Texture& t = entry.second;
        if (!t.composed && t.image) held.push_back({t.image, t.width, t.height, t.levels, t.mmpFormat});
    }
    return held;
}

}   // namespace render
