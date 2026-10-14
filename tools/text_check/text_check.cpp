// Offline check of the interface text layout (src/render/render_text, ticket N2; docs/notes/ui-native.md).
//
//   ei_text_check [--rules] [--gdi] [--report] [--texts <texts.res>] [--threads N] [--pixels-every N]
//                 [--examples N]
//
// --rules   (no GDI) DrawText's wrap / alignment / prefix / tab / break rules on synthetic cases, with a
//           fixed-pitch fake font. The expectations were measured from user32's DrawTextA on Windows 10
//           with an underlined Courier New (16 px, 10 px cells): the underline shows each drawn line's
//           extent, spaces included.
// --gdi     the layout run with GDI's own metrics (a Font implemented over GDI, here only) against
//           DrawTextA itself, on the game's strings and random synthetic ones: return values,
//           DT_CALCRECT rectangles and, for every Nth case, the pixels (non-antialiased, so exact).
//           This checks the layout rules on real fonts; it is a pass / fail gate.
// --report  the shipped fonts (Liberation through stb_truetype) against GDI's Times New Roman /
//           Courier New: font metrics, and how many strings break into lines differently or differ in
//           extent. A report, not a gate (the fonts are stand-ins).
// Without a mode switch all three run. The game's strings come from texts.res (read only).
#define NOMINMAX
#include <windows.h>

#include "render/render_text.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace render::text;

