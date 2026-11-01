// The titles' scrolling text surface (UiScrollingSurface, ui_screens.h): a window surface holding an RTF text
// (outro.rtf), laid out once by render_rich_text as RichEdit 2.0 placed it and drawn onto the surface's canvas as
// glyph runs at a scroll offset that moves with the game time. Twips and font sizes go to pixels at a DPI taken from
// the window's size, as the interface's fonts are sized (TitlesDpi), not from the system's (docs/notes/ui-native.md,
// "The titles' rich text"). See docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"
#include "screen_layout.h"
#include "game_strings.h"

#include "resource.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "render/render_rich_text.h"
#include "render/render_ui.h"

#include <cstdint>
#include <string_view>

namespace {
constexpr int kTwipsPerInch = 1440;
constexpr int kDesignDpi = 96;           // the dots an inch at the design width, as the interface's point sizes are measured
constexpr int kDesignWidth = 800;        // the layout's width
constexpr int kExtraTwipsBelow = 1000;   // the text is formatted this far below the visible height
constexpr std::uint32_t kWhite = 0xFFFFFF;

// The DPI twips and points are measured at: the interface's font scale (Interface3D::Create sizes its fonts by the
// window's width over FontScale, 0.01 on a 4:3 window), 96 at 800 pixels wide. Text and layout then grow with the
// window and ignore the system's DPI.
int TitlesDpi(const UiSurface* surface) {
    const float scale = static_cast<float>(surface->ui->width) / kDesignWidth * (0.01f / screen_layout::FontScale());
    return static_cast<int>(scale * kDesignDpi + 0.5f);
}

// `a` * `b` / `divisor` rounded to the nearest, a half away from zero; -1 for a divisor of 0 or a result past 31 bits
// (USER32's MulDiv, which the titles used).
int MulDiv(int a, int b, int divisor) {
    if (divisor == 0) return -1;
    std::int64_t product = static_cast<std::int64_t>(a) * b;
    std::int64_t by = divisor;
    if (by < 0) {
        product = -product;
        by = -by;
    }
    const std::int64_t half = by / 2;
    const std::int64_t result = (product >= 0 ? product + half : product - half) / by;
    if (result > 2147483647 || result < -2147483647) return -1;
    return static_cast<int>(result);
}

int PixelsToTwips(const UiSurface* surface, int pixels) { return MulDiv(pixels, kTwipsPerInch, TitlesDpi(surface)); }

int HeightInTwips(const UiScrollingSurface* surface) { return PixelsToTwips(surface, surface->rect.bottom - surface->rect.top); }
} // namespace

UiScrollingSurface::UiScrollingSurface(Interface3D* ui, const Rect* rect) : UiSurface(ui, rect, SurfaceBlend::Keyed) {
    background = kWhite;
}

UiScrollingSurface::~UiScrollingSurface() = default;

bool UiScrollingSurface::Load(MappedResource* archive, const char* name) {
    std::uint32_t size = 0;
    void* data = archive->GetFile(name, &size);
    if (data) LoadRtf(data, size);
    archive->CloseFile(data);
    return data != nullptr;
}

void UiScrollingSurface::LoadRtf(const void* data, std::uint32_t size) {
    const int dpi = TitlesDpi(this);
    const int rectWidth = render::text::TwipsToPixels(PixelsToTwips(this, rect.right - rect.left), dpi);
    // Drawn without smoothing, as RichEdit drew with the titles' font smoothing off.
    text = std::make_unique<render::text::RichLayout>(render::text::LayoutRichText(
        render::text::ReadRtf(std::string_view(static_cast<const char*>(data), size)), rectWidth, dpi, false));
    offset = 0;
    RenderText();
}

void UiScrollingSurface::RenderText() {
    if (!holder || !text) return;
    const int dpi = TitlesDpi(this);
    const render::Rect page{0, 0, width, height};
    render::ui::Fill(holder, page, background);
    const int top = render::text::TwipsToPixels(-offset, dpi);
    for (const render::text::RichDraw& draw :
         render::text::RichDraws(*text, render::text::TwipsToPixels(-offsetX, dpi), top, render::Rect{0, 0, width, height}))
        render::ui::GlyphRun(holder, *draw.font, draw.layout, draw.colour);
    const int textHeight = HeightInTwips(this);
    if (endOffset > 0) {
        if (offset > endOffset + kExtraTwipsBelow + textHeight) {
            speed = 0.0f;
            endOffset = 0;
        }
    } else if (top + text->height <= render::text::TwipsToPixels(kExtraTwipsBelow + textHeight, dpi)) {
        endOffset = offset;
    }
}

void UiScrollingSurface::Blit() {
    EI_NAN_AUDIT(speed);
    if (speed != 0.0f) {
        const float now = GameTime();
        offset = static_cast<int>((now - startTime) * speed + offset);
        startTime = now;
        RenderText();
    }
    UiSurface::Blit();
}

void UiScrollingSurface::Start(float newSpeed, bool fromBelow) {
    endOffset = 0;
    offset = fromBelow ? -HeightInTwips(this) : 0;
    speed = newSpeed;
    startTime = GameTime();
}
