// Textures as PNG: texture_png.h. The rules (which format comes back as which, premultiplied alpha, the levels) are in
// docs/notes/mods.md, "Textures as PNG".
#include "engine/models/texture_png.h"

#include "lib/bytes.h"
#include "lib/png.h"
#include "lib/toml.h"

#include "miniz/miniz.h"
#include "stb/stb_dxt.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <thread>

namespace texture_png {

namespace {

constexpr std::uint32_t kMmpMagic = 0x504D4D;   // "MMP"
constexpr std::string_view kChunkKeyword = "ei-texture";
constexpr int kMostLevels = 16;                 // the renderer takes no more (TextureFromMmpData)

constexpr MmpFormat Dxt(char digit) {
    return static_cast<MmpFormat>('D' | ('X' << 8) | ('T' << 16) | (static_cast<std::uint32_t>(digit) << 24));
}

// `work(first, last)` over the rows [0, count), cut into runs of `atLeast` rows or more, one a processor at most, the
// runs on threads (the calling one among them). Each row depends on its own texels alone, so the bytes are the same.
template <class Work>
void InRuns(int count, int atLeast, Work work) {
    const int threads = std::clamp(count / std::max(atLeast, 1), 1, std::max(1, static_cast<int>(std::thread::hardware_concurrency())));
    std::vector<std::thread> helpers;
    for (int t = 1; t < threads; ++t) helpers.emplace_back(work, count * t / threads, count * (t + 1) / threads);
    work(0, count / threads);
    for (std::thread& helper : helpers) helper.join();
}

// Fewer texels than these are not worth a thread of their own.
constexpr int kTexelsAThread = 256 * 256;

// How a format's texels are stored.
enum class Storage {
    Masked,   // 16 or 32 bits a texel, the header's channel masks (the RGB formats)
    Pnt3,     // premultiplied 0xAARRGGBB, runs of transparent black packed (render.h's ImageLayout::Pnt3)
    Dxt1,     // 4 x 4 blocks: BC1 (a block's third mode makes its fourth colour transparent black)
    Dxt3,     // BC2: 4-bit alpha a texel, a BC1 colour block
    Dxt5,     // BC3: interpolated alpha, a BC1 colour block
};

struct FormatInfo {
    MmpFormat format = MmpFormat::None;
    std::string_view name;   // in the chunk
    Storage storage = Storage::Masked;
    std::uint32_t bitsPerPixel = 0;
    bool premultiplied = false;   // the texels hold colour x alpha
    std::string_view channels;   // the RGB formats' own channel layout (their masks); empty: any
};

constexpr FormatInfo kFormats[] = {
    {MmpFormat::R5G6B5, "565", Storage::Masked, 16, false, "565"},
    {MmpFormat::A1R5G5B5, "1555", Storage::Masked, 16, false, "1555"},
    {MmpFormat::A4R4G4B4, "4444", Storage::Masked, 16, false, "4444"},
    {MmpFormat::A8R8G8B8, "8888", Storage::Masked, 32, true, "8888"},
    {MmpFormat::Pnt3, "PNT3", Storage::Pnt3, 32, true, {}},
    {Dxt('1'), "DXT1", Storage::Dxt1, 4, false, {}},
    {Dxt('2'), "DXT2", Storage::Dxt3, 8, true, {}},
    {Dxt('3'), "DXT3", Storage::Dxt3, 8, false, {}},
    {Dxt('4'), "DXT4", Storage::Dxt5, 8, true, {}},
    {Dxt('5'), "DXT5", Storage::Dxt5, 8, false, {}},
};

const FormatInfo* InfoOf(MmpFormat format) {
    for (const FormatInfo& info : kFormats)
        if (info.format == format) return &info;
    return nullptr;
}

const FormatInfo* InfoNamed(std::string_view name) {
    for (const FormatInfo& info : kFormats)
        if (info.name == name) return &info;
    return nullptr;
}

// The header's channel masks: alpha, red, green, blue.
struct Channels {
    std::string_view name;
    std::uint32_t alpha = 0, red = 0, green = 0, blue = 0;
};

constexpr Channels kChannels[] = {
    {"none", 0, 0, 0, 0},
    {"565", 0, 0xF800, 0x07E0, 0x001F},
    {"1555", 0x8000, 0x7C00, 0x03E0, 0x001F},
    {"4444", 0xF000, 0x0F00, 0x00F0, 0x000F},
    {"8888", 0xFF000000u, 0xFF0000, 0xFF00, 0xFF},
};

const Channels* ChannelsNamed(std::string_view name) {
    for (const Channels& channels : kChannels)
        if (channels.name == name) return &channels;
    return nullptr;
}

const Channels* ChannelsOf(const MmpHeader& h) {
    for (const Channels& channels : kChannels)
        if (channels.alpha == h.alphaMask && channels.red == h.redMask && channels.green == h.greenMask &&
            channels.blue == h.blueMask)
            return &channels;
    return nullptr;
}

// A mask's lowest bit and its count of bits.
struct Field {
    std::uint32_t mask = 0;
    int shift = 0;
    int bits = 0;
    explicit Field(std::uint32_t m) : mask(m) {
        if (!m) return;
        while (!((m >> shift) & 1)) ++shift;
        while (shift + bits < 32 && ((m >> (shift + bits)) & 1)) ++bits;
    }
};

// The header of a `format` texture with `channels`' masks (each mask's shift and bit count, the alpha's count as the
// alpha kind: every shipped file's fields so).
MmpHeader MakeHeader(const FormatInfo& format, const Channels& channels, int width, int height, std::uint32_t mipCount,
                     std::uint32_t extraSize) {
    MmpHeader h;
    h.magic = kMmpMagic;
    h.width = static_cast<std::uint32_t>(width);
    h.height = static_cast<std::uint32_t>(height);
    h.mipCount = mipCount;
    h.format = format.format;
    h.bitsPerPixel = format.bitsPerPixel;
    const Field a(channels.alpha), r(channels.red), g(channels.green), b(channels.blue);
    h.alphaMask = a.mask;
    h.alphaShift = static_cast<std::uint32_t>(a.shift);
    h.alphaKind = static_cast<std::uint32_t>(a.bits);
    h.redMask = r.mask;
    h.redShift = static_cast<std::uint32_t>(r.shift);
    h.redBits = static_cast<std::uint32_t>(r.bits);
    h.greenMask = g.mask;
    h.greenShift = static_cast<std::uint32_t>(g.shift);
    h.greenBits = static_cast<std::uint32_t>(g.bits);
    h.blueMask = b.mask;
    h.blueShift = static_cast<std::uint32_t>(b.shift);
    h.blueBits = static_cast<std::uint32_t>(b.bits);
    h.extraSize = extraSize;
    return h;
}

// The levels of a `width` x `height` texture down to 1 x 1.
std::uint32_t FullChain(int width, int height) {
    std::uint32_t levels = 1;
    while (width > 1 || height > 1) {
        width = std::max(width / 2, 1);
        height = std::max(height / 2, 1);
        ++levels;
    }
    return levels;
}

// The bytes level `mip` has in the file: bits x width x height / 8 of the level's size (each halved per level, down to
// 0), 32-bit arithmetic: the game's fill and the renderer's read (render_textures.cpp, MmpFileLevelBytes).
std::uint32_t LevelBytes(const MmpHeader& h, unsigned mip) {
    return (h.bitsPerPixel * (h.width >> (mip & 31)) * (h.height >> (mip & 31))) >> 3;
}

int LevelSize(std::uint32_t size, unsigned mip) { return std::max(static_cast<int>(size >> (mip & 31)), 1); }

// ---- Texels in the file ----------------------------------------------------------------------------------------

void DecodeMasked(const MmpHeader& h, const std::uint8_t* bytes, Image& out) {
    const std::size_t size = h.bitsPerPixel / 8;
    const Field r(h.redMask), g(h.greenMask), b(h.blueMask), a(h.alphaMask);
    const std::size_t count = static_cast<std::size_t>(out.width) * out.height;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint32_t texel = 0;
        std::memcpy(&texel, bytes + i * size, size);
        std::uint8_t* o = &out.rgba[i * 4];
        o[0] = static_cast<std::uint8_t>(Widen((texel & r.mask) >> r.shift, r.bits));
        o[1] = static_cast<std::uint8_t>(Widen((texel & g.mask) >> g.shift, g.bits));
        o[2] = static_cast<std::uint8_t>(Widen((texel & b.mask) >> b.shift, b.bits));
        o[3] = static_cast<std::uint8_t>(a.bits ? Widen((texel & a.mask) >> a.shift, a.bits) : 255);
    }
}

void EncodeMasked(const MmpHeader& h, const Image& image, std::uint8_t* bytes) {
    const std::size_t size = h.bitsPerPixel / 8;
    const Field r(h.redMask), g(h.greenMask), b(h.blueMask), a(h.alphaMask);
    const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* t = &image.rgba[i * 4];
        const std::uint32_t texel = Narrow(t[0], r.bits) << r.shift | Narrow(t[1], g.bits) << g.shift |
                                    Narrow(t[2], b.bits) << b.shift | (a.bits ? Narrow(t[3], a.bits) << a.shift : 0);
        std::memcpy(bytes + i * size, &texel, size);
    }
}

