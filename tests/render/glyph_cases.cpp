#include "render/glyph_cases.h"

#include "render/render_text.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdio>
#include <vector>

namespace glyph_cases {

using render::Rect;
namespace ui = render::ui;
namespace text = render::text;

const Format kFormats[] = {
    {"5:6:5", {16, 0xF800, 0x7E0, 0x1F}},
    {"5:5:5", {16, 0x7C00, 0x3E0, 0x1F}},
    {"8:8:8", {32, 0xFF0000, 0xFF00, 0xFF}},
};

namespace {

constexpr std::uint32_t Colourref(unsigned r, unsigned g, unsigned b) { return r | (g << 8) | (b << 16); }
std::uint32_t RgbOfColorref(std::uint32_t c) { return ((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF); }

// The test's own quantising (independent of render_ui's): top bits, repeated.
std::uint32_t RefChannel(std::uint32_t v, int bits) {
    if (bits >= 8) return v;
    std::uint32_t top = v >> (8 - bits), out = 0;
    for (int filled = 0; filled < 8; filled += bits) out |= (top << (8 - bits)) >> filled;
    return out & 0xFF;
}
struct Bits {
    int r, g, b;
};
Bits BitsOf(const render::PixelFormat& f) {
    if (f.bitsPerPixel == 32) return {8, 8, 8};
    return f.greenMask == 0x7E0 ? Bits{5, 6, 5} : Bits{5, 5, 5};
}

// The run drawn onto `pixels` (0x00RRGGBB) by the canvas's rule.
void RefGlyphRun(std::vector<std::uint32_t>& pixels, int width, int height, text::TextFont& font,
                 const text::TextLayout& layout, std::uint32_t colour, const render::PixelFormat& format) {
    int x0 = 0, y0 = 0, x1 = width, y1 = height;
    if (layout.clipped) {
        x0 = std::max(x0, layout.clip.left);
        y0 = std::max(y0, layout.clip.top);
        x1 = std::min(x1, layout.clip.right);
        y1 = std::min(y1, layout.clip.bottom);
    }
    if (x1 <= x0 || y1 <= y0) return;
    std::vector<std::uint8_t> cover(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    const text::GlyphAtlas& atlas = font.Atlas();
    for (const text::GlyphQuad& q : layout.quads)
        for (int y = std::max(y0, q.y); y < std::min(y1, q.y + q.height); ++y)
            for (int x = std::max(x0, q.x); x < std::min(x1, q.x + q.width); ++x) {
                std::uint8_t a = atlas.Pixels()[static_cast<std::size_t>(q.atlasY + y - q.y) *
                                                    static_cast<std::size_t>(atlas.Width()) +
                                                static_cast<std::size_t>(q.atlasX + x - q.x)];
                std::uint8_t& c =
                    cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)];
                c = std::max(c, a);
            }
    for (const Rect& u : layout.underlines)
        for (int y = std::max(y0, u.top); y < std::min(y1, u.bottom); ++y)
            for (int x = std::max(x0, u.left); x < std::min(x1, u.right); ++x)
                cover[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x)] =
                    255;
    std::uint32_t text = RgbOfColorref(colour);
    for (std::size_t i = 0; i < cover.size(); ++i) {
        unsigned c = cover[i];
        if (!c) continue;
        std::uint32_t out = 0;
        for (int shift : {16, 8, 0}) {
            unsigned t = (text >> shift) & 0xFF, d = (pixels[i] >> shift) & 0xFF;
            out |= ((t * c + d * (255 - c) + 127) / 255) << shift;
        }
        pixels[i] = RefQuantise(out, format);
    }
}

}   // namespace

std::uint32_t RefQuantise(std::uint32_t rgb, const render::PixelFormat& f) {
    Bits b = BitsOf(f);
    return (RefChannel((rgb >> 16) & 0xFF, b.r) << 16) | (RefChannel((rgb >> 8) & 0xFF, b.g) << 8) |
           RefChannel(rgb & 0xFF, b.b);
}
// Each channel's top bits of the format (the low bits 0), as the game's PixelAt gives them.
std::uint32_t RefTopBits(std::uint32_t rgb, const render::PixelFormat& f) {
    Bits b = BitsOf(f);
    auto top = [](std::uint32_t v, int bits) { return v & (0xFFu << (8 - bits)) & 0xFF; };
    return (top((rgb >> 16) & 0xFF, b.r) << 16) | (top((rgb >> 8) & 0xFF, b.g) << 8) | top(rgb & 0xFF, b.b);
}

