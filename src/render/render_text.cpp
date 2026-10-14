// Interface text without GDI: fonts, glyph atlas, DrawText-compatible layout. See render_text.h and
// docs/notes/ui-native.md, section 2.
#include "render_text.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>

#pragma warning(push, 0)
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb/stb_truetype.h"
#pragma warning(pop)

namespace render::text {

// ---- Font files ---------------------------------------------------------------------------------------

namespace {

#ifdef EI_FONT_DIR
std::string g_fontDirectory = EI_FONT_DIR;
#else
std::string g_fontDirectory = "fonts";
#endif

int RoundToInt(float value) { return static_cast<int>(std::floor(value + 0.5f)); }

int ReadU16(const unsigned char* p) { return (p[0] << 8) | p[1]; }
int ReadS16(const unsigned char* p) { return static_cast<std::int16_t>(ReadU16(p)); }

// A loaded font file with the table values GDI's metrics come from.
struct FontFile {
    std::vector<unsigned char> data;
    stbtt_fontinfo info{};
    int unitsPerEm = 2048;
    int winAscent = 0, winDescent = 0;     // OS/2: GDI's tmAscent / tmDescent
    int averageWidth = 0;                  // OS/2 xAvgCharWidth: tmAveCharWidth
    int hheaAscent = 0, hheaDescent = 0, hheaLineGap = 0;

    const unsigned char* Table(const char* tag) const {
        if (data.size() < 12) return nullptr;
        const int tables = ReadU16(&data[4]);
        for (int i = 0; i < tables; ++i) {
            const std::size_t record = 12 + 16 * static_cast<std::size_t>(i);
            if (record + 16 > data.size()) return nullptr;
            if (std::memcmp(&data[record], tag, 4) != 0) continue;
            const std::size_t offset = (std::size_t(data[record + 8]) << 24) | (std::size_t(data[record + 9]) << 16) |
                                       (std::size_t(data[record + 10]) << 8) | data[record + 11];
            return offset < data.size() ? &data[offset] : nullptr;
        }
        return nullptr;
    }

    bool Parse() {
        if (!stbtt_InitFont(&info, data.data(), stbtt_GetFontOffsetForIndex(data.data(), 0))) return false;
        if (const unsigned char* head = Table("head")) unitsPerEm = ReadU16(head + 18);
        stbtt_GetFontVMetrics(&info, &hheaAscent, &hheaDescent, &hheaLineGap);
        winAscent = hheaAscent;
        winDescent = -hheaDescent;
        if (const unsigned char* os2 = Table("OS/2")) {
            averageWidth = ReadS16(os2 + 2);
            winAscent = ReadU16(os2 + 74);
            winDescent = ReadU16(os2 + 76);
        }
        // Times New Roman's xAvgCharWidth (821) is the old OS/2 weighted average of 'a'..'z' and
        // the space; Liberation's is the plain average of all glyphs (much wider). Recompute the old
        // way, so tab stops come out as GDI's (equal for a fixed-pitch font).
        static const int kWeights[27] = {64, 14, 27, 35, 100, 20, 14, 42, 63, 3, 6, 35, 20, 56,
                                         56, 17, 4, 49, 56, 71, 31, 10, 18, 3, 18, 2, 166};
        int weighted = 0;
        bool complete = true;
        for (int i = 0; i < 27 && complete; ++i) {
            const int codepoint = i < 26 ? 'a' + i : ' ';
            complete = stbtt_FindGlyphIndex(&info, codepoint) != 0;
            int advance = 0, bearing = 0;
            stbtt_GetCodepointHMetrics(&info, codepoint, &advance, &bearing);
            weighted += kWeights[i] * advance;
        }
        if (complete) averageWidth = (weighted + 500) / 1000;
        return unitsPerEm > 0;
    }
};

std::shared_ptr<FontFile> LoadFontFile(const std::string& path) {
    static std::map<std::string, std::weak_ptr<FontFile>> files;
    if (auto loaded = files[path].lock()) return loaded;
    std::FILE* file = nullptr;
    if (fopen_s(&file, path.c_str(), "rb") != 0 || !file) return nullptr;
    auto font = std::make_shared<FontFile>();
    std::fseek(file, 0, SEEK_END);
    const long size = std::ftell(file);
    std::fseek(file, 0, SEEK_SET);
    if (size > 0) {
        font->data.resize(static_cast<std::size_t>(size));
        if (std::fread(font->data.data(), 1, font->data.size(), file) != font->data.size()) font->data.clear();
    }
    std::fclose(file);
    if (font->data.empty() || !font->Parse()) return nullptr;
    files[path] = font;
    return font;
}

// stb_truetype at one em height. Control characters (below U+0020) have no advance and no ink, as
// they have in GDI's DrawText with the TrueType fonts the game uses.
class StbFont final : public Font {
public:
    StbFont(std::shared_ptr<FontFile> file, int pixels) : file_(std::move(file)) {
        scale_ = static_cast<float>(pixels) / static_cast<float>(file_->unitsPerEm);
        metrics_.ascent = RoundToInt(static_cast<float>(file_->winAscent) * scale_);
        metrics_.descent = RoundToInt(static_cast<float>(file_->winDescent) * scale_);
        metrics_.height = metrics_.ascent + metrics_.descent;
        const int hheaHeight = file_->hheaAscent - file_->hheaDescent + file_->hheaLineGap;
        const int winHeight = file_->winAscent + file_->winDescent;
        metrics_.lineGap = std::max(0, RoundToInt(static_cast<float>(hheaHeight - winHeight) * scale_));
        metrics_.averageWidth = RoundToInt(static_cast<float>(file_->averageWidth) * scale_);
    }