// PNT3: a group of four texels, or (a first word of alpha 0 that is not 0) a count of the bytes to leave.
bool DecodePnt3(const std::uint8_t* bytes, std::size_t size, Image& out) {
    const std::size_t count = static_cast<std::size_t>(out.width) * out.height;
    std::size_t at = 0;
    while (at < count && size >= 4) {
        const std::uint32_t word = lib::Load<std::uint32_t>(bytes);
        if ((word >> 24) == 0 && word != 0) {
            at += word / 4;
            bytes += 4;
            size -= 4;
            continue;
        }
        for (int k = 0; k < 4 && at < count && size >= 4; ++k, ++at, bytes += 4, size -= 4) {
            std::uint8_t* o = &out.rgba[at * 4];
            o[0] = bytes[2];
            o[1] = bytes[1];
            o[2] = bytes[0];
            o[3] = bytes[3];
        }
    }
    return true;
}

// Runs of transparent black as counts, every other texel in groups of four (a group never starts with a texel of
// alpha 0 that is not 0: premultiplied texels of alpha 0 are 0).
std::vector<std::uint8_t> EncodePnt3(const Image& image) {
    std::vector<std::uint8_t> out;
    const std::size_t count = static_cast<std::size_t>(image.width) * image.height;
    auto texel = [&](std::size_t i) {
        const std::uint8_t* t = &image.rgba[i * 4];
        return static_cast<std::uint32_t>(t[3]) << 24 | t[0] << 16 | t[1] << 8 | t[2];
    };
    auto put = [&](std::uint32_t word) {
        out.resize(out.size() + 4);
        lib::Store<std::uint32_t>(out.data() + out.size() - 4, word);
    };
    constexpr std::size_t kLongestRun = 0xFFFFFF / 4;   // a count word's alpha byte is 0
    std::size_t at = 0;
    while (at < count) {
        if (texel(at) == 0) {
            std::size_t run = 0;
            while (at + run < count && run < kLongestRun && texel(at + run) == 0) ++run;
            put(static_cast<std::uint32_t>(run * 4));
            at += run;
            continue;
        }
        for (int k = 0; k < 4 && at < count; ++k, ++at) put(texel(at));
    }
    return out;
}