namespace {

// ---- Text helpers -------------------------------------------------------------------------------------

std::string Utf8(std::u32string_view text) {
    std::string out;
    for (char32_t ch : text) {
        if (ch == U'\n') out += "\\n";
        else if (ch == U'\r') out += "\\r";
        else if (ch == U'\t') out += "\\t";
        else if (ch < 0x20) {
            char buffer[8];
            std::snprintf(buffer, sizeof buffer, "\\x%02X", static_cast<unsigned>(ch));
            out += buffer;
        } else if (ch < 0x80) out += static_cast<char>(ch);
        else if (ch < 0x800) {
            out += static_cast<char>(0xC0 | (ch >> 6));
            out += static_cast<char>(0x80 | (ch & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (ch >> 12));
            out += static_cast<char>(0x80 | ((ch >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (ch & 0x3F));
        }
    }
    return out;
}

std::string Shorten(const std::string& text, std::size_t limit = 90) {
    return text.size() <= limit ? text : text.substr(0, limit) + "...";
}

std::string FormatName(unsigned format) {
    std::string name;
    auto add = [&](unsigned bit, const char* text) {
        if (!(format & bit)) return;
        if (!name.empty()) name += '|';
        name += text;
    };
    add(kCenter, "CENTER");
    add(kRight, "RIGHT");
    add(kVCenter, "VCENTER");
    add(kWordBreak, "WORDBREAK");
    add(kExpandTabs, "EXPANDTABS");
    add(kNoPrefix, "NOPREFIX");
    add(kCalcRect, "CALCRECT");
    return name.empty() ? "0" : name;
}

// ---- Rules (no GDI) -----------------------------------------------------------------------------------

constexpr int kCell = 10;

// Courier New at 16 px as GDI reports it: 10 px cells, 18 px lines; every printable character inks its
// whole cell except the space.
class FixedFont final : public Font {
public:
    FixedFont() {
        metrics_.ascent = 14;
        metrics_.descent = 4;
        metrics_.height = 18;
        metrics_.averageWidth = kCell;
    }
    const FontMetrics& Metrics() const override { return metrics_; }
    GlyphMetrics Glyph(char32_t ch) const override {
        GlyphMetrics glyph;
        if (ch < 0x20) return glyph;
        glyph.advance = kCell;
        if (ch != U' ') {
            glyph.top = -metrics_.ascent;
            glyph.width = kCell;
            glyph.height = metrics_.height;
        }
        return glyph;
    }
    int Kerning(char32_t, char32_t) const override { return 0; }
    void Rasterise(char32_t ch, std::uint8_t* out, int pitch) const override {
        const GlyphMetrics glyph = Glyph(ch);
        for (int y = 0; y < glyph.height; ++y) std::memset(out + y * pitch, 255, static_cast<std::size_t>(glyph.width));
    }

private:
    FontMetrics metrics_;
};

struct RuleCase {
    const char* text;
    int cells;                         // rectangle width in cells
    int calcLeft, calcRight, calcCentre;   // DT_CALCRECT width in cells: left, DT_RIGHT, DT_CENTER
    const char* left;                  // drawn extent of each line, left-aligned ("a..b" in cells, "-" none)
    const char* right;                 // ... right-aligned
    unsigned extra;                    // format bits besides DT_WORDBREAK
};

// Measured with DrawTextA (DT_WORDBREAK | extra) in a rectangle `cells` x 10 px wide.
const RuleCase kRuleCases[] = {
    {"aaa bbb ccc", 2, 3, 3, 3, "0..2 0..2 0..2", "0..2 - 0..2 - 0..2", 0},
    {"aaa bbb ccc", 3, 3, 3, 3, "0..3 0..3 0..3", "0..3 - 0..3 - 0..3", 0},
    {"aaa bbb ccc", 4, 4, 3, 3, "0..4 0..4 0..3", "1..4 1..4 1..4", 0},
    {"aaa bbb ccc", 5, 4, 3, 3, "0..4 0..4 0..3", "2..5 2..5 2..5", 0},
    {"aaa bbb ccc", 6, 4, 3, 3, "0..4 0..4 0..3", "3..6 3..6 3..6", 0},
    {"aaa bbb ccc", 7, 7, 7, 7, "0..7 0..3", "0..7 3..7", 0},
    {"aaa bbb ccc", 8, 8, 7, 7, "0..8 0..3", "1..8 5..8", 0},
    {"aaa bbb ccc", 9, 8, 7, 7, "0..8 0..3", "2..9 6..9", 0},
    {"aaa bbb ccc", 10, 8, 7, 7, "0..8 0..3", "3..10 7..10", 0},
    {"aaa bbb ccc", 11, 11, 11, 11, "0..11", "0..11", 0},
    {"aaa  bbb  ccc", 2, 3, 3, 3, "0..2 0..1 0..2 0..1 0..2", "0..2 1..2 0..2 1..2 0..2", 0},
    {"aaa  bbb  ccc", 3, 3, 3, 3, "0..3 0..1 0..3 0..1 0..3", "0..3 2..3 0..3 2..3 0..3", 0},
    {"aaa  bbb  ccc", 4, 4, 4, 3, "0..4 0..4 0..3", "1..4 0..4 3..4 1..4", 0},
    {"aaa  bbb  ccc", 5, 5, 4, 4, "0..5 0..5 0..3", "1..5 1..5 2..5", 0},
    {"aaa  bbb  ccc", 6, 5, 4, 4, "0..5 0..5 0..3", "2..6 2..6 3..6", 0},
    {"aaa  bbb  ccc", 7, 5, 4, 4, "0..5 0..5 0..3", "3..7 3..7 4..7", 0},
    {"aaa  bbb  ccc", 8, 8, 8, 8, "0..8 0..4", "0..8 3..8", 0},
    {"aaa  bbb  ccc", 9, 9, 8, 8, "0..9 0..3", "1..9 5..9", 0},
    {"aaa  bbb  ccc", 10, 10, 9, 9, "0..10 0..3", "1..10 7..10", 0},
    {"aaa  bbb  ccc", 11, 10, 9, 9, "0..10 0..3", "2..11 8..11", 0},
    {"aaa  bbb  ccc", 12, 10, 9, 9, "0..10 0..3", "3..12 9..12", 0},
    {"aaa   bbb   ccc", 2, 3, 3, 3, "0..2 0..2 0..2 0..2 0..2", "0..2 1..2 - 0..2 1..2 - 0..2", 0},
    {"aaa   bbb   ccc", 3, 3, 3, 3, "0..3 0..2 0..3 0..2 0..3", "0..3 1..3 0..3 1..3 0..3", 0},
    {"aaa   bbb   ccc", 4, 4, 3, 4, "0..4 0..4 0..2 0..3", "1..4 3..4 1..4 3..4 1..4", 0},
    {"aaa   bbb   ccc", 5, 5, 5, 4, "0..5 0..5 0..3", "1..5 1..5 0..5", 0},
    {"aaa   bbb   ccc", 6, 6, 5, 5, "0..6 0..6 0..3", "1..6 1..6 3..6", 0},
    {"aaa   bbb   ccc", 7, 6, 5, 5, "0..6 0..6 0..3", "2..7 2..7 4..7", 0},
    {"aaa   bbb   ccc", 8, 6, 5, 5, "0..6 0..6 0..3", "3..8 3..8 5..8", 0},
    {"aaa   bbb   ccc", 9, 9, 9, 9, "0..9 0..5", "0..9 3..9", 0},
    {"aaa   bbb   ccc", 10, 10, 9, 9, "0..10 0..4", "1..10 5..10", 0},
    {"aaa   bbb   ccc", 11, 11, 10, 10, "0..11 0..3", "1..11 7..11", 0},
    {"aaa   bbb   ccc", 12, 12, 11, 11, "0..12 0..3", "1..12 9..12", 0},
    {"aaa    bbb", 2, 3, 3, 3, "0..2 0..2 0..2", "0..2 1..2 1..2 0..2", 0},
    {"aaa    bbb", 3, 3, 3, 3, "0..3 0..3 0..3", "0..3 1..3 - 0..3", 0},
    {"aaa    bbb", 4, 4, 3, 3, "0..4 0..2 0..3", "1..4 2..4 1..4", 0},
    {"aaa    bbb", 5, 5, 5, 4, "0..5 0..4", "1..5 0..5", 0},
    {"aaa    bbb", 6, 6, 5, 5, "0..6 0..3", "1..6 2..6", 0},
    {"aaa    bbb", 7, 7, 6, 6, "0..7 0..3", "1..7 4..7", 0},
    {"aaa    bbb", 8, 7, 6, 6, "0..7 0..3", "2..8 5..8", 0},
    {"aaa    bbb", 9, 7, 6, 6, "0..7 0..3", "3..9 6..9", 0},
    {"aaa    bbb", 10, 10, 10, 10, "0..10", "0..10", 0},
    {"aaaaaaaa bbb", 2, 8, 8, 8, "0..2 0..2", "0..2 - 0..2", 0},
    {"aaaaaaaa bbb", 3, 8, 8, 8, "0..3 0..3", "0..3 - 0..3", 0},
    {"aaaaaaaa bbb", 4, 8, 8, 8, "0..4 0..3", "0..4 0..4", 0},
    {"aaaaaaaa bbb", 5, 8, 8, 8, "0..5 0..3", "0..5 1..5", 0},
    {"aaaaaaaa bbb", 6, 8, 8, 8, "0..6 0..3", "0..6 2..6", 0},
    {"aaaaaaaa bbb", 7, 8, 8, 8, "0..7 0..3", "0..7 3..7", 0},
    {"aaaaaaaa bbb", 8, 8, 8, 8, "0..8 0..3", "0..8 4..8", 0},
    {"aaaaaaaa bbb", 9, 9, 8, 8, "0..9 0..3", "1..9 6..9", 0},
    {"aaaaaaaa bbb", 10, 9, 8, 8, "0..9 0..3", "2..10 7..10", 0},
    {"aaaaaaaa bbb", 11, 9, 8, 8, "0..9 0..3", "3..11 8..11", 0},
    {"aaaaaaaa bbb", 12, 12, 12, 12, "0..12", "0..12", 0},
    {"aaaaaaaa  bbb", 2, 8, 8, 8, "0..2 0..1 0..2", "0..2 1..2 0..2", 0},
    {"aaaaaaaa  bbb", 3, 8, 8, 8, "0..3 0..1 0..3", "0..3 2..3 0..3", 0},
    {"aaaaaaaa  bbb", 4, 8, 8, 8, "0..4 0..4", "0..4 3..4 1..4", 0},
    {"aaaaaaaa  bbb", 5, 8, 8, 8, "0..5 0..4", "0..5 0..5", 0},
    {"aaaaaaaa  bbb", 6, 8, 8, 8, "0..6 0..4", "0..6 1..6", 0},
    {"aaaaaaaa  bbb", 7, 8, 8, 8, "0..7 0..4", "0..7 2..7", 0},
    {"aaaaaaaa  bbb", 8, 8, 8, 8, "0..8 0..4", "0..8 3..8", 0},
    {"aaaaaaaa  bbb", 9, 9, 8, 8, "0..9 0..3", "1..9 5..9", 0},
    {"aaaaaaaa  bbb", 10, 10, 9, 9, "0..10 0..3", "1..10 7..10", 0},
    {"aaaaaaaa  bbb", 11, 10, 9, 9, "0..10 0..3", "2..11 8..11", 0},
    {"aaaaaaaa  bbb", 12, 10, 9, 9, "0..10 0..3", "3..12 9..12", 0},
    {"aaaaaaaa   bbb", 2, 8, 8, 8, "0..2 0..2 0..2", "0..2 1..2 - 0..2", 0},
    {"aaaaaaaa   bbb", 3, 8, 8, 8, "0..3 0..2 0..3", "0..3 1..3 0..3", 0},
    {"aaaaaaaa   bbb", 4, 8, 8, 8, "0..4 0..2 0..3", "0..4 2..4 1..4", 0},
    {"aaaaaaaa   bbb", 5, 8, 8, 8, "0..5 0..5", "0..5 3..5 2..5", 0},
    {"aaaaaaaa   bbb", 6, 8, 8, 8, "0..6 0..5", "0..6 0..6", 0},
    {"aaaaaaaa   bbb", 7, 8, 8, 8, "0..7 0..5", "0..7 1..7", 0},
    {"aaaaaaaa   bbb", 8, 8, 8, 8, "0..8 0..5", "0..8 2..8", 0},
    {"aaaaaaaa   bbb", 9, 9, 8, 8, "0..9 0..4", "1..9 4..9", 0},
    {"aaaaaaaa   bbb", 10, 10, 9, 9, "0..10 0..3", "1..10 6..10", 0},
    {"aaaaaaaa   bbb", 11, 11, 10, 10, "0..11 0..3", "1..11 8..11", 0},
    {"aaaaaaaa   bbb", 12, 11, 10, 10, "0..11 0..3", "2..12 9..12", 0},
    {"aa bbbbbbbb cc", 2, 8, 8, 8, "0..2 0..2 0..2", "0..2 - 0..2 - 0..2", 0},
    {"aa bbbbbbbb cc", 3, 8, 8, 8, "0..3 0..3 0..2", "1..3 0..3 0..3", 0},
    {"aa bbbbbbbb cc", 4, 8, 8, 8, "0..3 0..4 0..2", "2..4 0..4 1..4", 0},
    {"aa bbbbbbbb cc", 5, 8, 8, 8, "0..3 0..5 0..2", "3..5 0..5 2..5", 0},
    {"aa bbbbbbbb cc", 6, 8, 8, 8, "0..3 0..6 0..2", "4..6 0..6 3..6", 0},
    {"aa bbbbbbbb cc", 7, 8, 8, 8, "0..3 0..7 0..2", "5..7 0..7 4..7", 0},
    {"aa bbbbbbbb cc", 8, 8, 8, 8, "0..3 0..8 0..2", "6..8 0..8 5..8", 0},
    {"aa bbbbbbbb cc", 9, 9, 8, 8, "0..3 0..9 0..2", "7..9 1..9 7..9", 0},
    {"aa bbbbbbbb cc", 10, 9, 8, 8, "0..3 0..9 0..2", "8..10 2..10 8..10", 0},
    {"aa bbbbbbbb cc", 11, 11, 11, 11, "0..11 0..2", "0..11 8..11", 0},
    {"aa bbbbbbbb cc", 12, 12, 11, 11, "0..12 0..2", "1..12 10..12", 0},
    {"aa  bbbbbbbb  cc", 2, 8, 8, 8, "0..2 0..1 0..2 0..1 0..2", "0..2 1..2 0..2 1..2 0..2", 0},
    {"aa  bbbbbbbb  cc", 3, 8, 8, 8, "0..3 0..3 0..3", "1..3 - 0..3 2..3 1..3", 0},
    {"aa  bbbbbbbb  cc", 4, 8, 8, 8, "0..4 0..4 0..3", "1..4 0..4 0..4", 0},
    {"aa  bbbbbbbb  cc", 5, 8, 8, 8, "0..4 0..5 0..3", "2..5 0..5 1..5", 0},
    {"aa  bbbbbbbb  cc", 6, 8, 8, 8, "0..4 0..6 0..3", "3..6 0..6 2..6", 0},
    {"aa  bbbbbbbb  cc", 7, 8, 8, 8, "0..4 0..7 0..3", "4..7 0..7 3..7", 0},
    {"aa  bbbbbbbb  cc", 8, 8, 8, 8, "0..4 0..8 0..3", "5..8 0..8 4..8", 0},
    {"aa  bbbbbbbb  cc", 9, 9, 8, 8, "0..4 0..9 0..2", "6..9 1..9 6..9", 0},
    {"aa  bbbbbbbb  cc", 10, 10, 9, 9, "0..4 0..10 0..2", "7..10 1..10 8..10", 0},
    {"aa  bbbbbbbb  cc", 11, 10, 9, 9, "0..4 0..10 0..2", "8..11 2..11 9..11", 0},
    {"aa  bbbbbbbb  cc", 12, 12, 12, 12, "0..12 0..3", "0..12 8..12", 0},
    {"   aaa bbb", 2, 3, 3, 3, "0..2 0..2 0..2", "1..2 - 0..2 - 0..2", 0},
    {"   aaa bbb", 3, 3, 3, 3, "0..3 0..3 0..3", "1..3 0..3 - 0..3", 0},
    {"   aaa bbb", 4, 4, 3, 3, "0..3 0..4 0..3", "2..4 1..4 1..4", 0},
    {"   aaa bbb", 5, 4, 3, 3, "0..3 0..4 0..3", "3..5 2..5 2..5", 0},
    {"   aaa bbb", 6, 6, 6, 6, "0..6 0..3", "0..6 2..6", 0},
    {"   aaa bbb", 7, 7, 6, 6, "0..7 0..3", "1..7 4..7", 0},
    {"   aaa bbb", 8, 7, 6, 6, "0..7 0..3", "2..8 5..8", 0},
    {"   aaa bbb", 9, 7, 6, 6, "0..7 0..3", "3..9 6..9", 0},
    {"   aaa bbb", 10, 10, 10, 10, "0..10", "0..10", 0},
    {"aaa bbb   ", 2, 3, 3, 3, "0..2 0..2 0..2", "0..2 - 0..2 1..2 1..2", 0},
    {"aaa bbb   ", 3, 3, 3, 3, "0..3 0..3 0..2", "0..3 - 0..3 0..3", 0},
    {"aaa bbb   ", 4, 4, 3, 3, "0..4 0..4 0..1", "1..4 1..4 2..4", 0},
    {"aaa bbb   ", 5, 5, 4, 4, "0..4 0..5", "2..5 1..5 4..5", 0},
    {"aaa bbb   ", 6, 6, 6, 6, "0..4 0..6", "3..6 0..6", 0},
    {"aaa bbb   ", 7, 7, 7, 7, "0..7 0..2", "0..7 4..7", 0},
    {"aaa bbb   ", 8, 8, 7, 7, "0..8 0..1", "1..8 6..8", 0},
    {"aaa bbb   ", 9, 9, 8, 8, "0..9", "1..9 8..9", 0},
    {"aaa bbb   ", 10, 10, 10, 10, "0..10", "0..10", 0},
    {"aaa \nbbb", 2, 3, 3, 3, "0..2 - 0..2", "0..2 1..2 0..2", 0},
    {"aaa \nbbb", 3, 3, 3, 3, "0..3 - 0..3", "0..3 2..3 0..3", 0},
    {"aaa \nbbb", 4, 4, 4, 4, "0..4 0..3", "0..4 1..4", 0},
    {"aaa \nbbb", 5, 4, 4, 4, "0..4 0..3", "1..5 2..5", 0},
    {"aaa \nbbb", 6, 4, 4, 4, "0..4 0..3", "2..6 3..6", 0},
    {"aaa \nbbb", 7, 4, 4, 4, "0..4 0..3", "3..7 4..7", 0},
    {"aaa \nbbb", 8, 4, 4, 4, "0..4 0..3", "4..8 5..8", 0},
    {"aaa  \nbbb", 2, 3, 3, 3, "0..2 0..1 0..2", "0..2 0..2 0..2", 0},
    {"aaa  \nbbb", 3, 3, 3, 3, "0..3 0..1 0..3", "0..3 1..3 0..3", 0},
    {"aaa  \nbbb", 4, 4, 3, 3, "0..4 - 0..3", "1..4 3..4 1..4", 0},
    {"aaa  \nbbb", 5, 5, 5, 5, "0..5 0..3", "0..5 2..5", 0},
    {"aaa  \nbbb", 6, 5, 5, 5, "0..5 0..3", "1..6 3..6", 0},
    {"aaa  \nbbb", 7, 5, 5, 5, "0..5 0..3", "2..7 4..7", 0},
    {"aaa  \nbbb", 8, 5, 5, 5, "0..5 0..3", "3..8 5..8", 0},
    {"aaa  \nbbb", 9, 5, 5, 5, "0..5 0..3", "4..9 6..9", 0},
    {"aaa \n bbb", 2, 3, 3, 3, "0..2 - 0..1 0..2", "0..2 1..2 - 0..2", 0},
    {"aaa \n bbb", 3, 3, 3, 3, "0..3 - 0..1 0..3", "0..3 2..3 - 0..3", 0},
    {"aaa \n bbb", 4, 4, 4, 4, "0..4 0..4", "0..4 0..4", 0},
    {"aaa \n bbb", 5, 4, 4, 4, "0..4 0..4", "1..5 1..5", 0},
    {"aaa \n bbb", 6, 4, 4, 4, "0..4 0..4", "2..6 2..6", 0},
    {"aaa \n bbb", 7, 4, 4, 4, "0..4 0..4", "3..7 3..7", 0},
    {"aaa \n bbb", 8, 4, 4, 4, "0..4 0..4", "4..8 4..8", 0},
    {"aaa \n bbb", 9, 4, 4, 4, "0..4 0..4", "5..9 5..9", 0},
    {" ", 2, 1, 1, 1, "0..1", "1..2", 0},
    {" ", 3, 1, 1, 1, "0..1", "2..3", 0},
    {"   ", 2, 2, 1, 1, "0..2", "1..2 1..2", 0},
    {"   ", 3, 3, 3, 3, "0..3", "0..3", 0},
    {"a   ", 2, 2, 2, 1, "0..2 0..1", "1..2 0..2", 0},
    {"a   ", 3, 3, 2, 2, "0..3", "1..3 2..3", 0},
    {"a   ", 4, 4, 4, 4, "0..4", "0..4", 0},
    {"aaa\tbbb", 4, 3, 3, 3, "0..3 0..3", "1..4 1..4", 0},
    {"aaa\t\tbbb", 4, 3, 3, 3, "0..3 0..3", "1..4 1..4", 0},
    {"aaa \tbbb", 4, 4, 4, 4, "0..4 0..3", "0..4 1..4", 0},
    {"aaa\t bbb", 4, 4, 3, 3, "0..4 0..3", "1..4 1..4", 0},
    {"aaa\tbbb", 4, 3, 3, 3, "0..3 0..3", "1..4 - 1..4", kExpandTabs},
    {"aaa\tbbb", 9, 8, 3, 3, "0..8 0..3", "6..9 6..9", kExpandTabs},
    {"aaa\t\tbbb", 9, 8, 3, 3, "0..8 0..3", "6..9 - 6..9", kExpandTabs},
    {"aaa\tbbb ccc", 12, 12, 11, 11, "0..12 0..3", "1..12 9..12", kExpandTabs},
    {"aaa\tbbb ccc", 11, 11, 11, 11, "0..11 0..3", "0..11 7..11", kExpandTabs},
    {"a&a bbb", 3, 3, 3, 3, "0..3 0..3", "1..3 0..3", 0},
    {"aaa &bbb", 4, 4, 3, 3, "0..4 0..3", "1..4 1..4", 0},
    {"aaa& bbb", 4, 4, 3, 3, "0..4 0..3", "1..4 1..4", 0},
};

// What the underline probe saw: each line's [x, x + width) clipped to the rectangle, in cells.
std::string LineExtents(const TextLayout& layout, int cells) {
    std::string out;
    for (const TextLine& line : layout.lines) {
        const int x0 = std::max(line.x - layout.rect.left, 0);
        const int x1 = std::min(line.x - layout.rect.left + line.width, cells * kCell);
        if (!out.empty()) out += ' ';
        out += x1 <= x0 ? std::string("-") : std::to_string(x0 / kCell) + ".." + std::to_string(x1 / kCell);
    }
    return out;
}

int CheckRules() {
    TextFont font(std::make_unique<FixedFont>(), false);
    int failures = 0, checks = 0;
    auto fail = [&](const std::string& what) {
        ++failures;
        std::printf("  FAIL %s\n", what.c_str());
    };
    for (const RuleCase& rule : kRuleCases) {
        const std::u32string text = DecodeCp1251(rule.text);
        const Rect rect{0, 0, rule.cells * kCell, 1000};
        const unsigned base = kWordBreak | rule.extra;
        const unsigned aligns[3] = {kLeft, kRight, kCenter};
        const int calc[3] = {rule.calcLeft, rule.calcRight, rule.calcCentre};
        for (int i = 0; i < 3; ++i) {
            TextLayout layout;
            Layout(font, text, rect, base | aligns[i] | kCalcRect, layout);
            ++checks;
            if (layout.rect.right != calc[i] * kCell)
                fail("\"" + Utf8(text) + "\" W" + std::to_string(rule.cells) + " " + FormatName(base | aligns[i] | kCalcRect) +
                     ": width " + std::to_string(layout.rect.right / kCell) + ", DrawText " + std::to_string(calc[i]));
        }
        for (int i = 0; i < 2; ++i) {
            TextLayout layout;
            Layout(font, text, rect, base | aligns[i], layout);
            const std::string got = LineExtents(layout, rule.cells);
            const char* want = i == 0 ? rule.left : rule.right;
            ++checks;
            if (got != want)
                fail("\"" + Utf8(text) + "\" W" + std::to_string(rule.cells) + " " + FormatName(base | aligns[i]) + ": lines [" + got +
                     "], DrawText [" + want + "]");
        }
    }

    // Line breaks, prefixes, tabs, the return value (measured with Times New Roman 13 px; restated in cells).
    struct Simple {
        const char* text;
        unsigned format;
        int height;    // rectangle height (0: tall)
        int result;    // return value in lines (-1: the special value 1 of an empty text)
        int width;     // DT_CALCRECT width in cells
    };
    const Simple simples[] = {
        {"", 0, 0, -1, 0},
        {"abc", 0, 0, 1, 3},
        {"abc\n", 0, 0, 2, 3},
        {"abc\r\n", 0, 0, 2, 3},
        {"\n", 0, 0, 2, 0},
        {"\n\n", 0, 0, 3, 0},
        {"\r\n\r\n", 0, 0, 3, 0},
        {"\n\r", 0, 0, 2, 0},
        {"\r\r", 0, 0, 3, 0},
        {"a\n\rb", 0, 0, 2, 1},
        {"a\r\rb", 0, 0, 3, 1},
        {"abc   ", kRight, 0, 1, 6},
        {"abc   ", kRight | kWordBreak, 0, 1, 6},
        {"&abc", 0, 0, 1, 3},
        {"a&&b", 0, 0, 1, 3},
        {"a&&b", kNoPrefix, 0, 1, 4},
        {"abc&", 0, 0, 1, 3},
        {"&ab&cd", 0, 0, 1, 4},
        {"a\tb", 0, 0, 1, 2},
        {"a\tb", kExpandTabs, 0, 1, 9},
        {"abc", kVCenter, 0, 1, 3},
        {"a\nb\nc\nd\ne\nf", 0, 20, 2, 1},
        {"aaa bbb ccc ddd eee fff ggg", kWordBreak, 20, 2, 4},
    };
    for (const Simple& simple : simples) {
        const std::u32string text = DecodeCp1251(simple.text);
        Rect rect{0, 0, 300, simple.height ? simple.height : 1000};
        if (simple.height) rect.right = 50;
        TextLayout drawn, measured;
        Layout(font, text, rect, simple.format, drawn);
        Layout(font, text, rect, simple.format | kCalcRect, measured);
        const int want = simple.result < 0 ? 1 : simple.result * 18;
        ++checks;
        if (drawn.result != want)
            fail("\"" + Utf8(text) + "\" " + FormatName(simple.format) + ": returns " + std::to_string(drawn.result) + ", DrawText " +
                 std::to_string(want));
        ++checks;
        if (measured.rect.right != simple.width * kCell)
            fail("\"" + Utf8(text) + "\" " + FormatName(simple.format | kCalcRect) + ": width " + std::to_string(measured.rect.right) +
                 ", DrawText " + std::to_string(simple.width * kCell));
    }
    // Underlines: the last prefix character's cell, one pixel at ascent + 1.
    struct Underline {
        const char* text;
        int cell;
    };
    for (const Underline& underline : {Underline{"&abc", 0}, Underline{"&ab&cd", 2}, Underline{"a&&b", -1}}) {
        TextLayout layout;
        Layout(font, DecodeCp1251(underline.text), Rect{0, 0, 300, 100}, 0, layout);
        ++checks;
        const bool ok = underline.cell < 0 ? layout.underlines.empty()
                                           : layout.underlines.size() == 1 && layout.underlines[0].left == underline.cell * kCell &&
                                                 layout.underlines[0].right == underline.cell * kCell + kCell &&
                                                 layout.underlines[0].top == 15 && layout.underlines[0].bottom == 16;
        if (!ok) fail(std::string("underline of \"") + underline.text + "\"");
    }
    std::printf("rules: %d checks, %d failed\n", checks, failures);
    return failures;
}

// ---- GDI (only in this tool) --------------------------------------------------------------------------

struct Dib {
    HDC dc = nullptr;
    HBITMAP bitmap = nullptr;
    HGDIOBJ old = nullptr;
    std::uint32_t* pixels = nullptr;
    int width = 0, height = 0;

    Dib(int w, int h) : width(w), height(h) {
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(info.bmiHeader);
        info.bmiHeader.biWidth = w;
        info.bmiHeader.biHeight = -h;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        dc = CreateCompatibleDC(nullptr);
        void* bits = nullptr;
        bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
        pixels = static_cast<std::uint32_t*>(bits);
        old = SelectObject(dc, bitmap);
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(255, 255, 255));
    }
    ~Dib() {
        SelectObject(dc, old);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }
    Dib(const Dib&) = delete;
    Dib& operator=(const Dib&) = delete;
    void Clear() { std::memset(pixels, 0, static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4); }
};

HFONT MakeGdiFont(const char* face, int pixels, BYTE quality) {
    LOGFONTA logFont{};
    logFont.lfHeight = -pixels;
    logFont.lfCharSet = RUSSIAN_CHARSET;   // DrawTextA then reads the bytes as 1251, as on a Russian system
    logFont.lfQuality = quality;
    strcpy_s(logFont.lfFaceName, face);
    return CreateFontIndirectA(&logFont);
}

// A Font over GDI's own metrics and glyphs (single characters through TextOutW), so the layout can be
// compared with DrawTextA exactly.
class GdiFont final : public Font {
public:
    explicit GdiFont(HFONT font) : scratch_(1, 1) {
        SelectObject(scratch_.dc, font);
        TEXTMETRICW tm;
        GetTextMetricsW(scratch_.dc, &tm);
        overhang_ = tm.tmOverhang;
        metrics_.ascent = tm.tmAscent;
        metrics_.descent = tm.tmDescent;
        metrics_.height = tm.tmHeight;
        metrics_.lineGap = tm.tmExternalLeading;
        metrics_.averageWidth = tm.tmAveCharWidth;
        pad_ = tm.tmHeight;
        glyphDib_ = std::make_unique<Dib>(tm.tmMaxCharWidth + 4 * pad_, tm.tmHeight + 2 * pad_);
        SelectObject(glyphDib_->dc, font);
        const DWORD pairs = GetKerningPairsW(scratch_.dc, 0, nullptr);
        std::vector<KERNINGPAIR> table(pairs);
        if (pairs) GetKerningPairsW(scratch_.dc, pairs, table.data());
        for (const KERNINGPAIR& pair : table)
            if (pair.iKernAmount) kerning_[(std::uint64_t(pair.wFirst) << 32) | pair.wSecond] = pair.iKernAmount;
    }
    const FontMetrics& Metrics() const override { return metrics_; }
    GlyphMetrics Glyph(char32_t ch) const override { return Entry(ch).metrics; }
    int Kerning(char32_t left, char32_t right) const override {
        auto found = kerning_.find((std::uint64_t(left) << 32) | right);
        return found == kerning_.end() ? 0 : found->second;
    }
    void Rasterise(char32_t ch, std::uint8_t* out, int pitch) const override {
        const GlyphEntry& entry = Entry(ch);
        for (int y = 0; y < entry.metrics.height; ++y)
            std::memcpy(out + y * pitch, &entry.coverage[static_cast<std::size_t>(y * entry.metrics.width)], static_cast<std::size_t>(entry.metrics.width));
    }

private:
    struct GlyphEntry {
        GlyphMetrics metrics;
        std::vector<std::uint8_t> coverage;
    };
    const GlyphEntry& Entry(char32_t ch) const {
        auto found = glyphs_.find(ch);
        if (found != glyphs_.end()) return found->second;
        GlyphEntry& entry = glyphs_[ch];
        const wchar_t wide = static_cast<wchar_t>(ch);
        SIZE size{};
        GetTextExtentPoint32W(scratch_.dc, &wide, 1, &size);
        entry.metrics.advance = size.cx - overhang_;
        if (ch < 0x20) {
            // DrawText (Uniscribe) gives control characters their own width, not GetTextExtentPoint32's.
            const wchar_t with[3] = {L'a', wide, L'a'};
            RECT a{0, 0, 10000, 100}, b{0, 0, 10000, 100};
            DrawTextW(scratch_.dc, with, 3, &a, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
            const wchar_t without[2] = {L'a', L'a'};
            DrawTextW(scratch_.dc, without, 2, &b, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
            entry.metrics.advance = a.right - b.right;
        }
        Dib& dib = *glyphDib_;
        dib.Clear();
        TextOutW(dib.dc, pad_, pad_, &wide, 1);
        GdiFlush();
        int x0 = dib.width, y0 = dib.height, x1 = -1, y1 = -1;
        for (int y = 0; y < dib.height; ++y)
            for (int x = 0; x < dib.width; ++x)
                if (dib.pixels[y * dib.width + x] & 0xFFFFFF) {
                    x0 = std::min(x0, x);
                    y0 = std::min(y0, y);
                    x1 = std::max(x1, x);
                    y1 = std::max(y1, y);
                }
        if (x1 >= x0) {
            entry.metrics.left = x0 - pad_;
            entry.metrics.top = y0 - pad_ - metrics_.ascent;
            entry.metrics.width = x1 - x0 + 1;
            entry.metrics.height = y1 - y0 + 1;
            entry.coverage.resize(static_cast<std::size_t>(entry.metrics.width * entry.metrics.height));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const std::uint32_t pixel = dib.pixels[y * dib.width + x];
                    const int value = std::max({int(pixel & 0xFF), int((pixel >> 8) & 0xFF), int((pixel >> 16) & 0xFF)});
                    entry.coverage[static_cast<std::size_t>((y - y0) * entry.metrics.width + (x - x0))] = static_cast<std::uint8_t>(value);
                }
        }
        return entry;
    }

    Dib scratch_;
    std::unique_ptr<Dib> glyphDib_;
    int pad_ = 0, overhang_ = 0;
    FontMetrics metrics_;
    mutable std::map<char32_t, GlyphEntry> glyphs_;
    std::map<std::uint64_t, int> kerning_;
};

// ---- The cases ----------------------------------------------------------------------------------------

struct TestString {
    std::string bytes;   // Windows-1251, as the game hands them to DrawTextA
    bool synthetic;
};

// texts.res: the game's text database (a .res archive of text files; header magic 0x019CE23C, a table of
// 22-byte entries). Each file holds zero-separated chunks of CR LF lines; every chunk and every line of it
// is a test string.
std::vector<TestString> LoadTexts(const std::string& path) {
    std::vector<TestString> out;
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) {
        std::printf("cannot read %s: no game strings\n", path.c_str());
        return out;
    }
    std::vector<unsigned char> data;
    std::fseek(file, 0, SEEK_END);
    data.resize(static_cast<std::size_t>(std::ftell(file)));
    std::fseek(file, 0, SEEK_SET);
    const std::size_t read = std::fread(data.data(), 1, data.size(), file);
    std::fclose(file);
    if (read != data.size() || data.size() < 16) return out;
    auto u32 = [&](std::size_t at) {
        std::uint32_t value;
        std::memcpy(&value, &data[at], 4);
        return value;
    };
    if (u32(0) != 0x019CE23C) return out;
    const std::uint32_t count = u32(4), table = u32(8);
    std::map<std::string, bool> seen;
    auto add = [&](const std::string& text) {
        if (text.empty() || text.size() > 3000 || seen.count(text)) return;
        seen[text] = true;
        out.push_back({text, false});
    };
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t entry = table + static_cast<std::size_t>(i) * 22;
        if (entry + 22 > data.size()) break;
        const std::uint32_t size = u32(entry + 4), offset = u32(entry + 8);
        if (offset + size > data.size()) continue;
        const std::string content(reinterpret_cast<const char*>(&data[offset]), size);
        std::size_t start = 0;
        while (start < content.size()) {
            std::size_t end = content.find('\0', start);
            if (end == std::string::npos) end = content.size();
            const std::string chunk = content.substr(start, end - start);
            add(chunk);
            std::size_t lineStart = 0;
            while (lineStart < chunk.size()) {
                std::size_t lineEnd = chunk.find("\r\n", lineStart);
                if (lineEnd == std::string::npos) lineEnd = chunk.size();
                add(chunk.substr(lineStart, lineEnd - lineStart));
                lineStart = lineEnd + 2;
            }
            start = end + 1;
        }
    }
    return out;
}

std::vector<TestString> SyntheticStrings() {
    std::vector<TestString> out;
    const char* fixed[] = {
        "", " ", "   ", "a", "&", "&&", "&a", "a&", "&ab&cd", "a&&b&c", "x & y", "Fire&&Ice", "\t", "a\tb", "a\t\tb", "\ta",
        "a \tb", "a\t b", "Имя:\tЗначение\tЕщё", "\n", "\r", "\r\n", "\n\r", "\r\r", "\n\n", "a\n", "a\r\n", "a\n\rb", "a\r\rb",
        "a\r\n\r\nb", "  leading", "trailing  ", "  both  ", "word", "Supercalifragilisticexpialidocious and more",
        "Очень_длинное_слово_без_пробелов_которое_не_помещается в строку", "one two  three   four    five",
        "Привет, мир! Это тест переноса строк в интерфейсе игры.", "Ёжик ёлка Йод й", "\x85\x96\x97\xAB\xBB\xB9",
        "a\x07" "b\x01" "c", "end with newline\r\n", "&File &Edit &View", "tab\tafter space \t and\t\ttwo",
    };
    for (const char* text : fixed) {
        // The Cyrillic literals are UTF-8 in this file; the game's bytes are 1251.
        std::string bytes = text;
        wchar_t wide[512];
        const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, wide, 512);
        if (count > 0) {
            char narrow[512];
            if (WideCharToMultiByte(1251, 0, wide, count, narrow, sizeof narrow, nullptr, nullptr) > 0) bytes = narrow;
        }
        out.push_back({bytes, true});
    }
    // Random strings over an alphabet that stresses the rules.
    std::mt19937 random(1234);
    const char* pieces[] = {"a", "b", "W", "i", "l", "m", "\xDF", "\xE6", " ", " ", " ", "  ", "\t", "\n", "\r", "\r\n", "&", "&&", "word", "WWW", "iii",
                            "\xEF\xF0\xE8\xE2\xE5\xF2"};
    for (int i = 0; i < 1000; ++i) {
        std::string text;
        const int count = static_cast<int>(random() % 24);
        for (int j = 0; j < count; ++j) text += pieces[random() % (sizeof pieces / sizeof pieces[0])];
        out.push_back({text, true});
    }
    return out;
}

// The interface fonts as CInterface3D::Create (005F4990) makes them, and the console's.
struct FontSpec {
    const char* face;
    int tenths;
    int screenWidth, screenHeight;
    std::string name;
};

std::vector<FontSpec> FontSpecs() {
    const int resolutions[5][2] = {{800, 600}, {1024, 768}, {1280, 720}, {1920, 1080}, {3840, 2160}};
    const int shares[3] = {104, 112, 144};
    // The scale: a 75-point font's pixel height / 100 (100 px at 96 dpi).
    const float scale = static_cast<float>(PointSizeToPixels(750)) * 0.01f;
    std::vector<FontSpec> out;
    for (const auto& resolution : resolutions) {
        for (int i = 0; i < 3; ++i) {
            const float size = static_cast<float>(resolution[0] * shares[i]) / scale * (1.0f / 800.0f);
            const int tenths = static_cast<int>(size);
            out.push_back({"Times New Roman", tenths, resolution[0], resolution[1],
                           std::to_string(resolution[0]) + "x" + std::to_string(resolution[1]) + " font" + std::to_string(i) + " (" +
                               std::to_string(tenths) + ")"});
        }
    }
    out.push_back({"Courier New", 90, 800, 600, "console (90)"});
    return out;
}

const unsigned kFormats[] = {
    0, kWordBreak, kWordBreak | kCenter, kWordBreak | kRight, kWordBreak | kExpandTabs, kCenter | kVCenter, kWordBreak | kNoPrefix, kRight,
};
const int kWidths[] = {90, 220, 420};   // in the 800 x 600 layout, scaled to the screen

struct Stats {
    long long layouts = 0, layoutMismatches = 0, pixelCases = 0, pixelExact = 0, pixelMismatches = 0;
    long long reportCases = 0, breakDifferences = 0, resultDifferences = 0, extentCases = 0, extentDifferent = 0;
    long long extentAbsSum = 0, extentWorst = 0, extentSignedSum = 0, extentGdiSum = 0;
    std::map<std::string, std::pair<long long, long long>> breaksPerFont;   // strings, differing
    std::vector<std::string> examples;
    void Merge(const Stats& other) {
        layouts += other.layouts;
        layoutMismatches += other.layoutMismatches;
        pixelCases += other.pixelCases;
        pixelExact += other.pixelExact;
        pixelMismatches += other.pixelMismatches;
        reportCases += other.reportCases;
        breakDifferences += other.breakDifferences;
        resultDifferences += other.resultDifferences;
        extentCases += other.extentCases;
        extentDifferent += other.extentDifferent;
        extentAbsSum += other.extentAbsSum;
        extentSignedSum += other.extentSignedSum;
        extentGdiSum += other.extentGdiSum;
        extentWorst = std::max(extentWorst, other.extentWorst);
        for (const auto& [key, value] : other.breaksPerFont) {
            breaksPerFont[key].first += value.first;
            breaksPerFont[key].second += value.second;
        }
        examples.insert(examples.end(), other.examples.begin(), other.examples.end());
    }
};

struct Options {
    bool gdi = false, report = false;
    int threads = 8, pixelsEvery = 4, examples = 20;
    int onlyFont = -1;   // --font K: only the Kth font of FontSpecs()
};

// One worker: every font, the strings with index % threads == worker.
void Work(const std::vector<TestString>& strings, const std::vector<FontSpec>& specs, const Options& options, int worker, Stats& stats) {
    const int maxExamples = options.examples;
    for (const FontSpec& spec : specs) {
        const int pixels = PointSizeToPixels(spec.tenths);
        const HFONT hfont = MakeGdiFont(spec.face, pixels, NONANTIALIASED_QUALITY);
        TextFont gdiFont(std::make_unique<GdiFont>(hfont), false);
        std::unique_ptr<TextFont> stbFont;
        if (options.report) {
            if (auto font = LoadFont(FontDirectory() + "/" + FontFileForFace(spec.face), pixels)) stbFont = std::make_unique<TextFont>(std::move(font), true);
        }
        Dib measure(1, 1);
        SelectObject(measure.dc, hfont);
        const int lineHeight = gdiFont.Metrics().height;
        long long fontStrings = 0, fontDiffering = 0;
        for (std::size_t index = static_cast<std::size_t>(worker); index < strings.size(); index += static_cast<std::size_t>(options.threads)) {
            const TestString& test = strings[index];
            const std::u32string text = DecodeCp1251(test.bytes.c_str(), static_cast<int>(test.bytes.size()));
            bool breaksDiffer = false;
            int variant = 0;
            for (unsigned format : kFormats) {
                for (int width : kWidths) {
                    ++variant;
                    const int rectWidth = width * spec.screenWidth / 800;
                    const Rect rect{7, 3, 7 + rectWidth, 3 + 100000};
                    if (options.gdi) {
                        // Return value and DT_CALCRECT.
                        RECT gdiRect{rect.left, rect.top, rect.right, rect.bottom};
                        const int gdiResult = DrawTextA(measure.dc, test.bytes.c_str(), -1, &gdiRect, format | DT_CALCRECT);
                        TextLayout ours;
                        Layout(gdiFont, text, rect, format | kCalcRect, ours);
                        ++stats.layouts;
                        const bool same = gdiResult == ours.result && gdiRect.left == ours.rect.left && gdiRect.top == ours.rect.top &&
                                          gdiRect.right == ours.rect.right && gdiRect.bottom == ours.rect.bottom;
                        if (!same) {
                            ++stats.layoutMismatches;
                            if (static_cast<int>(stats.examples.size()) < maxExamples) {
                                char buffer[256];
                                std::snprintf(buffer, sizeof buffer, "CALCRECT %s %s w%d: GDI %d [%d,%d,%d,%d], ours %d [%d,%d,%d,%d]: ",
                                              spec.name.c_str(), FormatName(format).c_str(), rectWidth, gdiResult, int(gdiRect.left), int(gdiRect.top),
                                              int(gdiRect.right), int(gdiRect.bottom), ours.result, ours.rect.left, ours.rect.top, ours.rect.right,
                                              ours.rect.bottom);
                                stats.examples.push_back(buffer + Shorten(Utf8(text)));
                            }
                        }
                        // Pixels, for every Nth case (and every synthetic one), in rectangles of a few heights.
                        const bool pixelCase = test.synthetic || (static_cast<int>(index) + variant) % options.pixelsEvery == 0;
                        if (pixelCase && gdiResult < 4000) {
                            const int heights[3] = {gdiResult + lineHeight, lineHeight, 2 * lineHeight + 1};
                            for (int h = 0; h < (test.synthetic && (format & kWordBreak) ? 3 : 1); ++h) {
                                const Rect drawRect{8, 4, 8 + rectWidth, 4 + heights[h]};
                                Dib gdi(rectWidth + 16, heights[h] + 8);
                                SelectObject(gdi.dc, hfont);
                                RECT drawGdi{drawRect.left, drawRect.top, drawRect.right, drawRect.bottom};
                                const int drawnResult = DrawTextA(gdi.dc, test.bytes.c_str(), -1, &drawGdi, format);
                                GdiFlush();
                                TextLayout drawn;
                                Layout(gdiFont, text, drawRect, format, drawn);
                                std::vector<std::uint32_t> mine(static_cast<std::size_t>(gdi.width) * static_cast<std::size_t>(gdi.height), 0);
                                ComposeAgainstBlack(gdiFont, drawn, 0xFFFFFF, mine.data(), gdi.width, gdi.height, gdi.width);
                                long long differing = 0;
                                for (std::size_t i = 0; i < mine.size(); ++i)
                                    if (((gdi.pixels[i] & 0xFFFFFF) != 0) != ((mine[i] & 0xFFFFFF) != 0)) ++differing;
                                ++stats.pixelCases;
                                if (!differing && drawnResult == drawn.result) ++stats.pixelExact;
                                else {
                                    ++stats.pixelMismatches;
                                    if (static_cast<int>(stats.examples.size()) < maxExamples) {
                                        char buffer[256];
                                        std::snprintf(buffer, sizeof buffer, "PIXELS %s %s %dx%d: %lld pixels differ, GDI returns %d, ours %d: ",
                                                      spec.name.c_str(), FormatName(format).c_str(), rectWidth, heights[h], differing, drawnResult,
                                                      drawn.result);
                                        stats.examples.push_back(buffer + Shorten(Utf8(text)));
                                    }
                                }
                            }
                        }
                    }
                    if (stbFont && !test.synthetic) {
                        TextLayout gdiLines, stbLines;
                        Layout(gdiFont, text, rect, format | kCalcRect, gdiLines);
                        Layout(*stbFont, text, rect, format | kCalcRect, stbLines);
                        ++stats.reportCases;
                        bool differ = gdiLines.lines.size() != stbLines.lines.size();
                        for (std::size_t i = 0; !differ && i < gdiLines.lines.size(); ++i)
                            differ = gdiLines.lines[i].begin != stbLines.lines[i].begin || gdiLines.lines[i].end != stbLines.lines[i].end;
                        if (differ) {
                            ++stats.breakDifferences;
                            breaksDiffer = true;
                        }
                        if (gdiLines.result != stbLines.result) ++stats.resultDifferences;
                    }
                }
            }
            if (stbFont && !test.synthetic) {
                ++fontStrings;
                if (breaksDiffer) ++fontDiffering;
                if (test.bytes.find_first_of("\r\n") == std::string::npos) {
                    const int gdiWidth = TextExtent(gdiFont, text).cx, stbWidth = TextExtent(*stbFont, text).cx;
                    ++stats.extentCases;
                    if (gdiWidth != stbWidth) ++stats.extentDifferent;
                    const long long difference = std::abs(gdiWidth - stbWidth);
                    stats.extentAbsSum += difference;
                    stats.extentSignedSum += stbWidth - gdiWidth;
                    stats.extentGdiSum += gdiWidth;
                    stats.extentWorst = std::max(stats.extentWorst, difference);
                }
            }
        }
        if (options.report) {
            stats.breaksPerFont[spec.name].first += fontStrings;
            stats.breaksPerFont[spec.name].second += fontDiffering;
        }
        DeleteObject(hfont);
    }
}

int RunGdi(const std::string& textsPath, const Options& options) {
    // The 1251 table against Windows' own.
    int tableErrors = 0;
    for (int byte = 1; byte < 256; ++byte) {
        const char narrow = static_cast<char>(byte);
        wchar_t wide = 0;
        MultiByteToWideChar(1251, 0, &narrow, 1, &wide, 1);
        if (DecodeCp1251(&narrow, 1)[0] != static_cast<char32_t>(wide)) {
            ++tableErrors;
            std::printf("  1251 byte %02X: ours U+%04X, Windows U+%04X\n", byte, unsigned(DecodeCp1251(&narrow, 1)[0]), unsigned(wide));
        }
    }
    std::printf("code page 1251: %d bytes differ from MultiByteToWideChar\n", tableErrors);

    std::vector<TestString> strings = LoadTexts(textsPath);
    const std::size_t gameStrings = strings.size();
    if (options.gdi) {
        std::vector<TestString> synthetic = SyntheticStrings();
        strings.insert(strings.end(), synthetic.begin(), synthetic.end());
    }
    std::vector<FontSpec> specs = FontSpecs();
    if (options.onlyFont >= 0 && options.onlyFont < static_cast<int>(specs.size())) specs = {specs[static_cast<std::size_t>(options.onlyFont)]};
    std::printf("strings: %zu from texts.res, %zu synthetic; fonts: %zu (5 resolutions x 3 interface fonts + console); %zu formats x %zu widths\n",
                gameStrings, strings.size() - gameStrings, specs.size(), std::size(kFormats), std::size(kWidths));
    // Font metrics side by side.
    if (options.report) {
        std::printf("metrics (GDI / Liberation): height, ascent, average width\n");
        for (const FontSpec& spec : specs) {
            const int pixels = PointSizeToPixels(spec.tenths);
            const HFONT hfont = MakeGdiFont(spec.face, pixels, NONANTIALIASED_QUALITY);
            GdiFont gdi(hfont);
            auto stb = LoadFont(FontDirectory() + "/" + FontFileForFace(spec.face), pixels);
            if (stb)
                std::printf("  %-28s %3d px: %3d/%3d %3d/%3d %3d/%3d\n", spec.name.c_str(), pixels, gdi.Metrics().height, stb->Metrics().height,
                            gdi.Metrics().ascent, stb->Metrics().ascent, gdi.Metrics().averageWidth, stb->Metrics().averageWidth);
            else std::printf("  %s: cannot load %s/%s\n", spec.name.c_str(), FontDirectory().c_str(), FontFileForFace(spec.face));
            DeleteObject(hfont);
        }
    }

    const auto start = std::chrono::steady_clock::now();
    std::vector<Stats> stats(static_cast<std::size_t>(options.threads));
    std::vector<std::thread> threads;
    for (int i = 0; i < options.threads; ++i)
        threads.emplace_back([&, i] { Work(strings, specs, options, i, stats[static_cast<std::size_t>(i)]); });
    for (std::thread& thread : threads) thread.join();
    Stats total;
    for (const Stats& part : stats) total.Merge(part);
    const double seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    for (std::size_t i = 0; i < total.examples.size() && static_cast<int>(i) < options.examples; ++i) std::printf("  %s\n", total.examples[i].c_str());
    int failures = 0;
    if (options.gdi) {
        std::printf("gdi: %lld layouts (return value and DT_CALCRECT), %lld differ; %lld drawn cases compared pixel for pixel, %lld exact, %lld differ\n",
                    total.layouts, total.layoutMismatches, total.pixelCases, total.pixelExact, total.pixelMismatches);
        failures += static_cast<int>(std::min<long long>(total.layoutMismatches + total.pixelMismatches, 1000000)) + tableErrors;
    }
    if (options.report) {
        std::printf("report (Liberation vs GDI, game strings only):\n");
        for (const auto& [name, counts] : total.breaksPerFont)
            std::printf("  %-28s %lld strings, %lld break differently in some format / width\n", name.c_str(), counts.first, counts.second);
        std::printf("  %lld layouts, %lld with different line breaks, %lld with a different height\n", total.reportCases, total.breakDifferences,
                    total.resultDifferences);
        std::printf("  extents of %lld single-line strings: %lld differ, mean |difference| %.2f px, worst %lld px\n", total.extentCases,
                    total.extentDifferent, total.extentCases ? double(total.extentAbsSum) / double(total.extentCases) : 0.0, total.extentWorst);
        std::printf("  Liberation minus GDI over all of them: %+.2f%% of the GDI width\n",
                    total.extentGdiSum ? 100.0 * double(total.extentSignedSum) / double(total.extentGdiSum) : 0.0);
    }
    std::printf("(%.1f s, %d threads)\n", seconds, options.threads);
    return failures;
}

// --explain <text> <rect width> <format>: our lines with the first font, and what GDI measures for each.
// The text is read from the wide command line; \n, \r and \t escapes give line breaks and tabs.
int Explain(int rectWidth, unsigned format, int fontIndex) {
    int argc = 0;
    wchar_t** wargv = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::wstring wide;
    for (const wchar_t* p = wargv[2]; *p; ++p) {
        if (*p == L'\\' && p[1] == L'n') { wide += L'\n'; ++p; }
        else if (*p == L'\\' && p[1] == L'r') { wide += L'\r'; ++p; }
        else if (*p == L'\\' && p[1] == L't') { wide += L'\t'; ++p; }
        else wide += *p;
    }
    LocalFree(wargv);
    char narrow[4096] = {};
    WideCharToMultiByte(1251, 0, wide.c_str(), -1, narrow, sizeof narrow, nullptr, nullptr);
    const std::string bytes = narrow;
    const FontSpec spec = FontSpecs()[static_cast<std::size_t>(fontIndex)];
    const HFONT hfont = MakeGdiFont(spec.face, PointSizeToPixels(spec.tenths), NONANTIALIASED_QUALITY);
    TextFont font(std::make_unique<GdiFont>(hfont), false);
    Dib dib(1, 1);
    SelectObject(dib.dc, hfont);
    RECT gdiRect{0, 0, rectWidth, 10000};
    const int gdiResult = DrawTextA(dib.dc, bytes.c_str(), -1, &gdiRect, format | DT_CALCRECT);
    std::printf("GDI: %d, width %d\n", gdiResult, int(gdiRect.right));
    const std::u32string text = DecodeCp1251(bytes.c_str());
    TextLayout layout;
    Layout(font, text, Rect{0, 0, rectWidth, 10000}, format | kCalcRect, layout);
    std::printf("ours: %d, width %d\n", layout.result, layout.rect.right);
    for (int height : {15, 31, 46, 10000}) {
        RECT drawRect{0, 0, rectWidth, height};
        const int drawn = DrawTextA(dib.dc, bytes.c_str(), -1, &drawRect, format);
        TextLayout ours;
        Layout(font, text, Rect{0, 0, rectWidth, height}, format, ours);
        std::printf("  height %d: GDI draws %d, ours %d (%zu lines)\n", height, drawn, ours.result, ours.lines.size());
    }
    Layout(font, text, Rect{0, 0, rectWidth, 10000}, format, layout);
    for (const TextLine& line : layout.lines) {
        const std::string part = bytes.substr(static_cast<std::size_t>(line.begin), static_cast<std::size_t>(line.end - line.begin));
        RECT one{0, 0, 10000, 100};
        DrawTextA(dib.dc, part.c_str(), -1, &one, DT_CALCRECT | DT_NOPREFIX);
        SIZE extent{};
        GetTextExtentPoint32A(dib.dc, part.c_str(), static_cast<int>(part.size()), &extent);
        std::printf("  [%d,%d) ours %d, DrawText %d, extent %d: %s\n", line.begin, line.end, line.width, int(one.right), int(extent.cx),
                    Utf8(text.substr(static_cast<std::size_t>(line.begin), static_cast<std::size_t>(line.end - line.begin))).c_str());
    }
    // The drawing, both ways: '#' both, 'G' GDI only, 'o' ours only.
    const int pictureHeight = std::min(layout.result + 8, 200);
    Dib picture(std::min(rectWidth + 16, 400), pictureHeight);
    SelectObject(picture.dc, hfont);
    SetBkMode(picture.dc, TRANSPARENT);
    SetTextColor(picture.dc, RGB(255, 255, 255));
    RECT drawRect{4, 4, 4 + rectWidth, pictureHeight - 4};
    DrawTextA(picture.dc, bytes.c_str(), -1, &drawRect, format);
    GdiFlush();
    TextLayout drawn;
    Layout(font, text, Rect{4, 4, 4 + rectWidth, pictureHeight - 4}, format, drawn);
    std::vector<std::uint32_t> mine(static_cast<std::size_t>(picture.width) * static_cast<std::size_t>(picture.height), 0);
    ComposeAgainstBlack(font, drawn, 0xFFFFFF, mine.data(), picture.width, picture.height, picture.width);
    for (int y = 0; y < picture.height; ++y) {
        std::string row;
        bool any = false;
        for (int x = 0; x < picture.width; ++x) {
            const bool g = (picture.pixels[y * picture.width + x] & 0xFFFFFF) != 0;
            const bool o = (mine[static_cast<std::size_t>(y * picture.width + x)] & 0xFFFFFF) != 0;
            row += g && o ? '#' : g ? 'G' : o ? 'o' : '.';
            any = any || g || o;
        }
        if (any) std::printf("%3d %s\n", y, row.c_str());
    }
    DeleteObject(hfont);
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    if ((argc == 5 || argc == 6) && std::string(argv[1]) == "--explain") {
        const int font = argc == 6 ? std::atoi(argv[5]) : 0;
        if (font < 0 || font >= static_cast<int>(FontSpecs().size())) return 2;
        return Explain(std::atoi(argv[3]), static_cast<unsigned>(std::strtoul(argv[4], nullptr, 0)), font);
    }
    bool rules = false;
    Options options;
    std::string texts = "F:\\games\\EvilIslands\\Res\\texts.res";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--rules") rules = true;
        else if (arg == "--gdi") options.gdi = true;
        else if (arg == "--report") options.report = true;
        else if (arg == "--texts" && i + 1 < argc) texts = argv[++i];
        else if (arg == "--threads" && i + 1 < argc) options.threads = std::max(1, std::atoi(argv[++i]));
        else if (arg == "--pixels-every" && i + 1 < argc) options.pixelsEvery = std::max(1, std::atoi(argv[++i]));
        else if (arg == "--examples" && i + 1 < argc) options.examples = std::atoi(argv[++i]);
        else if (arg == "--font" && i + 1 < argc) options.onlyFont = std::atoi(argv[++i]);
        else if (arg == "--fonts" && i + 1 < argc) SetFontDirectory(argv[++i]);
        else {
            std::printf("usage: ei_text_check [--rules] [--gdi] [--report] [--texts path] [--fonts dir] [--threads N] [--pixels-every N] [--examples N] [--font K]\n"
                        "       ei_text_check --explain <text> <rect width> <format> [font K]\n");
            return 2;
        }
    }
    if (!rules && !options.gdi && !options.report) rules = options.gdi = options.report = true;
    int failures = 0;
    if (rules) failures += CheckRules();
    if (options.gdi || options.report) failures += RunGdi(texts, options);
    std::printf(failures ? "FAILED\n" : "OK\n");
    return failures ? 1 : 0;
}
