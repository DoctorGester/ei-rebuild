// engine/models/texture_png.h: textures as PNG and back (docs/notes/mods.md, "Textures as PNG"). Every 16-bit texel of
// the RGB formats and every premultiplied texel come back exactly; PNT3's runs; DXT1's transparent texels stay
// transparent and its colours within the compressor's error; the settings from the chunk, from the .mmp below, or a
// new texture's; the box filter's levels.
#include "engine/models/texture_png.h"

#include "lib/png.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <vector>

namespace {

int g_failures = 0;

void Check(bool ok, const std::string& what) {
    if (ok) return;
    ++g_failures;
    std::printf("ei_unit_check: texture_png: %s\n", what.c_str());
}

std::uint32_t Bits(std::uint32_t mask) {
    std::uint32_t n = 0;
    for (; mask; mask &= mask - 1) ++n;
    return n;
}

std::uint32_t Shift(std::uint32_t mask) {
    std::uint32_t n = 0;
    while (mask && !((mask >> n) & 1)) ++n;
    return n;
}

MmpHeader Header(MmpFormat format, std::uint32_t bits, std::uint32_t a, std::uint32_t r, std::uint32_t g, std::uint32_t b,
                 int width, int height, std::uint32_t levels) {
    MmpHeader h;
    h.magic = 0x504D4D;
    h.width = static_cast<std::uint32_t>(width);
    h.height = static_cast<std::uint32_t>(height);
    h.mipCount = levels;
    h.format = format;
    h.bitsPerPixel = bits;
    h.alphaMask = a;
    h.alphaShift = Shift(a);
    h.alphaKind = Bits(a);
    h.redMask = r;
    h.redShift = Shift(r);
    h.redBits = Bits(r);
    h.greenMask = g;
    h.greenShift = Shift(g);
    h.greenBits = Bits(g);
    h.blueMask = b;
    h.blueShift = Shift(b);
    h.blueBits = Bits(b);
    return h;
}

struct File {
    MmpHeader header;
    std::vector<std::uint8_t> body;
    texture_png::MmpView View() const { return {&header, body.data(), body.size()}; }
};

File Parse(const std::vector<std::uint8_t>& mmp) {
    File f;
    std::memcpy(&f.header, mmp.data(), sizeof(MmpHeader));
    f.body.assign(mmp.begin() + sizeof(MmpHeader), mmp.end());
    return f;
}

bool SameHeader(const MmpHeader& a, const MmpHeader& b) { return std::memcmp(&a, &b, sizeof(MmpHeader)) == 0; }

// `file` through a PNG and back: the file made again.
bool RoundTrip(const File& file, File* back, const std::string& what) {
    std::string error;
    const std::string png = texture_png::ToPng(file.View(), &error);
    Check(!png.empty(), what + ": ToPng: " + error);
    std::vector<std::uint8_t> mmp;
    const bool made = !png.empty() && texture_png::FromPng(png, nullptr, &mmp, &error);
    Check(made, what + ": FromPng: " + error);
    if (made) *back = Parse(mmp);
    return made;
}

// A PNG without the settings chunk (as an image editor writes one).
std::string PlainPng(const texture_png::Image& image) {
    return png::Write(image.width, image.height, 4, image.rgba);
}

std::uint32_t g_seed = 12345;
std::uint32_t Next() {
    g_seed = g_seed * 1664525u + 1013904223u;
    return g_seed >> 8;
}

void CheckChannels() {
    for (int bits : {1, 4, 5, 6, 8})
        for (std::uint32_t v = 0; v < (1u << bits); ++v)
            Check(texture_png::Narrow(texture_png::Widen(v, bits), bits) == v, "widen and narrow " + std::to_string(bits) + " bits, " + std::to_string(v));
    for (int a = 0; a < 256; ++a)
        for (int c = 0; c < 256; ++c) {
            const auto back = texture_png::Premultiply(texture_png::Unpremultiply(static_cast<std::uint8_t>(c), static_cast<std::uint8_t>(a)),
                                                       static_cast<std::uint8_t>(a));
            Check(back == (c <= a ? c : a), "premultiplied " + std::to_string(c) + " of alpha " + std::to_string(a));
        }
}

// Every 16-bit texel of 565, 1555 and 4444: the file made again holds the same bytes.
void CheckSixteenBits() {
    struct Case {
        MmpFormat format;
        std::uint32_t a, r, g, b;
    };
    for (const Case& c : {Case{MmpFormat::R5G6B5, 0, 0xF800, 0x07E0, 0x001F}, Case{MmpFormat::A1R5G5B5, 0x8000, 0x7C00, 0x03E0, 0x001F},
                          Case{MmpFormat::A4R4G4B4, 0xF000, 0x0F00, 0x00F0, 0x000F}}) {
        File file{Header(c.format, 16, c.a, c.r, c.g, c.b, 256, 256, 1), {}};
        for (std::uint32_t v = 0; v < 65536; ++v) {
            file.body.push_back(static_cast<std::uint8_t>(v));
            file.body.push_back(static_cast<std::uint8_t>(v >> 8));
        }
        File back;
        const std::string what = "format " + std::to_string(static_cast<unsigned>(c.format));
        if (!RoundTrip(file, &back, what)) continue;
        Check(SameHeader(file.header, back.header), what + ": the header");
        bool same = back.body.size() == file.body.size();
        for (std::size_t i = 0; same && i < file.body.size(); i += 2) {
            same = back.body[i] == file.body[i] && back.body[i + 1] == file.body[i + 1];
        }
        Check(same, what + ": every texel");
    }
}

// Premultiplied 8888 with levels: level 0 exact, the levels the box filter's.
void CheckArgb() {
    File file{Header(MmpFormat::A8R8G8B8, 32, 0xFF000000u, 0xFF0000, 0xFF00, 0xFF, 16, 8, 3), {}};
    const int texels[3] = {16 * 8, 8 * 4, 4 * 2};
    texture_png::Image level0;
    level0.width = 16;
    level0.height = 8;
    for (int m = 0; m < 3; ++m)
        for (int i = 0; i < texels[m]; ++i) {
            const std::uint8_t a = static_cast<std::uint8_t>(Next());
            const std::uint8_t rgb[3] = {static_cast<std::uint8_t>(Next() % (a + 1u)), static_cast<std::uint8_t>(Next() % (a + 1u)),
                                         static_cast<std::uint8_t>(Next() % (a + 1u))};
            file.body.insert(file.body.end(), {rgb[2], rgb[1], rgb[0], a});
            if (m == 0) level0.rgba.insert(level0.rgba.end(), {rgb[0], rgb[1], rgb[2], a});
        }
    File back;
    if (!RoundTrip(file, &back, "8888")) return;
    Check(SameHeader(file.header, back.header), "8888: the header");
    Check(back.body.size() == file.body.size(), "8888: the file's size");
    Check(std::memcmp(back.body.data(), file.body.data(), 16 * 8 * 4) == 0, "8888: level 0");
    texture_png::Image level1;
    Check(texture_png::ReadLevel(back.View(), 1, &level1) && level1.rgba == texture_png::BoxDown(level0, 8, 4, false).rgba,
          "8888: level 1 is the box filter's");
}

void CheckPnt3() {
    File file{Header(MmpFormat::Pnt3, 32, 0, 0, 0, 0, 7, 5, 0), {}};
    auto put = [&](std::uint32_t word) { file.body.insert(file.body.end(), {static_cast<std::uint8_t>(word), static_cast<std::uint8_t>(word >> 8),
                                                                           static_cast<std::uint8_t>(word >> 16), static_cast<std::uint8_t>(word >> 24)}); };
    put(6 * 4);   // six transparent texels
    for (std::uint32_t t : {0xFF102030u, 0x80404040u, 0x00000000u, 0x01010101u}) put(t);
    put(0);       // a group starting with transparent black
    for (std::uint32_t t : {0xFFFFFFFFu, 0x20101010u, 0x40000000u}) put(t);
    put(20 * 4);  // past the end
    file.header.mipCount = static_cast<std::uint32_t>(file.body.size());
    texture_png::Image original, again;
    Check(texture_png::ReadLevel(file.View(), 0, &original), "PNT3: read");
    File back;
    if (!RoundTrip(file, &back, "PNT3")) return;
    Check(back.header.format == MmpFormat::Pnt3 && back.header.width == 7 && back.header.height == 5, "PNT3: the header");
    Check(back.header.mipCount == back.body.size(), "PNT3: the packed size");
    Check(texture_png::ReadLevel(back.View(), 0, &again) && again.rgba == original.rgba, "PNT3: the texels");
}

// DXT1 from a PNG with no chunk, its settings from the .mmp below: the transparent texels stay so, the colours close.
void CheckDxt1() {
    texture_png::Image image;
    image.width = image.height = 16;
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            const bool hole = (x - 8) * (x - 8) + (y - 8) * (y - 8) < 20;
            image.rgba.insert(image.rgba.end(), {static_cast<std::uint8_t>(hole ? 0 : x * 16), static_cast<std::uint8_t>(hole ? 0 : x * 8),
                                                 static_cast<std::uint8_t>(hole ? 0 : 128), static_cast<std::uint8_t>(hole ? 0 : 255)});
        }
    const File below{Header(static_cast<MmpFormat>(0x31545844), 4, 0x8000, 0x7C00, 0x03E0, 0x001F, 64, 64, 2), std::vector<std::uint8_t>(64 * 64 / 2 + 32 * 32 / 2)};
    std::vector<std::uint8_t> mmp;
    std::string error;
    const texture_png::MmpView view = below.View();
    if (!texture_png::FromPng(PlainPng(image), &view, &mmp, &error)) {
        Check(false, "DXT1: " + error);
        return;
    }
    const File made = Parse(mmp);
    Check(made.header.format == below.header.format && made.header.mipCount == 2 && made.header.alphaKind == 1 && made.header.width == 16,
          "DXT1: the settings of the .mmp below");
    Check(made.body.size() == 16 * 16 / 2 + 8 * 8 / 2, "DXT1: the levels' bytes");
    texture_png::Image level;
    Check(texture_png::ReadLevel(made.View(), 0, &level), "DXT1: read");
    int worst = 0, sum = 0;
    bool alpha = true;
    for (std::size_t i = 0; i < level.rgba.size(); i += 4) {
        alpha = alpha && level.rgba[i + 3] == image.rgba[i + 3];
        for (int c = 0; c < 3; ++c) {
            const int off = std::abs(level.rgba[i + c] - image.rgba[i + c]);
            worst = std::max(worst, off);
            sum += off;
        }
    }
    Check(alpha, "DXT1: the transparent texels");
    // A gradient along the colours' line: a block of four colours holds it, one with transparent texels has three.
    Check(worst <= 16 && sum <= 2 * 16 * 16 * 3, "DXT1: colours off by " + std::to_string(worst) + " at most, " + std::to_string(sum) + " in all");
}