struct Rgba {
    int r = 0, g = 0, b = 0, a = 0;
};

Rgba Expand565(std::uint32_t c) {
    return {static_cast<int>(Widen(c >> 11, 5)), static_cast<int>(Widen((c >> 5) & 0x3F, 6)), static_cast<int>(Widen(c & 0x1F, 5)), 255};
}

// The four colours of a BC1 colour block of the 5:6:5 colours c0 and c1 (the D3D formulas, rounded to nearest): two
// between them, or with c0 not above c1 their mean and transparent black (`fourColours`: BC2 / BC3's blocks, always
// the four).
void ColourPalette(std::uint32_t c0, std::uint32_t c1, bool fourColours, Rgba p[4]) {
    p[0] = Expand565(c0);
    p[1] = Expand565(c1);
    if (fourColours || c0 > c1) {
        p[2] = {(2 * p[0].r + p[1].r + 1) / 3, (2 * p[0].g + p[1].g + 1) / 3, (2 * p[0].b + p[1].b + 1) / 3, 255};
        p[3] = {(p[0].r + 2 * p[1].r + 1) / 3, (p[0].g + 2 * p[1].g + 1) / 3, (p[0].b + 2 * p[1].b + 1) / 3, 255};
    } else {
        p[2] = {(p[0].r + p[1].r + 1) / 2, (p[0].g + p[1].g + 1) / 2, (p[0].b + p[1].b + 1) / 2, 255};
        p[3] = {0, 0, 0, 0};
    }
}

// The eight alphas of a BC3 alpha block: six between a0 and a1, or with a0 not above a1 four and 0 and 255.
void AlphaPalette(int a0, int a1, int p[8]) {
    p[0] = a0;
    p[1] = a1;
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) p[i + 1] = ((7 - i) * a0 + i * a1 + 3) / 7;
    } else {
        for (int i = 1; i < 5; ++i) p[i + 1] = ((5 - i) * a0 + i * a1 + 2) / 5;
        p[6] = 0;
        p[7] = 255;
    }
}

// A BC1 colour block's 16 texels.
void DecodeColourBlock(const std::uint8_t* block, bool fourColours, Rgba texels[16]) {
    Rgba p[4];
    ColourPalette(lib::Load<std::uint16_t>(block), lib::Load<std::uint16_t>(block + 2), fourColours, p);
    const std::uint32_t indices = lib::Load<std::uint32_t>(block + 4);
    for (int i = 0; i < 16; ++i) texels[i] = p[(indices >> (2 * i)) & 3];
}

void DecodeAlpha3(const std::uint8_t* block, Rgba texels[16]) {
    for (int i = 0; i < 16; ++i) texels[i].a = static_cast<int>(Widen((block[i / 2] >> (4 * (i & 1))) & 0xF, 4));
}