    const FontMetrics& Metrics() const override { return metrics_; }

    GlyphMetrics Glyph(char32_t ch) const override {
        GlyphMetrics glyph;
        if (ch < 0x20) return glyph;
        const int index = stbtt_FindGlyphIndex(&file_->info, static_cast<int>(ch));
        int advance = 0, bearing = 0;
        stbtt_GetGlyphHMetrics(&file_->info, index, &advance, &bearing);
        glyph.advance = RoundToInt(static_cast<float>(advance) * scale_);
        int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
        stbtt_GetGlyphBitmapBox(&file_->info, index, scale_, scale_, &x0, &y0, &x1, &y1);
        if (x1 > x0 && y1 > y0) {
            glyph.left = x0;
            glyph.top = y0;
            glyph.width = x1 - x0;
            glyph.height = y1 - y0;
        }
        return glyph;
    }

    int Kerning(char32_t left, char32_t right) const override {
        const int kern = stbtt_GetCodepointKernAdvance(&file_->info, static_cast<int>(left), static_cast<int>(right));
        return RoundToInt(static_cast<float>(kern) * scale_);
    }

    void Rasterise(char32_t ch, std::uint8_t* out, int pitch) const override {
        const GlyphMetrics glyph = Glyph(ch);
        if (glyph.width <= 0) return;
        const int index = stbtt_FindGlyphIndex(&file_->info, static_cast<int>(ch));
        stbtt_MakeGlyphBitmap(&file_->info, out, glyph.width, glyph.height, pitch, scale_, scale_, index);
    }

private:
    std::shared_ptr<FontFile> file_;
    float scale_ = 1.0f;
    FontMetrics metrics_;
};

} // namespace

std::unique_ptr<Font> LoadFont(const std::string& path, int pixels) {
    if (pixels <= 0) return nullptr;
    auto file = LoadFontFile(path);
    if (!file) return nullptr;
    return std::make_unique<StbFont>(std::move(file), pixels);
}

void SetFontDirectory(const std::string& directory) { g_fontDirectory = directory; }
std::string FontDirectory() { return g_fontDirectory; }

const char* FontFileForFace(std::string_view face) {
    auto same = [face](std::string_view name) {
        return face.size() == name.size() &&
               std::equal(face.begin(), face.end(), name.begin(), [](char a, char b) {
                   return (a | 0x20) == (b | 0x20);
               });
    };
    if (same("Courier New")) return "LiberationMono-Regular.ttf";
    return "LiberationSerif-Regular.ttf";
}

int PointSizeToPixels(int tenths, int dpi) { return dpi * tenths / 720; }

// ---- Glyph atlas --------------------------------------------------------------------------------------

namespace {
constexpr int kAtlasPadding = 1;   // empty pixels between glyphs, so bilinear sampling would not bleed
int AtlasWidthFor(const Font& font) {
    const int wanted = font.Metrics().height * 16;
    int width = 256;
    while (width < wanted && width < 4096) width *= 2;
    return width;
}
} // namespace

GlyphAtlas::GlyphAtlas(const Font& font, bool smoothing)
    : font_(font), smoothing_(smoothing), width_(AtlasWidthFor(font)) {}

const AtlasGlyph& GlyphAtlas::Get(char32_t ch) {
    auto found = glyphs_.find(ch);
    if (found != glyphs_.end()) return found->second;
    AtlasGlyph& glyph = glyphs_[ch];
    glyph.metrics = font_.Glyph(ch);
    Place(glyph, ch);
    return glyph;
}

void GlyphAtlas::Place(AtlasGlyph& glyph, char32_t ch) {
    const int w = glyph.metrics.width, h = glyph.metrics.height;
    if (w <= 0 || h <= 0) return;
    if (w + kAtlasPadding > width_) {
        // Wider than the atlas (not at interface sizes): keep the metrics, draw nothing.
        glyph.metrics.width = glyph.metrics.height = 0;
        return;
    }
    if (shelfX_ + w + kAtlasPadding > width_) {
        shelfY_ += shelfHeight_;
        shelfX_ = 0;
        shelfHeight_ = 0;
    }
    if (shelfY_ + h + kAtlasPadding > height_) {
        int newHeight = std::max(height_, 64);
        while (shelfY_ + h + kAtlasPadding > newHeight) newHeight *= 2;
        pixels_.resize(static_cast<std::size_t>(width_) * static_cast<std::size_t>(newHeight), 0);
        height_ = newHeight;
    }
    glyph.atlasX = shelfX_;
    glyph.atlasY = shelfY_;
    std::uint8_t* out = &pixels_[static_cast<std::size_t>(shelfY_) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(shelfX_)];
    font_.Rasterise(ch, out, width_);
    if (!smoothing_) {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                std::uint8_t& value = out[y * width_ + x];
                value = value >= 128 ? 255 : 0;
            }
    }
    shelfX_ += w + kAtlasPadding;
    shelfHeight_ = std::max(shelfHeight_, h + kAtlasPadding);
    ++generation_;
}

