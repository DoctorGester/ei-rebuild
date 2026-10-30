// The interface's fonts (ui_font.h): our own fonts at the interface's sizes, with their metrics.
#include "ui_font.h"

#include "asset_text.h"

#include "platform/system.h"
#include "render/render_text.h"

namespace {
constexpr int kDotsPerInch = 96;
} // namespace

UiFont PointFont(int tenths, const char* face) { return UiFont{face, render::text::PointSizeToPixels(tenths, kDotsPerInch)}; }

render::text::TextFont* TextFontOf(const UiFont& font) {
    if (!font) return nullptr;
    const bool smoothing = !font.unsmoothed && platform::FontSmoothing();
    return render::text::GetTextFont(font.face, font.pixels, smoothing);
}

SIZE TextExtent(const UiFont& font, std::string_view text) {
    render::text::TextFont* measured = TextFontOf(font);
    if (!measured) return SIZE{0, 0};
    const render::text::Extent extent = render::text::TextExtent(*measured, ::text::CodePoints(text));
    return SIZE{extent.cx, extent.cy};
}
