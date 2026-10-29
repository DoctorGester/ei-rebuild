// The titles' rich text (render_rich_text.h): an RTF subset reader and RichEdit 2.0's line layout on render::text's
// fonts. No Windows calls. See docs/notes/ui-native.md, "The titles' rich text".
#include "render_rich_text.h"

#include <algorithm>
#include <map>
#include <memory>

namespace render::text {

namespace {

// ---- Reading -----------------------------------------------------------------------------------------

// Windows-1252's 0x80..0x9F; the rest of its upper half is Latin-1.
constexpr char16_t kCp1252High[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

constexpr int kCyrillicCodePage = 1251;
constexpr int kWesternCodePage = 1252;

// A byte of text in `codePage` (1251, else 1252) as a code point.
char32_t DecodeByte(unsigned char byte, int codePage) {
    if (byte < 0x80) return byte;
    if (codePage == kCyrillicCodePage) {
        const char c = static_cast<char>(byte);
        const std::u32string decoded = DecodeCp1251(&c, 1);
        return decoded.empty() ? byte : decoded[0];
    }
    return byte < 0xA0 ? kCp1252High[byte - 0x80] : byte;
}

// \fcharset to a code page: 204 Cyrillic, 0 Western, the document's \ansicpg for the rest.
int CodePageOfCharset(int charset, int ansi) {
    if (charset == 204) return kCyrillicCodePage;
    if (charset == 0) return kWesternCodePage;
    return ansi;
}

enum class Destination { Text, FontTable, ColourTable, Skip };

struct FontEntry {
    std::string name;
    int charset = -1;
};

// What a group keeps: the formats and where its text goes.
struct GroupState {
    Destination destination = Destination::Text;
    RichStyle style;
    int font = -1;               // \f's number (-1: \deff's)
    RichAlign align = RichAlign::Left;
    int left = 0, first = 0, right = 0;
    std::vector<RichTab> tabs;
    bool rightTab = false;       // \tqr seen, for the next \tx
    int unicodeSkip = 1;         // \uc
};

bool SameStyle(const RichStyle& a, const RichStyle& b) {
    return a.face == b.face && a.halfPoints == b.halfPoints && a.bold == b.bold && a.italic == b.italic && a.colour == b.colour;
}

bool IsLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }

int HexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

class RtfReader {
public:
    explicit RtfReader(std::string_view newRtf) : rtf(newRtf) {}

    RichText Read() {
        std::vector<GroupState> stack;
        GroupState state;
        while (at < rtf.size()) {
            const char c = rtf[at++];
            if (c == '{') {
                stack.push_back(state);
            } else if (c == '}') {
                if (state.destination == Destination::FontTable && fontNumber >= 0) fontNumber = -1;
                if (stack.empty()) break;
                state = stack.back();
                stack.pop_back();
            } else if (c == '\\') {
                Control(state);
            } else if (c == '\r' || c == '\n') {
                continue;
            } else {
                Character(state, c == '\t' ? U'\t' : DecodeByte(static_cast<unsigned char>(c), CodePage(state)));
            }
        }
        if (!paragraph.runs.empty()) EndParagraph(state);
        return std::move(text);
    }

private:
    int CodePage(const GroupState& state) const {
        const int font = state.font >= 0 ? state.font : defaultFont;
        auto found = fonts.find(font);
        return CodePageOfCharset(found != fonts.end() ? found->second.charset : -1, ansiCodePage);
    }

    std::string FaceOf(int font) const {
        auto found = fonts.find(font >= 0 ? font : defaultFont);
        return found != fonts.end() ? found->second.name : std::string();
    }

    void Character(GroupState& state, char32_t ch) {
        if (skipCharacters > 0) {
            --skipCharacters;
            return;
        }
        switch (state.destination) {
        case Destination::Text: Text(state, ch); break;
        case Destination::FontTable:
            if (fontNumber < 0) break;
            if (ch == U';') fontNumber = -1;
            else if (ch < 0x80) fonts[fontNumber].name += static_cast<char>(ch);
            break;
        case Destination::ColourTable:
            if (ch == U';') {
                colours.push_back(colourSet ? colour : kAutomatic);
                colour = 0;
                colourSet = false;
            }
            break;
        case Destination::Skip: break;
        }
    }