void CheckSettings() {
    texture_png::Image image;
    image.width = 5;
    image.height = 3;
    image.rgba.assign(5 * 3 * 4, 200);
    std::vector<std::uint8_t> mmp;
    std::string error;
    Check(texture_png::FromPng(PlainPng(image), nullptr, &mmp, &error), "new texture: " + error);
    const File made = Parse(mmp);
    Check(made.header.format == MmpFormat::A8R8G8B8 && made.header.mipCount == 3 && made.header.alphaKind == 8,
          "new texture: 8888, every level");
    Check(made.body.size() == (5 * 3 + 2 * 1 + 1 * 0) * 4, "new texture: the levels' bytes (the last of them 0 high: none)");
    const std::string png = texture_png::ToPng(made.View(), &error);
    Check(texture_png::SettingsText(png).find("format = \"8888\"") != std::string::npos, "the chunk's format");
    Check(!texture_png::FromPng("not a png", nullptr, &mmp, &error), "a file that is not a PNG");

    texture_png::Image box;
    box.width = box.height = 2;
    box.rgba = {0, 10, 255, 255, 1, 10, 255, 0, 2, 10, 0, 255, 2, 11, 0, 0};
    Check(texture_png::BoxDown(box, 1, 1, false).rgba == std::vector<std::uint8_t>{1, 10, 128, 128}, "the box filter's rounding");
    Check(texture_png::BoxDown(box, 1, 1, true).rgba == std::vector<std::uint8_t>{1, 10, 128, 255}, "the box filter's cutout");
    box.rgba[3] = box.rgba[11] = 100;
    Check(texture_png::BoxDown(box, 1, 1, true).rgba[3] == 0, "the box filter's cutout of no opaque texel");
}

} // namespace

int RunTexturePngChecks() {
    CheckChannels();
    CheckSixteenBits();
    CheckArgb();
    CheckPnt3();
    CheckDxt1();
    CheckSettings();
    return g_failures;
}