void DecodeAlpha5(const std::uint8_t* block, Rgba texels[16]) {
    int p[8];
    AlphaPalette(block[0], block[1], p);
    std::uint64_t indices = 0;
    for (int i = 0; i < 6; ++i) indices |= static_cast<std::uint64_t>(block[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) texels[i].a = p[(indices >> (3 * i)) & 7];
}

void DecodeBlocks(Storage storage, const std::uint8_t* bytes, Image& out) {
    const std::size_t blockBytes = storage == Storage::Dxt1 ? 8 : 16;
    const int across = (out.width + 3) / 4, down = (out.height + 3) / 4;
    for (int by = 0; by < down; ++by)
        for (int bx = 0; bx < across; ++bx) {
            const std::uint8_t* block = bytes + (static_cast<std::size_t>(by) * across + bx) * blockBytes;
            Rgba texels[16];
            if (storage == Storage::Dxt1) {
                DecodeColourBlock(block, false, texels);
            } else {
                DecodeColourBlock(block + 8, true, texels);
                if (storage == Storage::Dxt3) DecodeAlpha3(block, texels);
                else DecodeAlpha5(block, texels);
            }
            for (int i = 0; i < 16; ++i) {
                const int x = bx * 4 + (i & 3), y = by * 4 + i / 4;
                if (x >= out.width || y >= out.height) continue;
                std::uint8_t* o = &out.rgba[(static_cast<std::size_t>(y) * out.width + x) * 4];
                o[0] = static_cast<std::uint8_t>(texels[i].r);
                o[1] = static_cast<std::uint8_t>(texels[i].g);
                o[2] = static_cast<std::uint8_t>(texels[i].b);
                o[3] = static_cast<std::uint8_t>(texels[i].a);
            }
        }
}

// A BC1 block of texels some of which are transparent (alpha under 128): the third mode (first colour not above the
// second), those texels index 3 (transparent black), the others the nearest of the two colours and their mean. The
// colours are stb_dxt's for the opaque texels (the transparent ones given their mean).
void EncodeCutoutBlock(const std::uint8_t texels[64], std::uint8_t out[8]) {
    int sum[3] = {}, opaque = 0;
    for (int i = 0; i < 16; ++i)
        if (texels[i * 4 + 3] >= 128) {
            for (int c = 0; c < 3; ++c) sum[c] += texels[i * 4 + c];
            ++opaque;
        }
    if (opaque == 0) {
        std::fill_n(out, 4, std::uint8_t{0});
        std::fill_n(out + 4, 4, std::uint8_t{0xFF});
        return;
    }
    std::uint8_t filled[64];
    for (int i = 0; i < 16; ++i) {
        const bool keep = texels[i * 4 + 3] >= 128;
        for (int c = 0; c < 3; ++c) filled[i * 4 + c] = keep ? texels[i * 4 + c] : static_cast<std::uint8_t>((sum[c] + opaque / 2) / opaque);
        filled[i * 4 + 3] = 255;
    }
    std::uint8_t four[8];
    stb_compress_dxt_block(four, filled, 0, STB_DXT_HIGHQUAL);
    const std::uint16_t high = lib::Load<std::uint16_t>(four), low = lib::Load<std::uint16_t>(four + 2);   // stb_dxt's first colour is not below its second
    out[0] = four[2];
    out[1] = four[3];
    out[2] = four[0];
    out[3] = four[1];
    const Rgba a = Expand565(low), b = Expand565(high);
    const Rgba p[3] = {a, b, {(a.r + b.r + 1) / 2, (a.g + b.g + 1) / 2, (a.b + b.b + 1) / 2, 255}};
    std::uint32_t indices = 0;
    for (int i = 0; i < 16; ++i) {
        std::uint32_t index = 3;
        if (texels[i * 4 + 3] >= 128) {
            int best = -1;
            for (std::uint32_t k = 0; k < 3; ++k) {
                const int dr = texels[i * 4] - p[k].r, dg = texels[i * 4 + 1] - p[k].g, db = texels[i * 4 + 2] - p[k].b;
                const int d = dr * dr + dg * dg + db * db;
                if (best < 0 || d < best) {
                    best = d;
                    index = k;
                }
            }
        }
        indices |= index << (2 * i);
    }
    lib::Store<std::uint32_t>(out + 4, indices);
}

// The 5:6:5 colour that widens to `rgb` exactly; -1 when none does.
int Exact565(const std::uint8_t* rgb) {
    const std::uint32_t r = Narrow(rgb[0], 5), g = Narrow(rgb[1], 6), b = Narrow(rgb[2], 5);
    if (Widen(r, 5) != rgb[0] || Widen(g, 6) != rgb[1] || Widen(b, 5) != rgb[2]) return -1;
    return static_cast<int>(r << 11 | g << 5 | b);
}

// The colour block whose texels (DecodeColourBlock) are these exactly, its two colours two of theirs: what a decoded
// block gives back, so a PNG of a DXT texture makes its blocks again. Without `fourColours` (BC1) a texel is opaque or
// transparent black; with it the alpha is the block's other half and not looked at. False when there is none.
bool ExactColourBlock(const std::uint8_t texels[64], bool fourColours, std::uint8_t out[8]) {
    int ends[16];
    int endCount = 0;
    bool transparent = false;
    std::uint32_t values[4] = {};   // a block decodes to at most four texel values
    int valueCount = 0;
    for (int i = 0; i < 16; ++i) {
        const std::uint8_t* t = texels + i * 4;
        const std::uint32_t value = lib::Load<std::uint32_t>(t) | (fourColours ? 0xFF000000u : 0u);
        if (std::find(values, values + valueCount, value) == values + valueCount) {
            if (valueCount == 4) return false;
            values[valueCount++] = value;
        }
        if (!fourColours && t[3] != 255) {
            if (t[3] != 0 || t[0] || t[1] || t[2]) return false;
            transparent = true;
            continue;
        }
        const int end = Exact565(t);
        if (end >= 0 && std::find(ends, ends + endCount, end) == ends + endCount) ends[endCount++] = end;
    }
    if (endCount == 0 && transparent) ends[endCount++] = 0;   // all transparent: the third mode of black
    for (int a = 0; a < endCount; ++a)
        for (int b = 0; b < endCount; ++b) {
            const auto c0 = static_cast<std::uint32_t>(ends[a]), c1 = static_cast<std::uint32_t>(ends[b]);
            if (transparent && c0 > c1) continue;   // only the third mode has transparent black
            Rgba p[4];
            ColourPalette(c0, c1, fourColours, p);
            std::uint32_t indices = 0;
            int i = 0;
            for (; i < 16; ++i) {
                const std::uint8_t* t = texels + i * 4;
                std::uint32_t k = 0;
                while (k < 4 && (p[k].r != t[0] || p[k].g != t[1] || p[k].b != t[2] || (!fourColours && p[k].a != t[3]))) ++k;
                if (k == 4) break;
                indices |= k << (2 * i);
            }
            if (i < 16) continue;
            lib::Store<std::uint16_t>(out, static_cast<std::uint16_t>(c0));
            lib::Store<std::uint16_t>(out + 2, static_cast<std::uint16_t>(c1));
            lib::Store<std::uint32_t>(out + 4, indices);
            return true;
        }
    return false;
}

// The BC3 alpha block whose alphas are these exactly, its two ends the largest and smallest of them (or of those
// neither 0 nor 255). False when there is none.
bool ExactAlphaBlock(const std::uint8_t texels[64], std::uint8_t out[8]) {
    int low = 255, high = 0, innerLow = 255, innerHigh = 0;
    for (int i = 0; i < 16; ++i) {
        const int a = texels[i * 4 + 3];
        low = std::min(low, a);
        high = std::max(high, a);
        if (a != 0 && a != 255) {
            innerLow = std::min(innerLow, a);
            innerHigh = std::max(innerHigh, a);
        }
    }
    if (innerLow > innerHigh) innerLow = innerHigh = 0;
    const int tries[2][2] = {{high, low}, {innerLow, innerHigh}};
    for (const auto& ends : tries) {
        if (&ends == &tries[0] && high <= low) continue;   // the first try is the six-between kind
        int p[8];
        AlphaPalette(ends[0], ends[1], p);
        std::uint64_t indices = 0;
        int i = 0;
        for (; i < 16; ++i) {
            std::uint64_t k = 0;
            while (k < 8 && p[k] != texels[i * 4 + 3]) ++k;
            if (k == 8) break;
            indices |= k << (3 * i);
        }
        if (i < 16) continue;
        out[0] = static_cast<std::uint8_t>(ends[0]);
        out[1] = static_cast<std::uint8_t>(ends[1]);
        for (int b = 0; b < 6; ++b) out[2 + b] = static_cast<std::uint8_t>(indices >> (8 * b));
        return true;
    }
    return false;
}

// A block of the texels: the block that gives them back exactly when there is one (a DXT texture's PNG, as exported),
// else stb_dxt's (a BC1 block with transparent texels: EncodeCutoutBlock).
void EncodeBlock(Storage storage, const std::uint8_t texels[64], std::uint8_t* out) {
    std::uint8_t opaque[64];
    std::copy_n(texels, 64, opaque);
    for (int i = 0; i < 16; ++i) opaque[i * 4 + 3] = 255;   // stb_dxt wants a constant alpha for a colour block
    switch (storage) {
    case Storage::Dxt1: {
        if (ExactColourBlock(texels, false, out)) break;
        bool cutout = false;
        for (int i = 0; i < 16; ++i) cutout = cutout || texels[i * 4 + 3] < 128;
        if (cutout) EncodeCutoutBlock(texels, out);
        else stb_compress_dxt_block(out, opaque, 0, STB_DXT_HIGHQUAL);
        break;
    }
    case Storage::Dxt3:
        for (int i = 0; i < 8; ++i)
            out[i] = static_cast<std::uint8_t>(Narrow(texels[i * 8 + 3], 4) | Narrow(texels[i * 8 + 7], 4) << 4);
        if (!ExactColourBlock(texels, true, out + 8)) stb_compress_dxt_block(out + 8, opaque, 0, STB_DXT_HIGHQUAL);
        break;
    default: {
        const bool alpha = ExactAlphaBlock(texels, out), colour = ExactColourBlock(texels, true, out + 8);
        if (alpha && colour) break;
        std::uint8_t compressed[16];
        stb_compress_dxt_block(compressed, texels, 1, STB_DXT_HIGHQUAL);
        if (!alpha) std::copy_n(compressed, 8, out);
        if (!colour) std::copy_n(compressed + 8, 8, out + 8);
        break;
    }
    }
}

// The level's blocks (a partial block's texels from the level's last row and column), a large level's rows of blocks
// on threads.
std::vector<std::uint8_t> EncodeBlocks(Storage storage, const Image& image) {
    const std::size_t blockBytes = storage == Storage::Dxt1 ? 8 : 16;
    const int across = (image.width + 3) / 4, down = (image.height + 3) / 4;
    std::vector<std::uint8_t> out(static_cast<std::size_t>(across) * down * blockBytes);
    const auto rows = [&](int first, int last) {
        std::uint8_t texels[64];
        for (int by = first; by < last; ++by)
            for (int bx = 0; bx < across; ++bx) {
                for (int i = 0; i < 16; ++i) {
                    const int x = std::min(bx * 4 + (i & 3), image.width - 1), y = std::min(by * 4 + i / 4, image.height - 1);
                    std::copy_n(&image.rgba[(static_cast<std::size_t>(y) * image.width + x) * 4], 4, texels + i * 4);
                }
                EncodeBlock(storage, texels, out.data() + (static_cast<std::size_t>(by) * across + bx) * blockBytes);
            }
    };
    InRuns(down, kTexelsAThread / 16 / std::max(across, 1), rows);
    return out;
}

// ---- The settings chunk ----------------------------------------------------------------------------------------

struct Settings {
    const FormatInfo* format = nullptr;
    const Channels* channels = nullptr;
    std::uint32_t levels = 0;    // 0: every level down to 1 x 1
    std::vector<std::uint8_t> extra;
};

std::string HexOf(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string text;
    for (std::uint8_t b : bytes) {
        text.push_back(kDigits[b >> 4]);
        text.push_back(kDigits[b & 15]);
    }
    return text;
}

bool FromHex(std::string_view text, std::vector<std::uint8_t>* bytes) {
    auto digit = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
    if (text.size() % 2) return false;
    bytes->clear();
    for (std::size_t i = 0; i < text.size(); i += 2) {
        const int high = digit(text[i]), low = digit(text[i + 1]);
        if (high < 0 || low < 0) return false;
        bytes->push_back(static_cast<std::uint8_t>(high << 4 | low));
    }
    return true;
}

std::string WriteSettings(const Settings& s) {
    toml::Writer w;
    w.Comment("ei-texture: the .mmp this PNG is made into (docs/notes/mods.md, \"Textures as PNG\")");
    w.Pair("format", toml::String(s.format->name));
    w.Pair("channels", toml::String(s.channels->name));
    if (s.format->storage != Storage::Pnt3) w.Pair("levels", toml::Int(s.levels));
    if (!s.extra.empty()) w.Pair("extra", toml::String(HexOf(s.extra)));
    return w.text;
}

bool ReadSettings(std::string_view text, Settings* s, std::string* error) {
    toml::Document document;
    if (!document.Parse(text, "ei-texture")) {
        *error = document.Error();
        return false;
    }
    const toml::Value root = document.Root();
    s->format = InfoNamed(root["format"].String());
    if (!s->format) {
        *error = "ei-texture: no format it makes: \"" + std::string(root["format"].String()) + "\"";
        return false;
    }
    const std::string_view own = s->format->channels.empty() ? std::string_view("none") : s->format->channels;
    s->channels = ChannelsNamed(root["channels"].IsString() ? root["channels"].String() : own);
    if (!s->channels) {
        *error = "ei-texture: channels \"" + std::string(root["channels"].String()) + "\" unknown";
        return false;
    }
    std::uint32_t levels = 0;
    if (root["levels"].Exists() && !toml::Read(root["levels"], &levels)) {
        *error = "ei-texture: levels is not a number";
        return false;
    }
    s->levels = levels;
    if (root["extra"].Exists() && !FromHex(root["extra"].String(), &s->extra)) {
        *error = "ei-texture: extra is not hexadecimal";
        return false;
    }
    return true;
}

// The settings of the .mmp `file` (a PNG stands for it).
bool SettingsOf(const MmpView& file, Settings* s, std::string* error) {
    const MmpHeader& h = *file.header;
    s->format = InfoOf(h.format);
    s->channels = ChannelsOf(h);
    if (!s->format || !s->channels) {
        *error = "its .mmp's format or channels are not made from a PNG";
        return false;
    }
    s->levels = h.mipCount;
    if (h.extraSize > file.bodyBytes) {
        *error = "its .mmp is short";
        return false;
    }
    s->extra.assign(file.body, file.body + h.extraSize);
    return true;
}

bool Consistent(const Settings& s, std::string* error) {
    if (s.format->channels.empty() || s.format->channels == s.channels->name) return true;
    *error = "ei-texture: format " + std::string(s.format->name) + " has channels " + std::string(s.format->channels);
    return false;
}

// The PNG's chunks: `visit(type, data)` for each, until it returns false; false when the PNG is malformed.
template <class Visit>
bool EachChunk(std::string_view png, Visit visit) {
    static constexpr std::string_view kSignature("\x89PNG\r\n\x1a\n", 8);
    if (png.substr(0, 8) != kSignature) return false;
    std::size_t at = 8;
    while (at + 12 <= png.size()) {
        const auto* p = reinterpret_cast<const std::uint8_t*>(png.data() + at);
        const std::uint32_t length = static_cast<std::uint32_t>(p[0]) << 24 | p[1] << 16 | p[2] << 8 | p[3];
        if (length > png.size() - at - 12) return false;
        if (!visit(png.substr(at + 4, 4), png.substr(at + 8, length))) return true;
        at += 12 + length;
    }
    return true;
}

void PutBigEndian(std::string& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(v >> shift));
}