    void Text(GroupState& state, char32_t ch) {
        state.style.face = FaceOf(state.font);
        if (paragraph.runs.empty() || !SameStyle(paragraph.runs.back().style, state.style))
            paragraph.runs.push_back(RichRun{state.style, {}});
        paragraph.runs.back().text += ch;
    }

    void EndParagraph(const GroupState& state) {
        paragraph.align = state.align;
        paragraph.left = state.left;
        paragraph.first = state.first;
        paragraph.right = state.right;
        paragraph.tabs = state.tabs;
        paragraph.mark = state.style;
        paragraph.mark.face = FaceOf(state.font);
        text.paragraphs.push_back(std::move(paragraph));
        paragraph = RichParagraph{};
    }

    void Control(GroupState& state) {
        if (at >= rtf.size()) return;
        const char c = rtf[at];
        if (!IsLetter(c)) {
            ++at;
            switch (c) {
            case '\'': {
                const int high = at < rtf.size() ? HexValue(rtf[at]) : -1;
                const int low = at + 1 < rtf.size() ? HexValue(rtf[at + 1]) : -1;
                if (high < 0 || low < 0) return;
                at += 2;
                Character(state, DecodeByte(static_cast<unsigned char>(high * 16 + low), CodePage(state)));
                return;
            }
            case '\\': case '{': case '}': Character(state, static_cast<unsigned char>(c)); return;
            case '~': Character(state, 0x00A0); return;
            case '_': Character(state, 0x2011); return;
            case '*': state.destination = Destination::Skip; return;
            case '\r': case '\n': Word(state, "par", false, 0); return;
            default: return;   // \- (an optional hyphen) and the rest
            }
        }
        const std::size_t start = at;
        while (at < rtf.size() && IsLetter(rtf[at])) ++at;
        const std::string_view word = rtf.substr(start, at - start);
        bool hasValue = false;
        int value = 0;
        bool negative = false;
        if (at < rtf.size() && rtf[at] == '-') {
            negative = true;
            ++at;
        }
        while (at < rtf.size() && rtf[at] >= '0' && rtf[at] <= '9') {
            value = value * 10 + (rtf[at++] - '0');
            hasValue = true;
        }
        if (negative) value = -value;
        if (at < rtf.size() && rtf[at] == ' ') ++at;
        Word(state, word, hasValue, value);
    }

