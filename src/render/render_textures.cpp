// Game textures in the new renderer, keyed by their surface holder (CTexture +0x5C; no holder has a
// DirectDraw surface): .mmp textures decoded from the file's levels (RGB of any mask layout, DXT1-5;
// levels the file lacks box-filtered) and CPU pixels (TexturePixels), kept as sokol images until the
// holder goes, with what they were decoded from for a new device. Also here: the holders' pixels in
// memory the renderer owns (render::SurfaceMemory: DIB sections the game code draws into). A texture
// whose holder has such memory (the save / load screen's pictures, the CPU-written, shadow and .bmp
// textures) is read from it, again when the memory changes; so are window surfaces
// (WindowSurfaceView).
#include "render_internal.h"

#include "../hook/log.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "../game/texture.h"   // CDXSurface, CTexture

#include <cstring>
#include <unordered_map>

namespace render::detail {

namespace {

// What an owned texture (below) was decoded from: the .mmp file's levels as the file has them (as
// compressed as the file: about what DirectDraw's managed texture kept in system memory), or the
// RGBA8 level of TexturePixels. A new Direct3D 11 device (the old one was lost) gets the texture
// decoded again from it.
struct TextureSource {
    enum Kind : std::uint8_t { None, Mmp, Rgba } kind = None;
    std::vector<std::uint8_t> header;   // Mmp: the file's header
    std::vector<std::uint8_t> bytes;    // Mmp: the file's bytes from `firstOffset` on; Rgba: the pixels
    std::uint32_t firstOffset = 0;      // Mmp: where `bytes` start after the header
    unsigned mip = 0, levels = 0;       // Mmp: the levels decoded
    int width = 0, height = 0;          // Rgba
};

struct Texture {
    sg_image image{};
    sg_view view{};
    std::uint32_t checkedFrame = 0xFFFFFFFFu;
    std::uint32_t usedFrame = 0;
    bool keyed = false;              // window surfaces: the colour key it was converted with
    std::uint32_t key = 0;
    int width = 0, height = 0;
    bool owned = false;              // decoded for a holder without memory (.mmp files, TexturePixels): the
                                     // only copy, kept until the holder goes
    TextureSource source;            // owned: what it was decoded from, to make it again on a new device
};

std::unordered_map<const void*, Texture> g_textures;   // by surface holder: textures with mip levels
std::unordered_map<const void*, Texture> g_windows;    // by surface holder: window surfaces
std::vector<sg_view> g_retiredViews;                   // replaced during the frame: destroyed at Present
std::vector<sg_image> g_retiredImages;
std::unordered_map<std::uint32_t, sg_sampler> g_samplers;
Texture g_white;
sg_sampler g_linear{};
int g_failures = 0;
constexpr std::uint32_t kUnusedFrames = 600;   // entries unused this long are dropped

// One channel of an RGB mask layout: mask, shift of its lowest bit, width.
struct Channel {
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;
};
Channel MakeChannel(std::uint32_t mask) {
    Channel c;
    c.mask = mask;
    if (!mask) return c;
    while (!((mask >> c.shift) & 1)) ++c.shift;
    while (c.shift + c.bits < 32 && ((mask >> (c.shift + c.bits)) & 1)) ++c.bits;
    return c;
}
std::uint8_t Expand(std::uint32_t pixel, const Channel& c, std::uint8_t missing) {
    if (!c.bits) return missing;
    std::uint32_t v = (pixel & c.mask) >> c.shift;
    if (c.bits >= 8) return static_cast<std::uint8_t>(v >> (c.bits - 8));
    std::uint32_t out = 0;
    for (int filled = 0; filled < 8; filled += c.bits) out |= (v << (8 - c.bits)) >> filled;
    return static_cast<std::uint8_t>(out);
}

// A 5:6:5 colour as RGBA8.
void Colour565(std::uint16_t c, std::uint8_t* out) {
    int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
    out[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
    out[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
    out[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
    out[3] = 255;
}

// One DXT colour block (8 bytes) into a 4x4 RGBA8 tile. `alphaBlock` (DXT1 only) allows the
// three-colour mode with transparent black.
void ColourBlock(const std::uint8_t* block, std::uint8_t tile[16][4], bool dxt1) {
    std::uint16_t c0 = static_cast<std::uint16_t>(block[0] | (block[1] << 8));
    std::uint16_t c1 = static_cast<std::uint16_t>(block[2] | (block[3] << 8));
    std::uint8_t palette[4][4];
    Colour565(c0, palette[0]);
    Colour565(c1, palette[1]);
    for (int ch = 0; ch < 3; ++ch) {
        if (!dxt1 || c0 > c1) {
            palette[2][ch] = static_cast<std::uint8_t>((2 * palette[0][ch] + palette[1][ch]) / 3);
            palette[3][ch] = static_cast<std::uint8_t>((palette[0][ch] + 2 * palette[1][ch]) / 3);
        } else {
            palette[2][ch] = static_cast<std::uint8_t>((palette[0][ch] + palette[1][ch]) / 2);
            palette[3][ch] = 0;
        }
    }
    palette[2][3] = 255;
    palette[3][3] = (!dxt1 || c0 > c1) ? 255 : 0;
    std::uint32_t bits = block[4] | (block[5] << 8) | (block[6] << 16) | (static_cast<std::uint32_t>(block[7]) << 24);
    for (int i = 0; i < 16; ++i) std::memcpy(tile[i], palette[(bits >> (2 * i)) & 3], 4);
}

// DXT1..5 blocks ("DXTn" as the .mmp header's format id) into RGBA8, packed rows of blocks.
bool DecodeDxtBlocks(const void* data, int width, int height, std::uint32_t fourcc, std::vector<std::uint8_t>& out) {
    char n = (fourcc & 0xFFFFFF) == 0x545844 ? static_cast<char>(fourcc >> 24) : 0;   // "DXT"
    int kind = n == '1' ? 1 : (n == '2' || n == '3') ? 3 : (n == '4' || n == '5') ? 5 : 0;
    if (!kind) return false;
    int blockBytes = kind == 1 ? 8 : 16;
    int blocksX = (width + 3) / 4, blocksY = (height + 3) / 4;
    int rowPitch = blocksX * blockBytes;
    out.assign(static_cast<std::size_t>(width) * height * 4, 0);
    const auto* base = static_cast<const std::uint8_t*>(data);
    for (int by = 0; by < blocksY; ++by) {
        for (int bx = 0; bx < blocksX; ++bx) {
            const std::uint8_t* block = base + static_cast<std::size_t>(by) * rowPitch + static_cast<std::size_t>(bx) * blockBytes;
            std::uint8_t tile[16][4];
            ColourBlock(kind == 1 ? block : block + 8, tile, kind == 1);
            if (kind == 3) {
                for (int i = 0; i < 16; ++i) {
                    int a = (block[i / 2] >> ((i & 1) * 4)) & 15;
                    tile[i][3] = static_cast<std::uint8_t>(a * 17);
                }
            } else if (kind == 5) {
                int a0 = block[0], a1 = block[1];
                int alpha[8] = {a0, a1};
                for (int k = 2; k < 8; ++k) {
                    if (a0 > a1) alpha[k] = ((8 - k) * a0 + (k - 1) * a1) / 7;
                    else if (k < 6) alpha[k] = ((6 - k) * a0 + (k - 1) * a1) / 5;
                    else alpha[k] = k == 6 ? 0 : 255;
                }
                std::uint64_t bits = 0;
                for (int k = 0; k < 6; ++k) bits |= static_cast<std::uint64_t>(block[2 + k]) << (8 * k);
                for (int i = 0; i < 16; ++i) tile[i][3] = static_cast<std::uint8_t>(alpha[(bits >> (3 * i)) & 7]);
            }
            for (int i = 0; i < 16; ++i) {
                int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
                if (x < width && y < height) std::memcpy(&out[(static_cast<std::size_t>(y) * width + x) * 4], tile[i], 4);
            }
        }
    }
    return true;
}

// A holder's memory as RGBA8 rows (after GdiFlush: GDI may still hold drawing for it). With
// `keyed`, pixels whose colour bits equal `key`'s (a DirectDraw source colour key) come out
// transparent.
bool Convert(const SurfaceMemory& m, std::vector<std::uint8_t>& out, bool keyed = false, std::uint32_t key = 0) {
    GdiFlush();
    int bytes = m.bitsPerPixel / 8;
    if (!m.bits || m.width <= 0 || m.height <= 0 || bytes < 2 || bytes > 4) return false;
    Channel r = MakeChannel(m.redMask), g = MakeChannel(m.greenMask), b = MakeChannel(m.blueMask);
    Channel a = MakeChannel(m.alphaMask);
    std::uint32_t colourBits = m.redMask | m.greenMask | m.blueMask;
    std::uint32_t keyBits = key & colourBits;
    int width = m.width, height = m.height;
    out.resize(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(m.bits) + static_cast<std::size_t>(y) * m.pitch;
        std::uint8_t* o = &out[static_cast<std::size_t>(y) * width * 4];
        for (int x = 0; x < width; ++x) {
            std::uint32_t pixel = 0;
            std::memcpy(&pixel, row + x * bytes, static_cast<std::size_t>(bytes));
            o[x * 4 + 0] = Expand(pixel, r, 0);
            o[x * 4 + 1] = Expand(pixel, g, 0);
            o[x * 4 + 2] = Expand(pixel, b, 0);
            o[x * 4 + 3] = keyed ? ((pixel & colourBits) == keyBits ? 0 : 255) : Expand(pixel, a, 255);
        }
    }
    return true;
}

// An RGBA8 image from `levels` (level 0 first); levels not given are box-filtered from the last one,
// down to 1x1.
sg_image MakeImage(int width, int height, std::vector<std::vector<std::uint8_t>> levels, const char* label) {
    int w = width, h = height;
    for (std::size_t i = 1; i < levels.size(); ++i) {
        w = w > 1 ? w / 2 : 1;
        h = h > 1 ? h / 2 : 1;
    }
    while ((w > 1 || h > 1) && levels.size() < SG_MAX_MIPMAPS) {
        int nw = w > 1 ? w / 2 : 1, nh = h > 1 ? h / 2 : 1;
        const std::vector<std::uint8_t>& src = levels.back();
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
        levels.push_back(std::move(dst));
        w = nw;
        h = nh;
    }
    sg_image_desc desc{};
    desc.width = width;
    desc.height = height;
    desc.num_mipmaps = static_cast<int>(levels.size());
    desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    for (std::size_t i = 0; i < levels.size(); ++i) desc.data.mip_levels[i] = sg_range{levels[i].data(), levels[i].size()};
    desc.label = label;
    CountImage(ImageUse::Texture, true);
    return sg_make_image(&desc);
}

// A texture view of `image`; none when the image could not be made (sokol's validation would
// dereference the missing image).
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
        g_white.image = MakeImage(1, 1, {std::vector<std::uint8_t>(4, 255)}, "ei-white");
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

// The holders' pixels in memory the new renderer owns (render::NewSurfaceMemory).
std::unordered_map<const void*, SurfaceMemory> g_memory;   // by holder
// The memory as the window texture was last made from it: a window is converted again only when its
// memory differs (the game's GDI drawing and its pixel code both land in that memory, so a
// comparison sees every writer).
using Snapshots = std::unordered_map<const void*, std::vector<std::uint8_t>>;
Snapshots g_snapshots;          // by holder: window surfaces
Snapshots g_textureSnapshots;   // by holder: textures (the save / load screen's pictures)

std::size_t MemoryBytes(const SurfaceMemory& m) {
    return static_cast<std::size_t>(m.pitch) * static_cast<std::size_t>(m.height);
}

// The memory of `holder` differs from its snapshot in `snapshots` (GdiFlush first: GDI may still
// hold drawing for it).
bool MemoryChanged(const Snapshots& snapshots, const void* holder, const SurfaceMemory& m) {
    GdiFlush();
    auto it = snapshots.find(holder);
    if (it == snapshots.end() || it->second.size() != MemoryBytes(m)) return true;
    return std::memcmp(it->second.data(), m.bits, it->second.size()) != 0;
}

// Keeps the memory of `holder` as its texture was just made from it (`made`), or forgets it when the
// texture could not be made.
void TakeSnapshot(Snapshots& snapshots, const void* holder, const SurfaceMemory& m, bool made) {
    if (!made) {
        snapshots.erase(holder);
        return;
    }
    auto& snapshot = snapshots[holder];
    snapshot.resize(MemoryBytes(m));
    std::memcpy(snapshot.data(), m.bits, snapshot.size());
}

// Reads a texture whose pixels are in the renderer's memory (one level from the memory, the others
// box-filtered) into `t`.
void ReadClientTexture(Texture& t, const void* holder, const SurfaceMemory& c) {
    std::vector<std::uint8_t> pixels;
    bool ok = Convert(c, pixels);
    TakeSnapshot(g_textureSnapshots, holder, c, ok);
    if (!ok) {
        if (g_failures++ < 10)
            ei::Log("render: cannot read the memory of texture holder %p (%d bits)", holder, c.bitsPerPixel);
        return;
    }
    std::vector<std::vector<std::uint8_t>> levels;
    levels.push_back(std::move(pixels));
    t.image = MakeImage(c.width, c.height, std::move(levels), "ei-client-texture");
    t.view = MakeTextureView(t.image);
    if (!t.view.id || sg_query_view_state(t.view) != SG_RESOURCESTATE_VALID) {
        if (t.view.id) sg_destroy_view(t.view);
        if (t.image.id) sg_destroy_image(t.image);
        t.image = sg_image{};
        t.view = sg_view{};
    }
}

// Reads a window surface (one level; keyed) from its memory into `t`.
void ReadWindow(Texture& t, const void* holder, const SurfaceMemory& m, bool keyed, std::uint32_t key) {
    std::vector<std::uint8_t> pixels;
    bool ok = Convert(m, pixels, keyed, key);
    TakeSnapshot(g_snapshots, holder, m, ok);
    t.keyed = keyed;
    t.key = key;
    if (!ok) {
        if (g_failures++ < 10) ei::Log("render: cannot read window holder %p (%d bits)", holder, m.bitsPerPixel);
        return;
    }
    t.width = m.width;
    t.height = m.height;
    sg_image_desc d{};
    d.width = t.width;
    d.height = t.height;
    d.num_mipmaps = 1;
    d.pixel_format = SG_PIXELFORMAT_RGBA8;
    d.data.mip_levels[0] = sg_range{pixels.data(), pixels.size()};
    d.label = "ei-window-surface";
    CountImage(ImageUse::Window, true);
    t.image = sg_make_image(&d);
    t.view = MakeTextureView(t.image);
    if (!t.view.id || sg_query_view_state(t.view) != SG_RESOURCESTATE_VALID) {
        if (t.view.id) sg_destroy_view(t.view);
        if (t.image.id) sg_destroy_image(t.image);
        t.image = sg_image{};
        t.view = sg_view{};
    }
}

void Remake(Texture& t);

} // namespace

sg_view SurfaceView(const void* holder) {
    auto memory = g_memory.find(holder);
    std::uint32_t frame = FrameNumber();
    if (memory == g_memory.end()) {
        // Nothing to read: what was decoded for the holder (its .mmp file), if anything.
        auto decoded = holder ? g_textures.find(holder) : g_textures.end();
        if (decoded != g_textures.end() && !decoded->second.view.id && decoded->second.owned &&
            decoded->second.checkedFrame != frame) {
            // Lost with the old device (DropTextures): made again from what it was decoded from,
            // tried once a frame.
            decoded->second.checkedFrame = frame;
            Remake(decoded->second);
        }
        if (decoded == g_textures.end() || !decoded->second.view.id) return White().view;
        decoded->second.usedFrame = frame;
        return decoded->second.view;
    }
    Texture& t = g_textures[holder];
    t.usedFrame = frame;
    if (t.checkedFrame == frame) return t.view.id ? t.view : White().view;
    // Textures in the renderer's memory are checked once a frame by comparing that memory with what
    // they were made from.
    bool reread = !t.view.id || MemoryChanged(g_textureSnapshots, holder, memory->second);
    t.checkedFrame = frame;
    if (reread) {
        Retire(t);
        ReadClientTexture(t, holder, memory->second);
    }
    return t.view.id ? t.view : White().view;
}

// The entry of `holder` from decoded levels, keyed by the holder: the texture until the holder goes
// (HolderGone), `source` kept to make it again on a new device. Not for a holder with memory of the
// renderer's: that memory is what the texture shows.
bool StoreDecoded(const void* holder, int width, int height, std::vector<std::vector<std::uint8_t>> levels,
                  const char* label, TextureSource source) {
    if (!holder || g_memory.count(holder)) return false;
    Texture& t = g_textures[holder];
    t.usedFrame = FrameNumber();
    Retire(t);
    t.image = MakeImage(width, height, std::move(levels), label);
    t.view = MakeTextureView(t.image);
    if (!t.view.id) {
        if (t.image.id) sg_destroy_image(t.image);
        t.image = sg_image{};
    }
    t.checkedFrame = FrameNumber();
    t.owned = true;
    t.source = std::move(source);
    return t.view.id != 0;
}

bool TextureFromPixels(const void* holder, const void* pixels, int width, int height, int pitch, int bitsPerPixel,
                       std::uint32_t redMask, std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask) {
    int bytes = bitsPerPixel / 8;
    if (width <= 0 || height <= 0 || (bytes != 2 && bytes != 4) || pitch < width * bytes) return false;
    if (g_memory.count(holder)) return false;   // read from its memory (SurfaceView)
    Channel r = MakeChannel(redMask), g = MakeChannel(greenMask), b = MakeChannel(blueMask), a = MakeChannel(alphaMask);
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    for (int y = 0; y < height; ++y) {
        const auto* row = static_cast<const std::uint8_t*>(pixels) + static_cast<std::size_t>(y) * pitch;
        for (int x = 0; x < width; ++x) {
            std::uint32_t pixel = 0;
            std::memcpy(&pixel, row + static_cast<std::size_t>(x) * bytes, static_cast<std::size_t>(bytes));
            std::uint8_t* o = &rgba[(static_cast<std::size_t>(y) * width + x) * 4];
            o[0] = Expand(pixel, r, 0);
            o[1] = Expand(pixel, g, 0);
            o[2] = Expand(pixel, b, 0);
            o[3] = Expand(pixel, a, 255);
        }
    }
    TextureSource source;   // the only copy (StoreDecoded): kept for a new device
    source.kind = TextureSource::Rgba;
    source.bytes = rgba;
    source.width = width;
    source.height = height;
    std::vector<std::vector<std::uint8_t>> levels;
    levels.push_back(std::move(rgba));
    return StoreDecoded(holder, width, height, std::move(levels), "ei-cpu-texture", std::move(source));
}

namespace {

// The .mmp header (game.exe's texture files; src/game/texture.h MmpHeader): size, mip count,
// format id ("DXTn" FourCCs or a 16 / 32-bit RGB id), bits per pixel, then mask / shift / bits for
// alpha, red, green and blue, and the size of an extra block that follows the header.
struct MmpFile {
    std::uint32_t magic, width, height, mipCount, format, bitsPerPixel;
    std::uint32_t alphaMask, alphaShift, alphaBits;
    std::uint32_t redMask, redShift, redBits;
    std::uint32_t greenMask, greenShift, greenBits;
    std::uint32_t blueMask, blueShift, blueBits;
    std::uint32_t extraSize;
};
static_assert(sizeof(MmpFile) == 0x4C);

// The offset of a mip level's pixels after the header, as the game computes it (006908E0, 32-bit
// arithmetic: the extra block plus a geometric sum of the larger levels).
std::uint32_t MipOffset(const MmpFile& h, unsigned mip) {
    if (mip == 0) return h.extraSize;
    std::uint32_t levelBytes = (h.bitsPerPixel * h.height * h.width) >> 3;
    std::uint32_t scale = 1u << ((2 * mip) & 31);
    return levelBytes * (scale - 1) * 4 / (scale * 3) + h.extraSize;
}

// One level of the file as RGBA8.
bool DecodeMmpLevel(const MmpFile& h, const std::uint8_t* pixels, int width, int height, std::vector<std::uint8_t>& out) {
    if ((h.format & 0xFFFFFF) == 0x545844) return DecodeDxtBlocks(pixels, width, height, h.format, out);   // "DXT"
    int bytes = static_cast<int>(h.bitsPerPixel / 8);
    // RGB with its masks in the header; "PNT2" / "PNT3" (item pictures, a packed format the game
    // decodes itself) and anything else are not decoded.
    if ((bytes != 2 && bytes != 4) || !h.redMask || !h.greenMask || !h.blueMask) return false;
    Channel r = MakeChannel(h.redMask), g = MakeChannel(h.greenMask), b = MakeChannel(h.blueMask);
    Channel a = MakeChannel(h.alphaMask);
    out.resize(static_cast<std::size_t>(width) * height * 4);
    for (int i = 0; i < width * height; ++i) {
        std::uint32_t pixel = 0;
        std::memcpy(&pixel, pixels + static_cast<std::size_t>(i) * bytes, static_cast<std::size_t>(bytes));
        std::uint8_t* o = &out[static_cast<std::size_t>(i) * 4];
        o[0] = Expand(pixel, r, 0);
        o[1] = Expand(pixel, g, 0);
        o[2] = Expand(pixel, b, 0);
        o[3] = Expand(pixel, a, 255);
    }
    return true;
}

// The bytes a level of `width` x `height` takes in the file.
std::uint32_t MmpLevelBytes(const MmpFile& h, int width, int height) {
    if ((h.format & 0xFFFFFF) == 0x545844) {   // "DXT": 8 bytes a 4x4 block for DXT1, else 16
        auto blocks = static_cast<std::uint32_t>((width + 3) / 4) * static_cast<std::uint32_t>((height + 3) / 4);
        return blocks * ((h.format >> 24) == '1' ? 8u : 16u);
    }
    return static_cast<std::uint32_t>(width) * static_cast<std::uint32_t>(height) * (h.bitsPerPixel / 8);
}

// The levels from `mip` on (`levels` of them, at least one) as RGBA8. `pixels` holds the file's bytes
// after the header from offset `pixelsOffset` on; the bytes the decoded levels take go to `*first`
// .. `*end`.
bool DecodeMmp(const MmpFile& h, const std::uint8_t* pixels, std::uint32_t pixelsOffset, unsigned mip, unsigned levels,
               int* width, int* height, std::vector<std::vector<std::uint8_t>>& decoded, std::uint32_t* first,
               std::uint32_t* end) {
    *width = *height = 0;
    *first = 0xFFFFFFFFu;
    *end = 0;
    for (unsigned m = mip; m < mip + (levels ? levels : 1) && m < h.mipCount && decoded.size() < SG_MAX_MIPMAPS; ++m) {
        int w = static_cast<int>(h.width >> (m & 31)), hh = static_cast<int>(h.height >> (m & 31));
        if (w < 1) w = 1;
        if (hh < 1) hh = 1;
        std::uint32_t offset = MipOffset(h, m);
        if (offset < pixelsOffset) break;
        std::vector<std::uint8_t> level;
        if (!DecodeMmpLevel(h, pixels + (offset - pixelsOffset), w, hh, level)) break;
        if (m == mip) {
            *width = w;
            *height = hh;
        }
        decoded.push_back(std::move(level));
        std::uint32_t levelEnd = offset + MmpLevelBytes(h, w, hh);
        if (offset < *first) *first = offset;
        if (levelEnd > *end) *end = levelEnd;
    }
    return !decoded.empty() && *width > 0 && *height > 0;
}

int g_mmpFailures = 0;

} // namespace

bool TextureFromMmpData(const void* holder, const void* data, unsigned mip, unsigned levels) {
    if (!holder || !data) return false;
    const auto& h = *static_cast<const MmpFile*>(data);
    const auto* base = static_cast<const std::uint8_t*>(data) + sizeof(MmpFile);
    if (h.width == 0 || h.height == 0 || h.width > 8192 || h.height > 8192 || h.mipCount == 0 || h.mipCount > 16) {
        if (g_mmpFailures++ < 10) ei::Log("render: .mmp header not understood; the texture is not decoded");
        return false;
    }
    std::vector<std::vector<std::uint8_t>> decoded;
    int width = 0, height = 0;
    std::uint32_t first = 0, end = 0;
    if (!DecodeMmp(h, base, 0, mip, levels, &width, &height, decoded, &first, &end)) {
        if (g_mmpFailures++ < 10)
            ei::Log("render: .mmp format %08X (%u bits) not decoded",
                    static_cast<unsigned>(h.format), static_cast<unsigned>(h.bitsPerPixel));
        return false;
    }
    TextureSource source;   // the only copy (StoreDecoded): the file's levels kept for a new device
    source.kind = TextureSource::Mmp;
    source.header.assign(static_cast<const std::uint8_t*>(data), base);
    source.bytes.assign(base + first, base + end);
    source.firstOffset = first;
    source.mip = mip;
    source.levels = levels;
    return StoreDecoded(holder, width, height, std::move(decoded), "ei-mmp-texture", std::move(source));
}

namespace {

// An owned texture's image made again from its source (on a new device).
void Remake(Texture& t) {
    const TextureSource& s = t.source;
    std::vector<std::vector<std::uint8_t>> levels;
    int width = 0, height = 0;
    if (s.kind == TextureSource::Mmp && s.header.size() == sizeof(MmpFile)) {
        MmpFile h{};
        std::memcpy(&h, s.header.data(), sizeof(h));
        std::uint32_t first = 0, end = 0;
        if (!DecodeMmp(h, s.bytes.data(), s.firstOffset, s.mip, s.levels, &width, &height, levels, &first, &end)) return;
    } else if (s.kind == TextureSource::Rgba) {
        width = s.width;
        height = s.height;
        levels.push_back(s.bytes);
    } else {
        return;
    }
    t.image = MakeImage(width, height, std::move(levels), "ei-texture-again");
    t.view = MakeTextureView(t.image);
    if (!t.view.id) {
        if (t.image.id) sg_destroy_image(t.image);
        t.image = sg_image{};
    }
}

} // namespace

namespace {
std::unordered_map<const void*, Picture> g_pictures;   // by surface holder
} // namespace

bool PictureFromMmpData(const void* holder, const void* data) {
    g_pictures.erase(holder);
    const auto& h = *static_cast<const MmpFile*>(data);
    if (h.width == 0 || h.height == 0 || h.width > 4096 || h.height > 4096) return false;
    Picture p;
    p.width = static_cast<int>(h.width);
    p.height = static_cast<int>(h.height);
    const auto* base = static_cast<const std::uint8_t*>(data) + sizeof(MmpFile);
    if (!DecodeMmpLevel(h, base + MipOffset(h, 0), p.width, p.height, p.rgba)) {
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
    // The texture's counted surface holder (CDXSurface).
    if (!texture) return White().view;
    return SurfaceView(static_cast<const CTexture*>(texture)->surface_);
}

sg_view WindowSurfaceView(const void* holder, bool keyed, std::uint32_t key, int* width, int* height) {
    *width = *height = 0;
    auto memory = g_memory.find(holder);
    if (memory == g_memory.end()) return sg_view{};
    std::uint32_t frame = FrameNumber();
    Texture& t = g_windows[holder];
    t.usedFrame = frame;
    // Window surfaces are checked at every blit (the game may redraw one between two blits of a
    // frame) by comparing their memory with what they were made from.
    bool changed = MemoryChanged(g_snapshots, holder, memory->second);
    bool reread = !t.view.id || t.keyed != keyed || (keyed && t.key != key) || changed;
    t.checkedFrame = frame;
    if (reread) {
        Retire(t);
        ReadWindow(t, holder, memory->second, keyed, key);
    }
    *width = t.width;
    *height = t.height;
    return t.view;
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

namespace {
sg_wrap Wrap(std::uint8_t address) {
    switch (address) {   // D3DTEXTUREADDRESS
    case 2: return SG_WRAP_MIRRORED_REPEAT;
    case 3: return SG_WRAP_CLAMP_TO_EDGE;
    case 4: return SG_WRAP_CLAMP_TO_BORDER;
    default: return SG_WRAP_REPEAT;
    }
}
} // namespace

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

void RetireTextures() {
    for (sg_view v : g_retiredViews) sg_destroy_view(v);
    for (sg_image i : g_retiredImages) sg_destroy_image(i);
    g_retiredViews.clear();
    g_retiredImages.clear();
    std::uint32_t frame = FrameNumber();
    for (auto* map : {&g_textures, &g_windows}) {
        for (auto it = map->begin(); it != map->end();) {
            // (A texture decoded for a holder with nothing to read again stays until the holder goes.)
            if (!it->second.owned && frame - it->second.usedFrame > kUnusedFrames) {
                if (it->second.image.id) {
                    sg_destroy_view(it->second.view);
                    sg_destroy_image(it->second.image);
                }
                it = map->erase(it);
            } else {
                ++it;
            }
        }
    }
}

void ForgetHolder(const void* holder) {
    for (auto* map : {&g_textures, &g_windows}) {
        auto it = map->find(holder);
        if (it == map->end()) continue;
        Retire(it->second);
        map->erase(it);
    }
}

void DestroyTextures() {
    RetireTextures();
    for (auto* map : {&g_textures, &g_windows}) {
        for (auto& entry : *map) {
            if (entry.second.image.id) {
                sg_destroy_view(entry.second.view);
                sg_destroy_image(entry.second.image);
            }
        }
        map->clear();
    }
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
    // The owned textures stay without their images (made again from their sources at their next
    // use); everything else is read again from its memory when it is next drawn.
    std::unordered_map<const void*, Texture> owned;
    for (auto& entry : g_textures) {
        Texture& t = entry.second;
        if (!t.owned || t.source.kind == TextureSource::None) continue;
        if (t.image.id) {
            sg_destroy_view(t.view);
            sg_destroy_image(t.image);
        }
        t.image = sg_image{};
        t.view = sg_view{};
        t.checkedFrame = 0xFFFFFFFFu;
        ++*kept;
        *keptBytes += t.source.bytes.size();
        owned.emplace(entry.first, std::move(t));
    }
    DestroyTextures();
    g_textures = std::move(owned);
    g_snapshots.clear();
    g_textureSnapshots.clear();
}

} // namespace render::detail

namespace render {

bool TextureFromMmp(const void* holder, const void* data, unsigned mip, unsigned levels) {
    if (!holder || !data || !Active() || !detail::Started()) return false;
    detail::FpuGuard fpu;
    return detail::TextureFromMmpData(holder, data, mip, levels);
}

} // namespace render

namespace render {

const SurfaceMemory* NewSurfaceMemory(const void* holder, int width, int height, int bitsPerPixel, std::uint32_t redMask,
                                      std::uint32_t greenMask, std::uint32_t blueMask, std::uint32_t alphaMask) {
    if (!holder || width <= 0 || height <= 0 || (bitsPerPixel != 16 && bitsPerPixel != 32)) return nullptr;
    FreeSurfaceMemory(holder);
    // A top-down DIB section with the channels at the format's masks; rows are DWORD-aligned.
    struct {
        BITMAPINFOHEADER header;
        DWORD masks[3];
    } info{};
    info.header.biSize = sizeof(BITMAPINFOHEADER);
    info.header.biWidth = width;
    info.header.biHeight = -height;
    info.header.biPlanes = 1;
    info.header.biBitCount = static_cast<WORD>(bitsPerPixel);
    info.header.biCompression = BI_BITFIELDS;
    info.masks[0] = redMask;
    info.masks[1] = greenMask;
    info.masks[2] = blueMask;
    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(nullptr, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) {
        if (bitmap) DeleteObject(bitmap);
        return nullptr;
    }
    SurfaceMemory m;
    m.bits = bits;
    m.bitmap = bitmap;
    m.pitch = ((width * bitsPerPixel / 8) + 3) & ~3;
    m.width = width;
    m.height = height;
    m.bitsPerPixel = bitsPerPixel;
    m.redMask = redMask;
    m.greenMask = greenMask;
    m.blueMask = blueMask;
    m.alphaMask = alphaMask;
    return &(detail::g_memory[holder] = m);
}

const SurfaceMemory* SurfaceMemoryOf(const void* holder) {
    auto it = detail::g_memory.find(holder);
    return it == detail::g_memory.end() ? nullptr : &it->second;
}

void FreeSurfaceMemory(const void* holder) {
    auto it = detail::g_memory.find(holder);
    if (it != detail::g_memory.end()) {
        DeleteObject(static_cast<HBITMAP>(it->second.bitmap));
        detail::g_memory.erase(it);
    }
    detail::g_snapshots.erase(holder);
    detail::g_textureSnapshots.erase(holder);
}

void HolderGone(const void* holder) {
    FreeSurfaceMemory(holder);
    detail::g_pictures.erase(holder);
    // What the renderer made for it (a later holder at the same address starts afresh; a texture
    // decoded for the holder would otherwise outlive it).
    detail::ForgetHolder(holder);
}

void PictureFromMmp(const void* holder, const void* data) {
    if (!holder || !data || !Active()) return;
    detail::FpuGuard fpu;
    detail::PictureFromMmpData(holder, data);
}

} // namespace render