// The chunk "tEXt" of `keyword` and `text`.
std::string TextChunk(std::string_view keyword, std::string_view text) {
    std::string data = "tEXt";
    data.append(keyword);
    data.push_back('\0');
    data.append(text);
    std::string chunk;
    PutBigEndian(chunk, static_cast<std::uint32_t>(data.size() - 4));
    chunk += data;
    PutBigEndian(chunk, static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, reinterpret_cast<const unsigned char*>(data.data()), data.size())));
    return chunk;
}

} // namespace

std::uint32_t Widen(std::uint32_t value, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 8) return (value >> (bits - 8)) & 0xFF;
    std::uint32_t out = 0;
    for (int filled = 0; filled < 8; filled += bits) out |= (value << (8 - bits)) >> filled;
    return out & 0xFF;
}

std::uint32_t Narrow(std::uint32_t value8, int bits) {
    if (bits <= 0) return 0;
    if (bits >= 8) return value8 << (bits - 8);
    const std::uint32_t most = (1u << bits) - 1;
    return (value8 * most + 127) / 255;
}

std::uint8_t Premultiply(std::uint8_t c, std::uint8_t a) { return static_cast<std::uint8_t>((c * a + 127) / 255); }

std::uint8_t Unpremultiply(std::uint8_t c, std::uint8_t a) {
    if (a == 0) return 0;
    return static_cast<std::uint8_t>(std::min((c * 255 + a / 2) / a, 255));
}