int Differ(const std::vector<std::uint32_t>& a, const std::vector<std::uint32_t>& b, int width, std::string* example) {
    int differ = 0;
    for (std::size_t i = 0; i < a.size() && i < b.size(); ++i)
        if (a[i] != b[i] && ++differ == 1 && example) {
            char buffer[160];
            std::snprintf(buffer, sizeof buffer, "first at (%d, %d): expected %06X, canvas %06X",
                          static_cast<int>(i % static_cast<std::size_t>(width)),
                          static_cast<int>(i / static_cast<std::size_t>(width)), a[i], b[i]);
            *example = buffer;
        }
    return differ + (a.size() == b.size() ? 0 : 1000000);
}

void Run(CheckFn check) {
    const int w = 180, h = 72;
    struct Case {
        const char* text;
        unsigned format;
        Rect rect;
        bool smoothing;
        int pixels;
    };
    const Case cases[] = {
        {"Hello, &world: AV Wa fi", 0, {4, 4, 176, 40}, true, 16},
        {"\xCF\xF0\xE8\xE2\xE5\xF2, \xEC\xE8\xF0! \xD1\xEB\xEE\xE2\xE0 \xED\xE0 \xED\xE5\xF1\xEA\xEE\xEB\xFC\xEA\xEE "
         "\xF1\xF2\xF0\xEE\xEA",
         text::kWordBreak | text::kCenter,
         {10, 2, 120, 70},
         true,
         19},
        {"Clipped text that runs past the right edge of its rectangle", text::kNoPrefix, {-6, 30, 90, 44}, true, 13},
        {"Right &aligned\r\nsecond line", text::kRight, {0, 20, 175, 71}, false, 16},
    };
    for (const Format& f : kFormats) {
        int caseIndex = 0;
        for (const Case& c : cases) {
            const void* holder = &c;
            text::TextFont* font = text::GetTextFont("Times New Roman", c.pixels, c.smoothing);
            if (!font) {
                check(false, std::string("font Times New Roman ") + std::to_string(c.pixels) + " px");
                continue;
            }
            ui::Canvas(holder, w, h, f.format);
            // A background of fills (the text blends over it), the shadow then the text.
            std::vector<std::uint32_t> expected(static_cast<std::size_t>(w) * h, 0);
            const std::uint32_t fills[3] = {Colourref(200, 30, 90), Colourref(10, 140, 250), Colourref(255, 255, 255)};
            for (int i = 0; i < 3; ++i) {
                Rect r{i * 60, i * 10, i * 60 + 70, h - i * 5};
                ui::Fill(holder, r, fills[i]);
                for (int y = r.top; y < r.bottom; ++y)
                    for (int x = r.left; x < std::min(r.right, w); ++x)
                        expected[static_cast<std::size_t>(y) * w + static_cast<std::size_t>(x)] =
                            RefQuantise(RgbOfColorref(fills[i]), f.format);
            }
            std::u32string decoded = text::DecodeCp1251(c.text);
            text::TextLayout shadow, layout;
            Rect shifted{c.rect.left + 1, c.rect.top + 1, c.rect.right + 1, c.rect.bottom + 1};
            bool laid = text::Layout(*font, decoded, shifted, c.format, shadow) &&
                        text::Layout(*font, decoded, c.rect, c.format, layout);
            check(laid, std::string("layout of case ") + std::to_string(caseIndex));
            ui::GlyphRun(holder, *font, shadow, Colourref(8, 8, 8));
            RefGlyphRun(expected, w, h, *font, shadow, Colourref(8, 8, 8), f.format);
            const std::uint32_t colour = Colourref(250, 220, 120 + caseIndex * 30);
            ui::GlyphRun(holder, *font, layout, colour);
            RefGlyphRun(expected, w, h, *font, layout, colour, f.format);
            std::vector<std::uint32_t> got;
            std::string example;
            int differ = ui::ReadPixels(holder, &got) ? Differ(expected, got, w, &example) : -1;
            check(differ == 0, std::string(f.name) + " glyph run, case " + std::to_string(caseIndex) + " (" +
                                   std::to_string(layout.quads.size()) + " glyphs, " +
                                   std::to_string(layout.underlines.size()) +
                                   " underlines): " + std::to_string(differ) + " pixels differ" +
                                   (example.empty() ? "" : " (" + example + ")"));
            ui::DropCanvas(holder);
            ++caseIndex;
        }
    }
    // Runs that overlap none of the others are drawn together (one coverage pass and one compose pass, the runs
    // stacked in the scratch targets): four of them, one clipped by the canvas's left edge, in different colours.
    for (const Format& f : kFormats) {
        const void* holder = &f;
        text::TextFont* font = text::GetTextFont("Times New Roman", 14, true);
        if (!font) {
            check(false, "font Times New Roman 14 px");
            continue;
        }
        ui::Canvas(holder, w, h, f.format);
        std::vector<std::uint32_t> expected(static_cast<std::size_t>(w) * h,
                                            RefQuantise(RgbOfColorref(Colourref(30, 90, 60)), f.format));
        ui::Fill(holder, Rect{0, 0, w, h}, Colourref(30, 90, 60));
        const Rect rects[] = {{2, 2, 80, 20}, {100, 2, 178, 20}, {2, 28, 178, 46}, {-10, 52, 70, 70}};
        const char* const texts[] = {"First run", "Second", "A third, longer run of text", "Clipped at the edge"};
        int runs = 0;
        for (int i = 0; i < 4; ++i) {
            text::TextLayout layout;
            if (!text::Layout(*font, text::DecodeCp1251(texts[i]), rects[i], text::kNoPrefix, layout)) continue;
            const std::uint32_t colour = Colourref(250 - i * 40, 200, 40 + i * 50);
            ui::GlyphRun(holder, *font, layout, colour);
            RefGlyphRun(expected, w, h, *font, layout, colour, f.format);
            ++runs;
        }
        std::vector<std::uint32_t> got;
        std::string example;
        int differ = ui::ReadPixels(holder, &got) ? Differ(expected, got, w, &example) : -1;
        check(runs == 4 && differ == 0, std::string(f.name) + " glyph runs drawn together (" + std::to_string(runs) +
                                            " laid out): " + std::to_string(differ) + " pixels differ" +
                                            (example.empty() ? "" : " (" + example + ")"));
        ui::DropCanvas(holder);
    }
    // Runs drawn together whose vertices outgrow the stream buffer (about 2,000 glyphs, 190 KB): the buffer is
    // replaced before the first of them is appended, never between them.
    {
        const int bw = 960, bh = 340;
        const void* holder = &bw;
        text::TextFont* font = text::GetTextFont("Times New Roman", 14, true);
        if (font) {
            ui::Canvas(holder, bw, bh, kFormats[2].format);
            std::vector<std::uint32_t> expected(static_cast<std::size_t>(bw) * bh, 0);
            const char* line = "The quick brown fox jumps over the lazy dog; pack my box with five dozen liquor jugs. "
                               "Sphinx of black quartz, judge my vow: 0123456789 and again 9876543210.";
            int runs = 0;
            for (int i = 0; i < 13; ++i) {
                text::TextLayout layout;
                if (!text::Layout(*font, text::DecodeCp1251(line), Rect{2, 2 + i * 26, bw - 2, 2 + i * 26 + 24},
                                  text::kNoPrefix, layout))
                    continue;
                ui::GlyphRun(holder, *font, layout, Colourref(220, 230, 140 + i * 8));
                RefGlyphRun(expected, bw, bh, *font, layout, Colourref(220, 230, 140 + i * 8), kFormats[2].format);
                ++runs;
            }
            std::vector<std::uint32_t> got;
            std::string example;
            int differ = ui::ReadPixels(holder, &got) ? Differ(expected, got, bw, &example) : -1;
            check(runs == 13 && differ == 0, "glyph runs drawn together past the stream buffer (" +
                                                 std::to_string(runs) + " laid out): " + std::to_string(differ) +
                                                 " pixels differ" + (example.empty() ? "" : " (" + example + ")"));
            ui::DropCanvas(holder);
        } else {
            check(false, "font Times New Roman 14 px");
        }
    }
    // Over black in 8:8:8 the run is ComposeAgainstBlack (render_text's CPU reference).
    {
        const void* holder = &w;
        text::TextFont* font = text::GetTextFont("Times New Roman", 21, true);
        text::TextLayout layout;
        if (font && text::Layout(*font, text::DecodeCp1251("&Against black: Ww Qq \xC6\xE6"), Rect{3, 3, 170, 60},
                                 text::kWordBreak, layout)) {
            ui::Canvas(holder, w, h, kFormats[2].format);
            ui::GlyphRun(holder, *font, layout, Colourref(255, 200, 40));
            std::vector<std::uint32_t> composed(static_cast<std::size_t>(w) * h, 0), got;
            text::ComposeAgainstBlack(*font, layout, Colourref(255, 200, 40), composed.data(), w, h, w);
            for (std::uint32_t& p : composed) p &= 0xFFFFFF;
            std::string example;
            int differ = ui::ReadPixels(holder, &got) ? Differ(composed, got, w, &example) : -1;
            check(differ == 0, "8:8:8 glyph run over black against ComposeAgainstBlack: " + std::to_string(differ) +
                                   " pixels differ" + (example.empty() ? "" : " (" + example + ")"));
            ui::DropCanvas(holder);
        } else {
            check(false, "font or layout for the ComposeAgainstBlack case");
        }
    }
}

}   // namespace glyph_cases