    void Word(GroupState& state, std::string_view word, bool hasValue, int value) {
        const int flag = hasValue ? value : 1;
        if (state.destination == Destination::FontTable) {
            if (word == "f") {
                fontNumber = value;
                fonts[value];
            } else if (word == "fcharset" && fontNumber >= 0) {
                fonts[fontNumber].charset = value;
            }
            return;
        }
        if (state.destination == Destination::ColourTable) {
            if (word == "red") colour = (colour & ~0x0000FFu) | static_cast<std::uint32_t>(value & 0xFF);
            else if (word == "green") colour = (colour & ~0x00FF00u) | (static_cast<std::uint32_t>(value & 0xFF) << 8);
            else if (word == "blue") colour = (colour & ~0xFF0000u) | (static_cast<std::uint32_t>(value & 0xFF) << 16);
            else return;
            colourSet = true;
            return;
        }
        if (state.destination == Destination::Skip) return;
        if (word == "fonttbl") state.destination = Destination::FontTable;
        else if (word == "colortbl") state.destination = Destination::ColourTable;
        else if (word == "stylesheet" || word == "info" || word == "pict" || word == "object" || word == "header" ||
                 word == "footer" || word == "headerl" || word == "headerr" || word == "headerf" || word == "footerl" ||
                 word == "footerr" || word == "footerf" || word == "footnote" || word == "pntext" || word == "fldinst" ||
                 word == "listtable" || word == "listoverridetable" || word == "revtbl" || word == "rsidtbl")
            state.destination = Destination::Skip;
        else if (word == "ansicpg") ansiCodePage = value;
        else if (word == "deff") defaultFont = value;
        else if (word == "deftab") text.defaultTab = value > 0 ? value : text.defaultTab;
        else if (word == "uc") state.unicodeSkip = value;
        else if (word == "u") {
            Character(state, static_cast<char32_t>(value < 0 ? value + 0x10000 : value));
            skipCharacters = state.unicodeSkip;
        } else if (word == "pard") {
            state.align = RichAlign::Left;
            state.left = state.first = state.right = 0;
            state.tabs.clear();
            state.rightTab = false;
        } else if (word == "plain") {
            state.style = RichStyle{};
            state.font = -1;
        } else if (word == "par") EndParagraph(state);
        else if (word == "line") Character(state, U'\n');
        else if (word == "tab") Character(state, U'\t');
        else if (word == "emdash") Character(state, 0x2014);
        else if (word == "endash") Character(state, 0x2013);
        else if (word == "bullet") Character(state, 0x2022);
        else if (word == "lquote") Character(state, 0x2018);
        else if (word == "rquote") Character(state, 0x2019);
        else if (word == "ldblquote") Character(state, 0x201C);
        else if (word == "rdblquote") Character(state, 0x201D);
        else if (word == "b") state.style.bold = flag != 0;
        else if (word == "i") state.style.italic = flag != 0;
        else if (word == "fs") state.style.halfPoints = hasValue && value > 0 ? value : 24;
        else if (word == "f") state.font = value;
        else if (word == "cf") {
            const std::uint32_t tableColour = value >= 0 && static_cast<std::size_t>(value) < colours.size() ? colours[value] : kAutomatic;
            state.style.colour = tableColour == kAutomatic ? 0 : tableColour;
        } else if (word == "ql" || word == "qj") state.align = RichAlign::Left;
        else if (word == "qc") state.align = RichAlign::Centre;
        else if (word == "qr") state.align = RichAlign::Right;
        else if (word == "li") state.left = value;
        else if (word == "fi") state.first = value;
        else if (word == "ri") state.right = value;
        else if (word == "tqr") state.rightTab = true;
        else if (word == "tx") {
            state.tabs.push_back(RichTab{value, state.rightTab});
            state.rightTab = false;
        }
    }

    static constexpr std::uint32_t kAutomatic = 0xFFFFFFFFu;   // a colour table entry without a colour

    std::string_view rtf;
    std::size_t at = 0;
    RichText text;
    RichParagraph paragraph;
    std::map<int, FontEntry> fonts;
    int fontNumber = -1;         // the font table entry being read
    int defaultFont = 0;
    int ansiCodePage = kWesternCodePage;
    std::vector<std::uint32_t> colours;
    std::uint32_t colour = 0;
    bool colourSet = false;
    int skipCharacters = 0;      // characters after \u left out
};

// ---- Fonts -------------------------------------------------------------------------------------------

// A font emboldened as GDI does when the face has no bold: each glyph's coverage spread `strength` pixels to the
// right, the box and the advance that much wider.
class EmboldenedFont final : public Font {
public:
    EmboldenedFont(std::unique_ptr<Font> newFont, int newStrength) : font(std::move(newFont)), strength(newStrength) {}

    const FontMetrics& Metrics() const override { return font->Metrics(); }

    GlyphMetrics Glyph(char32_t ch) const override {
        GlyphMetrics glyph = font->Glyph(ch);
        if (ch < 0x20) return glyph;
        glyph.advance += strength;
        if (glyph.width > 0) glyph.width += strength;
        return glyph;
    }

    int Kerning(char32_t left, char32_t right) const override { return font->Kerning(left, right); }