Image BoxDown(const Image& image, int width, int height, bool cutout) {
    Image out;
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * height * 4);
    const auto rows = [&](int first, int last) {
        for (int y = first; y < last; ++y) {
            const int y0 = std::min(2 * y, image.height - 1), y1 = std::min(2 * y + 1, image.height - 1);
            for (int x = 0; x < width; ++x) {
                const int x0 = std::min(2 * x, image.width - 1), x1 = std::min(2 * x + 1, image.width - 1);
                const std::uint8_t* t[4] = {&image.rgba[(static_cast<std::size_t>(y0) * image.width + x0) * 4],
                                            &image.rgba[(static_cast<std::size_t>(y0) * image.width + x1) * 4],
                                            &image.rgba[(static_cast<std::size_t>(y1) * image.width + x0) * 4],
                                            &image.rgba[(static_cast<std::size_t>(y1) * image.width + x1) * 4]};
                std::uint8_t* o = &out.rgba[(static_cast<std::size_t>(y) * width + x) * 4];
                int opaque = 0, sum[3] = {};
                for (const std::uint8_t* texel : t)
                    if (texel[3] >= 128) {
                        ++opaque;
                        for (int c = 0; c < 3; ++c) sum[c] += texel[c];
                    }
                if (cutout && opaque > 0) {
                    for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>((sum[c] + opaque / 2) / opaque);
                    o[3] = 255;
                    continue;
                }
                for (int c = 0; c < 4; ++c) o[c] = static_cast<std::uint8_t>((t[0][c] + t[1][c] + t[2][c] + t[3][c] + 2) / 4);
                if (cutout) o[3] = 0;
            }
        }
    };
    InRuns(height, kTexelsAThread / std::max(width, 1), rows);
    return out;
}

