// The interface's fonts (ui_font.h): our own fonts at the interface's sizes, with their metrics.
#include "game/ui/ui_font.h"

#include "engine/core/asset_text.h"

#include "render/render_text.h"

namespace {
constexpr int kDotsPerInch = 96;
} // namespace

UiFont PointFont(int tenths, const char* face) { return UiFont{face, render::text::PointSizeToPixels(tenths, kDotsPerInch)}; }

render::text::TextFont* TextFontOf(const UiFont& font) {
    if (!font) return nullptr;
    return render::text::GetTextFont(font.face, font.pixels, !font.unsmoothed);
}

Size TextExtent(const UiFont& font, std::string_view text) {
    render::text::TextFont* measured = TextFontOf(font);
    if (!measured) return Size{0, 0};
    return render::text::TextExtent(*measured, ::text::CodePoints(text));
}
