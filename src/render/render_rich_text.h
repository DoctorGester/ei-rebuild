#pragma once

// Rich text without RichEdit: the RTF the titles scroll (outro.rtf in outro.res, written by Word) read into
// paragraphs and laid out as RichEdit 2.0's EM_FORMATRANGE places it, in render::text's fonts. See
// docs/notes/ui-native.md, "The titles' rich text".

#include "render_text.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace render::text {

// ---- The text ----------------------------------------------------------------------------------------

enum class RichAlign { Left, Centre, Right };

// A character format: \f's face, \fs, \b, \i, \cf.
struct RichStyle {
    std::string face;            // the font table's name ("Times New Roman Cyr")
    int halfPoints = 24;         // \fs
    bool bold = false;
    bool italic = false;
    std::uint32_t colour = 0;    // a COLORREF; \cf0 (automatic) is black
};

struct RichRun {
    RichStyle style;
    std::u32string text;         // a tab is U'\t', \line U'\n'
};

struct RichTab {
    int position = 0;            // twips from the left margin
    bool right = false;          // \tqr: the text after the tab ends at the stop
};

struct RichParagraph {
    RichAlign align = RichAlign::Left;
    int left = 0;                // \li, twips
    int first = 0;               // \fi: the first line's indent from \li, twips
    int right = 0;               // \ri, twips
    std::vector<RichTab> tabs;
    std::vector<RichRun> runs;
    RichStyle mark;              // the paragraph mark's format: an empty line's height
};

struct RichText {
    std::vector<RichParagraph> paragraphs;
    int defaultTab = 720;        // \deftab, twips
};

// Reads the RTF subset Word writes for plain formatted text: the font and colour tables, \pard / \plain, \par and
// \line, \b / \i, \fs, \f, \cf, \ql / \qc / \qr (\qj as left), \li / \fi / \ri, \tx / \tqr, \tab, \'xx in the font's
// character set (Windows-1251 for \fcharset204, 1252 for 0, else \ansicpg), \u with \uc, and the escaped characters.
// Destinations it has no use for (\stylesheet, \info, every \* group) are skipped; unknown control words are left out.
// Paragraph and character formats are kept per group. The text after the last \par, when any, is a paragraph too.
RichText ReadRtf(std::string_view rtf);

// ---- Layout ------------------------------------------------------------------------------------------

// Twips to pixels at `dpi` (MulDiv's rounding: halves away from zero).
int TwipsToPixels(int twips, int dpi);

struct RichGlyph {
    TextFont* font;
    std::uint32_t colour;        // a COLORREF
    char32_t ch;
    int x;                       // the pen from the page's left, pixels
};

struct RichLine {
    int top = 0;                 // from the text's top, pixels
    int height = 0;              // the tallest cell ascent plus the deepest cell descent of its characters (and its mark's)
    int ascent = 0;              // the baseline below the top
    std::vector<RichGlyph> glyphs;
};

struct RichLayout {
    std::vector<RichLine> lines;
    int height = 0;              // all lines, pixels
};

// Lays `text` out on a page `width` pixels wide at `dpi`, as RichEdit 2.0 does: each style's font at
// MulDiv(\fs, dpi, 144) pixels (the face's stand-in, render_text.h's FontForFace; bold emboldened by
// BoldFont, italic in the upright shapes), advances without kerning, lines broken after spaces (a word
// wider than the line inside it), trailing spaces left out of the alignment, indents and tab stops in
// twips at `dpi`.
RichLayout LayoutRichText(const RichText& text, int width, int dpi, bool smoothing);

// The font a bold style is drawn in: `face` at `pixels` widened by max(1, (pixels + 10) / 20) pixels, each glyph's
// coverage spread that far to the right and its advance that much longer (GDI's emboldening, as no bold face is
// built in). Made on first use and kept for the process, like GetTextFont.
TextFont* BoldFont(std::string_view face, int pixels, bool smoothing);

// What one GlyphRun draws: the glyphs of one font and colour.
struct RichDraw {
    TextFont* font;
    std::uint32_t colour;
    TextLayout layout;
};

// The glyphs of the lines of `layout` that `clip` shows, with the text's top left at (x, y), as GlyphRun layouts
// clipped to `clip`, one per font and colour.
std::vector<RichDraw> RichDraws(const RichLayout& layout, int x, int y, const Rect& clip);

} // namespace render::text