std::string SettingsText(std::string_view png) {
    std::string text;
    EachChunk(png, [&](std::string_view type, std::string_view data) {
        if (type == "IEND") return false;
        if (type != "tEXt" || data.size() <= kChunkKeyword.size() || data.substr(0, kChunkKeyword.size()) != kChunkKeyword ||
            data[kChunkKeyword.size()] != '\0')
            return true;
        text = std::string(data.substr(kChunkKeyword.size() + 1));
        return false;
    });
    return text;
}

std::string_view FormatName(MmpFormat format) {
    const FormatInfo* info = InfoOf(format);
    return info ? info->name : std::string_view();
}

bool Supported(const MmpHeader& header) { return InfoOf(header.format) && ChannelsOf(header); }

bool ReadLevel(const MmpView& file, unsigned level, Image* out) {
    const MmpHeader& h = *file.header;
    const FormatInfo* info = InfoOf(h.format);
    if (!info || !ChannelsOf(h) || h.width == 0 || h.height == 0 || h.width > 8192 || h.height > 8192) return false;
    if (info->storage == Storage::Pnt3 && level != 0) return false;
    if (info->storage != Storage::Pnt3 && level >= std::min<std::uint32_t>(h.mipCount, kMostLevels)) return false;
    std::size_t offset = h.extraSize;
    for (unsigned m = 0; m < level; ++m) offset += LevelBytes(h, m);
    if (offset > file.bodyBytes) return false;
    out->width = LevelSize(h.width, level);
    out->height = LevelSize(h.height, level);
    out->rgba.assign(static_cast<std::size_t>(out->width) * out->height * 4, 0);
    const std::uint8_t* bytes = file.body + offset;
    const std::size_t left = file.bodyBytes - offset;
    switch (info->storage) {
    case Storage::Pnt3:
        return DecodePnt3(bytes, left, *out);
    case Storage::Masked:
        if (left < static_cast<std::size_t>(out->width) * out->height * (h.bitsPerPixel / 8)) return false;
        DecodeMasked(h, bytes, *out);
        return true;
    default: {
        // The level's bytes as the renderer uploads them: what the file has for it, the rest of its blocks zero.
        const std::size_t blockBytes = info->storage == Storage::Dxt1 ? 8 : 16;
        std::vector<std::uint8_t> blocks(static_cast<std::size_t>((out->width + 3) / 4) * ((out->height + 3) / 4) * blockBytes, 0);
        const std::size_t has = std::min<std::size_t>(LevelBytes(h, level), blocks.size());
        if (left < has) return false;
        std::memcpy(blocks.data(), bytes, has);
        DecodeBlocks(info->storage, blocks.data(), *out);
        return true;
    }
    }
}