    void Rasterise(char32_t ch, std::uint8_t* out, int pitch) const override {
        const GlyphMetrics glyph = font->Glyph(ch);
        if (glyph.width <= 0 || glyph.height <= 0) return;
        std::vector<std::uint8_t> plain(static_cast<std::size_t>(glyph.width) * static_cast<std::size_t>(glyph.height), 0);
        font->Rasterise(ch, plain.data(), glyph.width);
        const int width = glyph.width + strength;
        for (int y = 0; y < glyph.height; ++y) {
            const std::uint8_t* row = &plain[static_cast<std::size_t>(y) * static_cast<std::size_t>(glyph.width)];
            for (int x = 0; x < width; ++x) {
                std::uint8_t value = 0;
                for (int s = 0; s <= strength; ++s) {
                    const int from = x - s;
                    if (from >= 0 && from < glyph.width) value = std::max(value, row[from]);
                }
                out[y * pitch + x] = value;
            }
        }
    }

private:
    std::unique_ptr<Font> font;
    int strength;
};

// ---- Layout ------------------------------------------------------------------------------------------

// MulDiv(value, multiplier, divisor) for a positive divisor: rounded, halves away from zero.
int MulDivRounded(int value, int multiplier, int divisor) {
    const long long product = static_cast<long long>(value) * multiplier;
    const long long half = divisor / 2;
    return static_cast<int>(product >= 0 ? (product + half) / divisor : -((-product + half) / divisor));
}

constexpr int kTwipsPerInch = 1440;
constexpr int kHalfPointsPerInch = 144;

// One character placed on a line.
struct Item {
    TextFont* font;
    std::uint32_t colour;
    char32_t ch;
    int advance;                 // without tabs (theirs depends on the pen)
};

bool IsBlank(char32_t ch) { return ch == U' ' || ch == U'\t'; }

class ParagraphLayout {
public:
    ParagraphLayout(const RichText& newText, const RichParagraph& newParagraph, int newWidth, int newDpi, bool newSmoothing)
        : text(newText), paragraph(newParagraph), width(newWidth), dpi(newDpi), smoothing(newSmoothing) {}

    void Append(RichLayout& out) {
        for (const RichRun& run : paragraph.runs) {
            TextFont* font = FontOf(run.style);
            if (!font) continue;
            for (char32_t ch : run.text) items.push_back(Item{font, run.style.colour, ch, ch == U'\t' || ch == U'\n' ? 0 : font->Advance(ch)});
        }
        TextFont* mark = FontOf(paragraph.mark);
        std::size_t at = 0;
        bool first = true;
        do {
            at = Line(out, at, first, mark);
            first = false;
        } while (at < items.size());
    }

private:
    TextFont* FontOf(const RichStyle& style) const {
        const int pixels = MulDivRounded(style.halfPoints, dpi, kHalfPointsPerInch);
        return style.bold ? BoldFont(style.face, pixels, smoothing) : GetTextFont(style.face, pixels, smoothing);
    }

    int Pixels(int twips) const { return TwipsToPixels(twips, dpi); }

    // The pen (from the page's left) after a tab at `pen`, `next` the item after the tab.
    int TabStop(int pen, std::size_t next) const {
        for (const RichTab& tab : paragraph.tabs) {
            const int stop = Pixels(tab.position);
            if (stop <= pen) continue;
            if (!tab.right) return stop;
            int segment = 0;
            for (std::size_t i = next; i < items.size() && items[i].ch != U'\t' && items[i].ch != U'\n'; ++i) segment += items[i].advance;
            return std::max(pen, stop - segment);
        }
        const int every = std::max(1, text.defaultTab);
        for (int k = 1;; ++k) {
            const int stop = Pixels(k * every);
            if (stop > pen) return stop;
        }
    }