// ---- Fonts with atlases -------------------------------------------------------------------------------

namespace {
constexpr char32_t kLowCharacters = 0x500;   // Latin, Greek and Cyrillic: advances in a flat table
}

TextFont::TextFont(std::unique_ptr<Font> font, bool smoothing)
    : font_(std::move(font)), atlas_(*font_, smoothing), lowAdvances_(kLowCharacters, -1) {}

int TextFont::Kerning(char32_t left, char32_t right) {
    const std::uint64_t key = (std::uint64_t(left) << 32) | right;
    auto found = kerning_.find(key);
    if (found != kerning_.end()) return found->second;
    const int kern = font_->Kerning(left, right);
    kerning_.emplace(key, kern);
    return kern;
}

int TextFont::Advance(char32_t ch) {
    if (ch < kLowCharacters) {
        int& advance = lowAdvances_[ch];
        if (advance < 0) advance = font_->Glyph(ch).advance;
        return advance;
    }
    auto found = advances_.find(ch);
    if (found != advances_.end()) return found->second;
    const int advance = font_->Glyph(ch).advance;
    advances_.emplace(ch, advance);
    return advance;
}

TextFont* GetTextFont(std::string_view face, int pixels, bool smoothing) {
    static std::map<std::string, std::unique_ptr<TextFont>> fonts;
    std::string key(face);
    key += '|' + std::to_string(pixels) + (smoothing ? "|1" : "|0");
    auto found = fonts.find(key);
    if (found != fonts.end()) return found->second.get();
    std::unique_ptr<TextFont>& slot = fonts[key];
    if (auto font = LoadFont(FontDirectory() + "/" + FontFileForFace(face), pixels))
        slot = std::make_unique<TextFont>(std::move(font), smoothing);
    return slot.get();
}

