// The interface's fonts (ui_font.h): our own fonts at the interface's sizes, with their metrics.
#include "ui_font.h"

#include "system_text.h"

#include "lib/gameapi.h"
#include "render/render_text.h"

#include <windows.h>

namespace {
constexpr int kDotsPerInch = 96;
} // namespace

UiFont PointFont(int tenths, const char* face) { return UiFont{face, render::text::PointSizeToPixels(tenths, kDotsPerInch)}; }

render::text::TextFont* TextFontOf(const UiFont& font) {
    if (!font) return nullptr;
    BOOL smoothing = FALSE;
    if (!font.unsmoothed && !gameapi::SystemParametersInfoA(SPI_GETFONTSMOOTHING, 0, &smoothing, 0)) smoothing = TRUE;
    return render::text::GetTextFont(font.face, font.pixels, smoothing != FALSE);
}

SIZE TextExtent(const UiFont& font, std::string_view text) {
    render::text::TextFont* measured = TextFontOf(font);
    if (!measured) return SIZE{0, 0};
    const render::text::Extent extent = render::text::TextExtent(*measured, CodePoints(text));
    return SIZE{extent.cx, extent.cy};
}

std::u32string CodePoints(std::string_view text) {
    const std::wstring wide = ::text::Wide(text);
    std::u32string out;
    out.reserve(wide.size());
    for (std::size_t i = 0; i < wide.size(); ++i) {
        const char32_t unit = wide[i];
        if (unit >= 0xD800 && unit < 0xDC00 && i + 1 < wide.size() && wide[i + 1] >= 0xDC00 && wide[i + 1] < 0xE000) {
            out.push_back(0x10000 + ((unit - 0xD800) << 10) + (static_cast<char32_t>(wide[i + 1]) - 0xDC00));
            ++i;
        } else {
            out.push_back(unit);
        }
    }
    return out;
}