    // Lays out the line starting at item `start`; returns where the next one starts.
    std::size_t Line(RichLayout& out, std::size_t start, bool first, TextFont* mark) {
        const int left = Pixels(paragraph.left + (first ? paragraph.first : 0));
        const int room = width - Pixels(paragraph.right) - left;
        std::vector<int> pens;
        std::vector<int> advances;
        std::size_t end = start, breakAfterBlank = 0;
        int pen = 0;
        for (std::size_t i = start; i < items.size(); ++i) {
            const Item& item = items[i];
            if (item.ch == U'\n') {
                pens.push_back(pen);
                advances.push_back(0);
                end = i + 1;
                break;
            }
            const int advance = item.ch == U'\t' ? TabStop(left + pen, i + 1) - (left + pen) : item.advance;
            if (!IsBlank(item.ch) && pen + advance > room && i > start) {
                end = breakAfterBlank > start ? breakAfterBlank : i;
                break;
            }
            pens.push_back(pen);
            advances.push_back(advance);
            pen += advance;
            end = i + 1;
            if (IsBlank(item.ch)) breakAfterBlank = i + 1;
        }
        pens.resize(end - start);
        advances.resize(end - start);
        // The width aligned: up to the last character that is not blank.
        int alignedWidth = 0;
        for (std::size_t k = 0; k < pens.size(); ++k) {
            const char32_t ch = items[start + k].ch;
            if (!IsBlank(ch) && ch != U'\n') alignedWidth = pens[k] + advances[k];
        }
        int x = left;
        if (paragraph.align == RichAlign::Centre) x += std::max(0, (room - alignedWidth) / 2);
        else if (paragraph.align == RichAlign::Right) x += std::max(0, room - alignedWidth);
        RichLine line;
        line.top = out.height;
        int ascent = 0, descent = 0;
        auto measure = [&](const TextFont* font) {
            ascent = std::max(ascent, font->Metrics().cellAscent);
            descent = std::max(descent, font->Metrics().cellDescent);
        };
        for (std::size_t k = 0; k < pens.size(); ++k) {
            const Item& item = items[start + k];
            measure(item.font);
            if (item.ch > U' ') line.glyphs.push_back(RichGlyph{item.font, item.colour, item.ch, x + pens[k]});
        }
        if (end >= items.size() && mark) measure(mark);
        line.ascent = ascent;
        line.height = ascent + descent;
        out.height += line.height;
        out.lines.push_back(std::move(line));
        return end;
    }

    const RichText& text;
    const RichParagraph& paragraph;
    int width, dpi;
    bool smoothing;
    std::vector<Item> items;
};

} // namespace

RichText ReadRtf(std::string_view rtf) { return RtfReader(rtf).Read(); }

int TwipsToPixels(int twips, int dpi) { return MulDivRounded(twips, dpi, kTwipsPerInch); }

TextFont* BoldFont(std::string_view face, int pixels, bool smoothing) {
    static std::map<std::string, std::unique_ptr<TextFont>> fonts;
    std::string key(face);
    key += '|' + std::to_string(pixels) + (smoothing ? "|1" : "|0");
    auto found = fonts.find(key);
    if (found != fonts.end()) return found->second.get();
    std::unique_ptr<TextFont>& slot = fonts[key];
    if (auto font = LoadFont(FontForFace(face), pixels))
        slot = std::make_unique<TextFont>(std::make_unique<EmboldenedFont>(std::move(font), std::max(1, (pixels + 10) / 20)), smoothing);
    return slot.get();
}

RichLayout LayoutRichText(const RichText& text, int width, int dpi, bool smoothing) {
    RichLayout out;
    for (const RichParagraph& paragraph : text.paragraphs) ParagraphLayout(text, paragraph, width, dpi, smoothing).Append(out);
    return out;
}

std::vector<RichDraw> RichDraws(const RichLayout& layout, int x, int y, const Rect& clip) {
    std::vector<RichDraw> draws;
    for (const RichLine& line : layout.lines) {
        const int top = y + line.top;
        if (top + line.height <= clip.top || top >= clip.bottom) continue;
        const int baseline = top + line.ascent;
        for (const RichGlyph& glyph : line.glyphs) {
            auto draw = std::find_if(draws.begin(), draws.end(), [&](const RichDraw& d) { return d.font == glyph.font && d.colour == glyph.colour; });
            if (draw == draws.end()) {
                draws.push_back(RichDraw{glyph.font, glyph.colour, TextLayout{}});
                draw = draws.end() - 1;
                draw->layout.rect = clip;
                draw->layout.clip = clip;
            }
            const AtlasGlyph& atlas = glyph.font->Atlas().Get(glyph.ch);
            if (atlas.metrics.width <= 0 || atlas.metrics.height <= 0) continue;
            draw->layout.quads.push_back(GlyphQuad{atlas.atlasX, atlas.atlasY, atlas.metrics.width, atlas.metrics.height,
                                                   x + glyph.x + atlas.metrics.left, baseline + atlas.metrics.top});
        }
    }
    return draws;
}

} // namespace render::text