// ---- Code page ----------------------------------------------------------------------------------------

std::u32string DecodeCp1251(const char* text, int count) {
    // Windows-1251 0x80..0xBF; 0xC0..0xFF are U+0410..U+044F.
    static constexpr char16_t kHigh[64] = {
        0x0402, 0x0403, 0x201A, 0x0453, 0x201E, 0x2026, 0x2020, 0x2021, 0x20AC, 0x2030, 0x0409, 0x2039, 0x040A, 0x040C, 0x040B, 0x040F,
        0x0452, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x0098, 0x2122, 0x0459, 0x203A, 0x045A, 0x045C, 0x045B, 0x045F,
        0x00A0, 0x040E, 0x045E, 0x0408, 0x00A4, 0x0490, 0x00A6, 0x00A7, 0x0401, 0x00A9, 0x0404, 0x00AB, 0x00AC, 0x00AD, 0x00AE, 0x0407,
        0x00B0, 0x00B1, 0x0406, 0x0456, 0x0491, 0x00B5, 0x00B6, 0x00B7, 0x0451, 0x2116, 0x0454, 0x00BB, 0x0458, 0x0405, 0x0455, 0x0457,
    };
    if (!text) return {};
    const std::size_t length = count < 0 ? std::strlen(text) : static_cast<std::size_t>(count);
    std::u32string out;
    out.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        const unsigned char byte = static_cast<unsigned char>(text[i]);
        if (byte < 0x80) out.push_back(byte);
        else if (byte < 0xC0) out.push_back(kHigh[byte - 0x80]);
        else out.push_back(char32_t(0x0410 + (byte - 0xC0)));
    }
    return out;
}

// ---- Layout -------------------------------------------------------------------------------------------
//
// The rules (measured against user32's DrawTextA on Windows 10 with fixed-pitch fonts; the synthetic
// cases in tools/text_check check each of them):
// - Lines end at CR or LF; CR LF and LF CR count as one break. A break at the very end still makes an
//   (empty) last line; an empty text draws nothing and returns 1.
// - With DT_WORDBREAK the text is cut into words: each space or tab is a word of its own, any other
//   run of characters up to a space, tab, CR or LF is one word. Words are added while the width from
//   the line start to the word's end is at most the rectangle's width. The first word that does not
//   fit ends the line before it; when it is the line's first word, the line is that word alone
//   (clipped, never broken inside).
//   - Left and centred text then skip one space or tab at the start of the next line; right-aligned
//     text does not (so a right-aligned wrap can leave a line holding only spaces).
//   - Centred and right-aligned lines that end at such a wrap lose one trailing space or tab, for
//     drawing and for DT_CALCRECT alike. Left-aligned lines keep it (DT_CALCRECT counts it).
// - Widths: advances summed plus the font's pair kerning (DrawText on Windows 10 goes through
//   Uniscribe, which kerns; GetTextExtentPoint32, and so TextExtent, does not). There is no kerning
//   pair across a tab, nor between a Latin and a Cyrillic run (a space after "в" is Cyrillic, so
//   "в W" does not kern the space with the W). '&' (unless DT_NOPREFIX) is dropped and makes the next character the prefix
//   character; "&&" draws one '&' and a trailing '&' draws nothing.
// - Tabs: with DT_EXPANDTABS a tab moves to the next multiple of 8 average widths from the line's
//   start; without it a tab has no width and no ink.
// - x: left, right - width, or left + (rect width - width) / 2 rounded down. Lines are the font's
//   height apart from the rectangle's top; DT_VCENTER without DT_SINGLELINE changes nothing.
// - Drawing stops after the first line whose bottom is below the rectangle (unless DT_NOCLIP); the
//   return value is the height of the lines drawn (one line more when it stopped early and the
//   text ends with a line break). DT_CALCRECT lays out every line, returns their height and sets right = left +
//   widest line, bottom = top + height; with DT_WORDBREAK and a widest line wider than the
//   rectangle it lays the text out again at that width.
// - The last prefix character of each tab segment is underlined: one pixel at ascent + 1 below the
//   line's top, from the character's position to the next character's (its advance at the end).
//   A prefixed expanded tab is underlined from the pen to its stop one pixel higher; a prefixed
//   tab that is not expanded takes the segment's underline away.
// - Not modelled (synthetic cases only; the game's texts have no '&'): a wrapped first word that
//   holds a prefix and ends in '&' sometimes keeps the following space on its line in DrawText.