std::string ToPng(const MmpView& file, std::string* error) {
    Settings s;
    Image image;
    if (!SettingsOf(file, &s, error)) return {};
    if (!ReadLevel(file, 0, &image)) {
        *error = "its first level does not read";
        return {};
    }
    bool opaque = true;
    for (std::size_t i = 0; i < image.rgba.size(); i += 4) {
        if (s.format->premultiplied)
            for (int c = 0; c < 3; ++c) image.rgba[i + c] = Unpremultiply(image.rgba[i + c], image.rgba[i + 3]);
        opaque = opaque && image.rgba[i + 3] == 255;
    }
    std::vector<std::uint8_t> rgb;
    const int components = opaque ? 3 : 4;
    if (opaque) {
        rgb.reserve(image.rgba.size() / 4 * 3);
        for (std::size_t i = 0; i < image.rgba.size(); i += 4) rgb.insert(rgb.end(), &image.rgba[i], &image.rgba[i + 3]);
    }
    std::string png = png::Write(image.width, image.height, components, opaque ? std::span<const std::uint8_t>(rgb) : image.rgba);
    if (png.size() < 33) {
        *error = "the PNG was not written";
        return {};
    }
    // The settings after the header chunk (IHDR, 25 bytes after the signature).
    png.insert(33, TextChunk(kChunkKeyword, WriteSettings(s)));
    return png;
}

bool FromPng(std::string_view png, const MmpView* below, std::vector<std::uint8_t>* mmp, std::string* error) {
    Settings s;
    if (const std::string text = SettingsText(png); !text.empty()) {
        if (!ReadSettings(text, &s, error)) return false;
    } else if (below) {
        if (!SettingsOf(*below, &s, error)) return false;
    } else {
        s.format = InfoOf(MmpFormat::A8R8G8B8);
        s.channels = ChannelsNamed("8888");
    }
    if (!Consistent(s, error)) return false;

    Image level;
    if (std::string reason; !png::ReadRgba(png, &level.width, &level.height, &level.rgba, &reason)) {
        *error = "not a PNG that reads: " + reason;
        return false;
    }
    const int width = level.width, height = level.height;
    if (s.format->premultiplied)
        for (std::size_t i = 0; i < level.rgba.size(); i += 4)
            for (int c = 0; c < 3; ++c) level.rgba[i + c] = Premultiply(level.rgba[i + c], level.rgba[i + 3]);

    const std::uint32_t chain = FullChain(width, height);
    const std::uint32_t levels = s.levels == 0 ? chain : std::min(s.levels, chain);
    MmpHeader h = MakeHeader(*s.format, *s.channels, width, height, levels, static_cast<std::uint32_t>(s.extra.size()));
    std::vector<std::uint8_t>& file = *mmp;
    file.assign(sizeof(MmpHeader), 0);
    file.insert(file.end(), s.extra.begin(), s.extra.end());
    if (s.format->storage == Storage::Pnt3) {
        const std::vector<std::uint8_t> packed = EncodePnt3(level);
        h.mipCount = static_cast<std::uint32_t>(packed.size());   // PNT3's field holds the packed bytes
        file.insert(file.end(), packed.begin(), packed.end());
    } else {
        std::size_t fileBytes = file.size();
        for (unsigned m = 0; m < levels; ++m) fileBytes += LevelBytes(h, m);
        file.reserve(fileBytes);
        // One bit of alpha (DXT1's transparent texels, 1555): a texel below is opaque when any of its four is, of their
        // colour (the shipped files' levels so).
        const bool cutout = s.format->storage == Storage::Dxt1 || s.channels->alpha == 0x8000;
        for (unsigned m = 0; m < levels; ++m) {
            if (m > 0) level = BoxDown(level, LevelSize(h.width, m), LevelSize(h.height, m), cutout);
            std::vector<std::uint8_t> bytes;
            if (s.format->storage == Storage::Masked) {
                bytes.resize(static_cast<std::size_t>(level.width) * level.height * (h.bitsPerPixel / 8));
                EncodeMasked(h, level, bytes.data());
            } else {
                bytes = EncodeBlocks(s.format->storage, level);
            }
            bytes.resize(LevelBytes(h, m));   // as the file has the level (a small DXT level: part of its block)
            file.insert(file.end(), bytes.begin(), bytes.end());
        }
    }
    std::memcpy(file.data(), &h, sizeof(MmpHeader));
    return true;
}

} // namespace texture_png
