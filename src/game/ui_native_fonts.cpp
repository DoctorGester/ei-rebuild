// The fonts native text is drawn in (ui_native.h, TextFontOf; docs/notes/ui-native.md, "P2 done"): the glyphs' shapes
// from our own fonts (render_text.h's stand-ins built into the executable, Liberation Serif for Times New Roman),
// placed by the GDI font's metrics: its ascent, descent and average width, each character's advance and the kerning
// pairs. So the layout (line breaks, alignment, DrawText's value) is the one GDI's DrawText gives and the game's own
// measuring (CInterface3D::TextExtent, GDI's extent) agrees with what is drawn; only the glyphs' shapes are ours.
// This is the one place native text asks GDI anything, and it draws nothing with it.
#include "ui_native.h"

#include "lib/gameapi.h"
#include "render/render_text.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <tuple>
#include <unordered_map>

namespace ui_native {

namespace {

namespace glyphs = render::text;

// A GDI font's metrics with a stand-in font's glyph shapes. Holds its own copy of the GDI font, selected into a
// memory DC of its own, so the interface may delete and make its fonts again.
class GdiPlacedFont final : public glyphs::Font {
public:
    GdiPlacedFont(const LOGFONTA& described, std::unique_ptr<glyphs::Font> shapes) : shapes_(std::move(shapes)) {
        dc_ = ::CreateCompatibleDC(nullptr);
        font_ = gameapi::CreateFontIndirectA(&described);
        if (dc_ && font_) ::SelectObject(dc_, font_);
        TEXTMETRICW tm{};
        if (dc_) ::GetTextMetricsW(dc_, &tm);
        overhang_ = tm.tmOverhang;
        metrics_.ascent = tm.tmAscent;
        metrics_.descent = tm.tmDescent;
        metrics_.height = tm.tmHeight;
        metrics_.lineGap = tm.tmExternalLeading;
        metrics_.averageWidth = tm.tmAveCharWidth;
        const DWORD pairs = dc_ ? ::GetKerningPairsW(dc_, 0, nullptr) : 0;
        if (pairs) {
            std::unique_ptr<KERNINGPAIR[]> table(new KERNINGPAIR[pairs]);
            const DWORD read = ::GetKerningPairsW(dc_, pairs, table.get());
            for (DWORD i = 0; i < read; ++i)
                if (table[i].iKernAmount) kerning_[Pair(table[i].wFirst, table[i].wSecond)] = table[i].iKernAmount;
        }
    }
    ~GdiPlacedFont() override {
        if (dc_) ::DeleteDC(dc_);
        if (font_) ::DeleteObject(font_);
    }
    GdiPlacedFont(const GdiPlacedFont&) = delete;
    GdiPlacedFont& operator=(const GdiPlacedFont&) = delete;

    bool Valid() const { return dc_ && font_ && metrics_.height > 0; }

    const glyphs::FontMetrics& Metrics() const override { return metrics_; }

    // The stand-in's coverage box (relative to the pen on the baseline) with GDI's advance.
    glyphs::GlyphMetrics Glyph(char32_t ch) const override {
        glyphs::GlyphMetrics glyph = shapes_->Glyph(ch);
        glyph.advance = Advance(ch);
        return glyph;
    }

    int Kerning(char32_t left, char32_t right) const override {
        const auto found = kerning_.find(Pair(left, right));
        return found == kerning_.end() ? 0 : found->second;
    }

    void Rasterise(char32_t ch, std::uint8_t* out, int pitch) const override { shapes_->Rasterise(ch, out, pitch); }

private:
    static std::uint64_t Pair(char32_t left, char32_t right) { return (std::uint64_t(left) << 32) | right; }

    // GetTextExtentPoint32's width less the overhang; a control character's is the width DrawText (Uniscribe) gives it
    // between two letters, not the extent's.
    int Advance(char32_t ch) const {
        if (ch > 0xFFFF) return shapes_->Glyph(ch).advance;
        const wchar_t wide = static_cast<wchar_t>(ch);
        if (ch >= 0x20) {
            SIZE size{};
            ::GetTextExtentPoint32W(dc_, &wide, 1, &size);
            return size.cx - overhang_;
        }
        const wchar_t with[3] = {L'a', wide, L'a'};
        const wchar_t without[2] = {L'a', L'a'};
        RECT a{0, 0, 10000, 100}, b{0, 0, 10000, 100};
        ::DrawTextW(dc_, with, 3, &a, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
        ::DrawTextW(dc_, without, 2, &b, DT_CALCRECT | DT_NOPREFIX | DT_SINGLELINE);
        return a.right - b.right;
    }

    std::unique_ptr<glyphs::Font> shapes_;
    HDC dc_ = nullptr;
    HFONT font_ = nullptr;
    int overhang_ = 0;
    glyphs::FontMetrics metrics_;
    std::unordered_map<std::uint64_t, int> kerning_;
};

} // namespace

glyphs::TextFont* TextFontOf(HFONT font) {
    LOGFONTA described{};
    if (!font || !gameapi::GetObjectA(font, sizeof(described), &described)) return nullptr;
    // GDI's default quality follows the system's setting (the credits switch it off); the harness makes the game's
    // fonts without smoothing.
    bool smoothing = described.lfQuality != NONANTIALIASED_QUALITY;
    if (smoothing && described.lfQuality != ANTIALIASED_QUALITY && described.lfQuality != CLEARTYPE_QUALITY) {
        BOOL systemSmoothing = TRUE;
        if (gameapi::SystemParametersInfoA(SPI_GETFONTSMOOTHING, 0, &systemSmoothing, 0)) smoothing = systemSmoothing != FALSE;
    }
    const std::string face(described.lfFaceName, ::strnlen(described.lfFaceName, LF_FACESIZE));
    const int pixels = std::abs(described.lfHeight);
    // Process-wide, as the queued glyph runs keep pointers to the fonts' atlases.
    static std::map<std::tuple<std::string, int, int, bool>, std::unique_ptr<glyphs::TextFont>> fonts;
    const auto key = std::make_tuple(face, pixels, static_cast<int>(described.lfWeight), smoothing);
    const auto found = fonts.find(key);
    if (found != fonts.end()) return found->second.get();
    std::unique_ptr<glyphs::TextFont>& slot = fonts[key];
    if (auto shapes = glyphs::LoadFont(glyphs::FontForFace(face), pixels)) {
        auto placed = std::make_unique<GdiPlacedFont>(described, std::move(shapes));
        if (placed->Valid()) slot = std::make_unique<glyphs::TextFont>(std::move(placed), smoothing);
    }
    return slot.get();
}

} // namespace ui_native