namespace {

bool IsLineBreak(char32_t ch) { return ch == U'\r' || ch == U'\n'; }
bool IsWhite(char32_t ch) { return ch == U' ' || ch == U'\t'; }

// Uniscribe splits text into runs by script and kerns only within a run; characters of no script
// (spaces, digits, punctuation) join the run before them. 1 Latin, 2 Cyrillic, 0 none.
int Script(char32_t ch) {
    if ((ch >= U'A' && ch <= U'Z') || (ch >= U'a' && ch <= U'z')) return 1;
    if (ch >= 0xC0 && ch <= 0x24F && ch != 0xD7 && ch != 0xF7) return 1;
    if (ch >= 0x400 && ch <= 0x52F) return 2;
    return 0;
}

class LineMaker {
public:
    LineMaker(TextFont& font, std::u32string_view text, unsigned format)
        : font_(font), text_(text), format_(format), prefixes_(!(format & kNoPrefix)),
          expandTabs_((format & kExpandTabs) != 0), tabLength_(8 * font.Metrics().averageWidth) {}

    // Calls visit(index, character, x) for each character drawn of [begin, end) from x = origin;
    // returns the pen position after the last one. `prefix(index, x, width)` gets each prefix
    // character (its underline), and index -1 where a tab starts a new segment.
    template <class Visit, class Prefix>
    int Walk(int begin, int end, int origin, Visit&& visit, Prefix&& prefix) {
        int x = origin;
        char32_t previous = 0;   // kerning pairs stay within a tab segment
        int run = 0;             // the script of the current run (see Script)
        // A prefix character's underline reaches the next character's (kerned) position, so it is
        // reported once that is known.
        int pending = -1, pendingX = 0, pendingAdvance = 0;
        auto flush = [&](int width) {
            if (pending >= 0) prefix(pending, pendingX, width);
            pending = -1;
        };
        for (int i = begin; i < end; ++i) {
            char32_t ch = text_[i];
            bool prefixed = false;
            if (ch == U'&' && prefixes_) {
                if (i + 1 >= end) continue;   // a trailing '&' draws nothing
                ++i;
                ch = text_[i];
                prefixed = ch != U'&';   // "&&" is a literal '&'
            }
            if (ch == U'\t') {
                flush(pendingAdvance);
                if (expandTabs_) {
                    const int stop = tabLength_ > 0 ? origin + ((x - origin) / tabLength_ + 1) * tabLength_ : x;
                    if (prefixed) prefix(i, x, stop - x);   // a prefixed tab is underlined to its stop
                    prefix(-1, 0, 0);
                    x = stop;
                } else if (prefixed) {
                    // Not expanded: no width and no ink; as the segment's last prefix it leaves no underline.
                    prefix(i, x, 0);
                }
                previous = 0;   // no kerning pair across a tab
                continue;
            }
            const int script = Script(ch);
            if (previous && (script == 0 || run == 0 || script == run)) x += font_.Kerning(previous, ch);
            if (script) run = script;
            flush(x - pendingX);
            if (prefixed) {
                pending = i;
                pendingX = x;
                pendingAdvance = font_.Advance(ch);
            }
            visit(i, ch, x);
            x += font_.Advance(ch);
            previous = ch;
        }
        flush(pendingAdvance);
        return x;
    }

    int Width(int begin, int end) {
        return Walk(begin, end, 0, [](int, char32_t, int) {}, [](int, int, int) {});
    }

