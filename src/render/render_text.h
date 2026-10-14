#pragma once

// Interface text without GDI (docs/notes/ui-native.md, section 2; ticket N2): TrueType fonts loaded
// from files (stb_truetype), a glyph atlas per font and pixel size, and a layout that follows
// user32's DrawTextA for the formats the game uses. The same layout gives the text extent
// (005F8340's GetTextExtentPoint32A), so wrapping and drawing use one set of metrics.
//
// Nothing here is pixel-exact with GDI: the fonts are metric-compatible stand-ins (Liberation Serif
// for Times New Roman, Liberation Mono for Courier New) rasterised without hinting. What follows
// DrawText exactly are the rules: where lines break, how they are aligned, tabs, the '&' prefix,
// clipping and the return value / DT_CALCRECT rectangle, given the font's advances.
//
// Output: glyph quads (atlas rectangle -> destination rectangle) plus underline rectangles, for the
// GPU canvas (N1) to draw, and a CPU composition "against black" for tests.

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace render::text {

// ---- Fonts --------------------------------------------------------------------------------------------

// A font's vertical metrics in pixels (as GDI's TEXTMETRIC: height = ascent + descent).
struct FontMetrics {
    int ascent = 0;          // baseline below the cell top
    int descent = 0;         // cell bottom below the baseline
    int height = 0;          // ascent + descent: DrawText's line height
    int lineGap = 0;         // external leading (DrawText uses it only with DT_EXTERNALLEADING)
    int averageWidth = 0;    // tmAveCharWidth: DT_EXPANDTABS stops are 8 of these apart
};

// One glyph at the font's size: the pen advance and the coverage bitmap's box relative to the pen
// on the baseline (y down; top is usually negative).
struct GlyphMetrics {
    int advance = 0;
    int left = 0, top = 0, width = 0, height = 0;
};

// One face at one pixel size. Implemented over stb_truetype (LoadFont); tests can implement it
// over anything else (a fixed-pitch fake, or GDI in the comparison tool).
class Font {
public:
    virtual ~Font() = default;
    virtual const FontMetrics& Metrics() const = 0;
    virtual GlyphMetrics Glyph(char32_t ch) const = 0;
    // Pair kerning in pixels, added to the left character's advance (DrawText on Windows 10 kerns).
    virtual int Kerning(char32_t left, char32_t right) const = 0;
    // Writes the glyph's 8-bit coverage (Glyph(ch).width x height) at `out`, rows `pitch` bytes apart.
    virtual void Rasterise(char32_t ch, std::uint8_t* out, int pitch) const = 0;
};

// Loads a TrueType file with stb_truetype at an em height of `pixels` (GDI's -lfHeight). Null when the
// file cannot be read or parsed. Files are shared between sizes.
std::unique_ptr<Font> LoadFont(const std::string& path, int pixels);

// Where font files are looked up: by default the repository's third_party/fonts (EI_FONT_DIR, set by
// the build); a host (the hook DLL, a standalone exe) sets its own folder once at start-up.
void SetFontDirectory(const std::string& directory);
std::string FontDirectory();
// The font file standing in for a GDI face name ("Times New Roman" -> LiberationSerif-Regular.ttf,
// "Courier New" -> LiberationMono-Regular.ttf); unknown faces get the serif one.
const char* FontFileForFace(std::string_view face);

// MFC's CFont::CreatePointFont (0070BBE5): tenths of a point to the em height in pixels at `dpi`
// (lfHeight = -(dpi * tenths / 720), integer division).
int PointSizeToPixels(int tenths, int dpi = 96);

// ---- Glyph atlas --------------------------------------------------------------------------------------

struct AtlasGlyph {
    GlyphMetrics metrics;
    int atlasX = 0, atlasY = 0;   // where the coverage box sits in the atlas
};

// 8-bit coverage of every glyph used so far, shelf-packed; grows downwards (Generation() changes
// whenever pixels are added or the image is reallocated, so a GPU copy knows to re-upload).
class GlyphAtlas {
public:
    // `smoothing` false thresholds coverage at one half (GDI's non-antialiased look).
    GlyphAtlas(const Font& font, bool smoothing);
    const AtlasGlyph& Get(char32_t ch);
    int Width() const { return width_; }
    int Height() const { return height_; }
    const std::uint8_t* Pixels() const { return pixels_.data(); }
    unsigned Generation() const { return generation_; }
    bool Smoothing() const { return smoothing_; }

private:
    void Place(AtlasGlyph& glyph, char32_t ch);
    const Font& font_;
    bool smoothing_;
    int width_, height_ = 0;
    int shelfX_ = 0, shelfY_ = 0, shelfHeight_ = 0;
    unsigned generation_ = 0;
    std::vector<std::uint8_t> pixels_;
    std::unordered_map<char32_t, AtlasGlyph> glyphs_;
};

// A font with its atlas and an advance cache: what layout and drawing take.
class TextFont {
public:
    TextFont(std::unique_ptr<Font> font, bool smoothing);
    const Font& Face() const { return *font_; }
    const FontMetrics& Metrics() const { return font_->Metrics(); }
    GlyphAtlas& Atlas() { return atlas_; }
    int Advance(char32_t ch);
    int Kerning(char32_t left, char32_t right);

private:
    std::unique_ptr<Font> font_;
    GlyphAtlas atlas_;
    std::vector<int> lowAdvances_;   // Latin and Cyrillic; -1 until asked
    std::unordered_map<char32_t, int> advances_;
    std::unordered_map<std::uint64_t, int> kerning_;
};

// The process-wide font for a GDI face name and em height in pixels (loaded on first use; null when
// the file is missing). Not thread-safe: call it from the thread that draws the interface.
TextFont* GetTextFont(std::string_view face, int pixels, bool smoothing = true);

// ---- Code page ----------------------------------------------------------------------------------------

// The game's text is Windows-1251 (docs/notes/standalone.md, U1). `count` -1 means up to the first
// zero byte. Bytes 1251 leaves undefined (0x98) map to the same code point, as Windows does.
std::u32string DecodeCp1251(const char* text, int count = -1);

// ---- Layout -------------------------------------------------------------------------------------------

// DrawText's format bits this layout follows (the values of DT_*). DT_VCENTER without DT_SINGLELINE
// (the game's format 4) has no effect, as in DrawText.
enum Format : unsigned {
    kLeft = 0x0,
    kCenter = 0x1,
    kRight = 0x2,
    kVCenter = 0x4,
    kWordBreak = 0x10,
    kExpandTabs = 0x40,
    kNoClip = 0x100,
    kCalcRect = 0x400,
    kNoPrefix = 0x800,
};
constexpr unsigned kSupportedFormats = kCenter | kRight | kVCenter | kWordBreak | kExpandTabs | kNoClip | kCalcRect | kNoPrefix;

struct Rect {
    int left = 0, top = 0, right = 0, bottom = 0;
};

struct GlyphQuad {
    int atlasX, atlasY, width, height;   // source in the atlas
    int x, y;                            // destination top-left (same size, unclipped)
};

struct TextLine {
    int begin, end;   // characters of the decoded text drawn on this line (prefix '&' included)
    int x, y;         // where the line starts
    int width;        // its extent (tab stops and prefixes applied)
};

struct TextLayout {
    int result = 0;                     // DrawText's return value (the height drawn)
    Rect rect;                          // the rectangle; with kCalcRect, DrawText's computed one
    Rect clip;                          // glyphs and underlines are clipped to it (unless kNoClip)
    bool clipped = true;
    std::vector<TextLine> lines;
    std::vector<GlyphQuad> quads;       // empty with kCalcRect
    std::vector<Rect> underlines;       // the '&' prefix underlines, drawn in the text colour
};

// Lays out `text` in `rect` the way DrawTextA(text, -1, rect, format) does with this font's metrics.
// False (and nothing written) for format bits outside kSupportedFormats.
bool Layout(TextFont& font, std::u32string_view text, const Rect& rect, unsigned format, TextLayout& out);

struct Extent {
    int cx = 0, cy = 0;
};
// GetTextExtentPoint32: the advances summed (no kerning, prefix or tab handling) and the font's height.
Extent TextExtent(TextFont& font, std::u32string_view text);

// ---- CPU composition ----------------------------------------------------------------------------------

// Draws a layout into 32-bit pixels (0x00RRGGBB in memory order B, G, R, X) as text drawn onto black:
// each pixel becomes colour x coverage (rounded), with alpha 0xFF wherever coverage > 0; overlapping
// glyphs keep the larger coverage; underlines have full coverage. `colour` is a COLORREF (0x00BBGGRR).
// Pixels outside the clip rectangle and the image are left alone. `pitch` is in pixels.
void ComposeAgainstBlack(TextFont& font, const TextLayout& layout, std::uint32_t colour, std::uint32_t* pixels,
                         int width, int height, int pitch);

} // namespace render::text