    // The end of the word starting at `at` (see the rules above).
    int WordEnd(int at) const {
        const int size = static_cast<int>(text_.size());
        if (IsWhite(text_[at])) return at + 1;
        int end = at;
        while (end < size && !IsWhite(text_[end]) && !IsLineBreak(text_[end])) ++end;
        return end;
    }

    struct Line {
        int begin, end, next;
        bool lastBreak;   // ended at a CR / LF that was the text's last character(s)
    };

    // The line starting at `begin`, and where the next one starts.
    Line Next(int begin, int rectWidth) {
        const int size = static_cast<int>(text_.size());
        Line line{begin, begin, begin, false};
        int at = begin;
        bool wrapped = false;
        if (format_ & kWordBreak) {
            while (at < size && !IsLineBreak(text_[at])) {
                const int wordEnd = WordEnd(at);
                if (Width(begin, wordEnd) > rectWidth) {
                    // A first word too wide for the rectangle is a line of its own, even when a line
                    // break follows it (that break then makes an empty line).
                    line.end = line.next = at != begin ? at : wordEnd;
                    wrapped = true;
                    break;
                }
                at = wordEnd;
            }
        } else {
            while (at < size && !IsLineBreak(text_[at])) ++at;
        }
        if (wrapped) {
            if (!(format_ & kRight) && line.next < size && IsWhite(text_[line.next])) ++line.next;
            if ((format_ & (kRight | kCenter)) && line.end > line.begin && IsWhite(text_[line.end - 1])) --line.end;
            return line;
        }
        line.end = line.next = at;
        if (at < size) {
            const char32_t first = text_[at];
            line.next = at + 1;
            if (line.next < size && IsLineBreak(text_[line.next]) && text_[line.next] != first) ++line.next;
            line.lastBreak = line.next >= size;
        }
        return line;
    }

private:
    TextFont& font_;
    std::u32string_view text_;
    unsigned format_;
    bool prefixes_, expandTabs_;
    int tabLength_;
};

} // namespace

bool Layout(TextFont& font, std::u32string_view text, const Rect& rect, unsigned format, TextLayout& out) {
    if (format & ~kSupportedFormats) return false;
    out = TextLayout{};
    out.rect = rect;
    out.clip = rect;
    out.clipped = !(format & kNoClip);
    const bool calc = (format & kCalcRect) != 0;
    if (text.empty()) {
        if (calc) {
            out.rect.right = rect.left;
            out.rect.bottom = rect.top;
        }
        out.result = 1;
        return true;
    }

    const FontMetrics& metrics = font.Metrics();
    const int rectWidth = rect.right - rect.left;
    LineMaker maker(font, text, format);
    GlyphAtlas& atlas = font.Atlas();
    const int size = static_cast<int>(text.size());
    int y = rect.top;
    int widest = 0;
    int at = 0;
    for (;;) {
        const LineMaker::Line line = maker.Next(at, rectWidth);
        const int width = maker.Width(line.begin, line.end);
        widest = std::max(widest, width);
        int x = rect.left;
        if (format & kRight) x = rect.right - width;
        else if (format & kCenter) x = rect.left + ((rectWidth - width) >> 1);   // rounds down, also when the line is wider
        out.lines.push_back({line.begin, line.end, x, y, width});
        if (!calc) {
            bool underlined = false, underlinedTab = false;
            int underlineX = 0, underlineWidth = 0;
            auto flush = [&] {
                if (!underlined || underlineWidth <= 0) return;
                // An expanded tab's underline sits one pixel higher than a character's.
                const int top = y + metrics.ascent + (underlinedTab ? 0 : 1);
                out.underlines.push_back({underlineX, top, underlineX + underlineWidth, top + 1});
                underlined = false;
            };
            maker.Walk(
                line.begin, line.end, x,
                [&](int, char32_t ch, int pen) {
                    const AtlasGlyph& glyph = atlas.Get(ch);
                    if (glyph.metrics.width <= 0) return;
                    out.quads.push_back({glyph.atlasX, glyph.atlasY, glyph.metrics.width, glyph.metrics.height,
                                         pen + glyph.metrics.left, y + metrics.ascent + glyph.metrics.top});
                },
                [&](int index, int pen, int advance) {
                    if (index < 0) {
                        flush();
                        return;
                    }
                    underlined = true;
                    underlinedTab = text[index] == U'\t';
                    underlineX = pen;
                    underlineWidth = advance;
                });
            flush();
        }
        y += metrics.height;
        if (!line.lastBreak && line.next >= size) break;
        if (!calc && out.clipped && y > rect.bottom) {
            // Stopped below the rectangle: a line break ending the text still counts its empty line.
            if (IsLineBreak(text.back())) y += metrics.height;
            break;
        }
        if (line.lastBreak) {
            // The text ends with a line break: one more, empty line.
            out.lines.push_back({size, size, rect.left, y, 0});
            y += metrics.height;
            break;
        }
        at = line.next;
    }
    out.result = y - rect.top;
    if (calc) {
        out.rect.right = rect.left + widest;
        out.rect.bottom = y;
        // A word wider than the rectangle widens it, and DT_CALCRECT wraps the text again at that width.
        if ((format & kWordBreak) && widest > rectWidth) {
            Rect wider = rect;
            wider.right = rect.left + widest;
            return Layout(font, text, wider, format, out);
        }
    }
    return true;
}

Extent TextExtent(TextFont& font, std::u32string_view text) {
    Extent extent;
    for (char32_t ch : text) extent.cx += font.Advance(ch);
    extent.cy = font.Metrics().height;
    return extent;
}

// ---- CPU composition ----------------------------------------------------------------------------------

void ComposeAgainstBlack(TextFont& font, const TextLayout& layout, std::uint32_t colour, std::uint32_t* pixels,
                         int width, int height, int pitch) {
    int x0 = 0, y0 = 0, x1 = width, y1 = height;
    if (layout.clipped) {
        x0 = std::max(x0, layout.clip.left);
        y0 = std::max(y0, layout.clip.top);
        x1 = std::min(x1, layout.clip.right);
        y1 = std::min(y1, layout.clip.bottom);
    }
    if (x1 <= x0 || y1 <= y0) return;
    const int w = x1 - x0;
    std::vector<std::uint8_t> coverage(static_cast<std::size_t>(w) * static_cast<std::size_t>(y1 - y0), 0);
    auto at = [&](int x, int y) -> std::uint8_t& {
        return coverage[static_cast<std::size_t>(y - y0) * static_cast<std::size_t>(w) + static_cast<std::size_t>(x - x0)];
    };
    const GlyphAtlas& atlas = font.Atlas();
    for (const GlyphQuad& quad : layout.quads) {
        const int qx0 = std::max(x0, quad.x), qx1 = std::min(x1, quad.x + quad.width);
        const int qy0 = std::max(y0, quad.y), qy1 = std::min(y1, quad.y + quad.height);
        for (int y = qy0; y < qy1; ++y) {
            const std::uint8_t* source = atlas.Pixels() + static_cast<std::size_t>(quad.atlasY + y - quad.y) * static_cast<std::size_t>(atlas.Width()) +
                                         static_cast<std::size_t>(quad.atlasX - quad.x);
            for (int x = qx0; x < qx1; ++x) at(x, y) = std::max(at(x, y), source[x]);
        }
    }
    for (const Rect& line : layout.underlines) {
        for (int y = std::max(y0, line.top); y < std::min(y1, line.bottom); ++y)
            for (int x = std::max(x0, line.left); x < std::min(x1, line.right); ++x) at(x, y) = 255;
    }
    const unsigned red = colour & 0xFF, green = (colour >> 8) & 0xFF, blue = (colour >> 16) & 0xFF;
    for (int y = y0; y < y1; ++y) {
        for (int x = x0; x < x1; ++x) {
            const unsigned cover = at(x, y);
            if (!cover) continue;
            auto shade = [cover](unsigned channel) { return (channel * cover + 127) / 255; };
            pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(pitch) + static_cast<std::size_t>(x)] =
                0xFF000000u | (shade(red) << 16) | (shade(green) << 8) | shade(blue);
        }
    }
}

} // namespace render::text
