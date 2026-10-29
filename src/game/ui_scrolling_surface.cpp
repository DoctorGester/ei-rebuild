// The titles' scrolling text surface (CUiScrollingSurface, ui_screens.h): a window surface holding an RTF text
// (outro.rtf), laid out once by render_rich_text as RichEdit 2.0 placed it and drawn onto the surface's canvas as
// glyph runs at a scroll offset that moves with the game time. Twips and font sizes go to pixels at a DPI taken from
// the window's size, as the interface's fonts are sized (TitlesDpi), not from the system's (docs/notes/ui-native.md,
// "The titles' rich text"). See docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"
#include "ui_widescreen.h"
#include "game_strings.h"

#include "resource.h"
#include "world.h"

#include "lib/crt.h"
#include "lib/nan_audit.h"
#include "render/render_rich_text.h"
#include "render/render_ui.h"

#include <windows.h>

#include <string_view>

namespace {
constexpr int kTwipsPerInch = 1440;
constexpr int kDesignDpi = 96;           // the dots an inch at the design width, as the interface's point sizes are measured
constexpr int kDesignWidth = 800;        // the layout's width
constexpr int kExtraTwipsBelow = 1000;   // the text is formatted this far below the visible height
constexpr COLORREF kWhite = 0xFFFFFF;

// The DPI twips and points are measured at: the interface's font scale (CInterface3D::Create sizes its fonts by the
// window's width over FontScale, 0.01 on a 4:3 window), 96 at 800 pixels wide. Text and layout then grow with the
// window and ignore the system's DPI.
int TitlesDpi(const CUiSurface* surface) {
    const float scale = static_cast<float>(surface->interface_->width_) / kDesignWidth * (0.01f / widescreen::FontScale());
    return static_cast<int>(scale * kDesignDpi + 0.5f);
}

int PixelsToTwips(const CUiSurface* surface, int pixels) { return ::MulDiv(pixels, kTwipsPerInch, TitlesDpi(surface)); }

int HeightInTwips(const CUiScrollingSurface* surface) { return PixelsToTwips(surface, surface->rect_.bottom - surface->rect_.top); }
} // namespace

CUiScrollingSurface::CUiScrollingSurface(CInterface3D* ui, const RECT* rect) : CUiSurface(ui, rect, SurfaceBlend::Keyed) {
    background_ = kWhite;
}

CUiScrollingSurface::~CUiScrollingSurface() = default;

bool CUiScrollingSurface::Load(CMappedResource* archive, const char* name) {
    std::uint32_t size = 0;
    void* data = archive->GetFile(name, &size);
    if (data) LoadRtf(data, size);
    archive->CloseFile(data);
    return data != nullptr;
}

void CUiScrollingSurface::LoadRtf(const void* data, std::uint32_t size) {
    const int dpi = TitlesDpi(this);
    const int width = render::text::TwipsToPixels(PixelsToTwips(this, rect_.right - rect_.left), dpi);
    // Drawn without smoothing, as RichEdit drew with the titles' font smoothing off.
    text_ = std::make_unique<render::text::RichLayout>(render::text::LayoutRichText(
        render::text::ReadRtf(std::string_view(static_cast<const char*>(data), size)), width, dpi, false));
    offset_ = 0;
    RenderText();
}

void CUiScrollingSurface::RenderText() {
    if (!holder_ || !text_) return;
    const int dpi = TitlesDpi(this);
    const render::Rect page{0, 0, width_, height_};
    render::ui::Fill(holder_, page, background_);
    const int top = render::text::TwipsToPixels(-offset_, dpi);
    for (const render::text::RichDraw& draw :
         render::text::RichDraws(*text_, render::text::TwipsToPixels(-offsetX_, dpi), top, render::text::Rect{0, 0, width_, height_}))
        render::ui::GlyphRun(holder_, *draw.font, draw.layout, draw.colour);
    const int height = HeightInTwips(this);
    if (endOffset_ > 0) {
        if (offset_ > endOffset_ + kExtraTwipsBelow + height) {
            speed_ = 0.0f;
            endOffset_ = 0;
        }
    } else if (top + text_->height <= render::text::TwipsToPixels(kExtraTwipsBelow + height, dpi)) {
        endOffset_ = offset_;
    }
}

void CUiScrollingSurface::Blit() {
    EI_NAN_AUDIT(speed_);
    if (speed_ != 0.0f) {
        const float now = GameTime();
        offset_ = static_cast<int>(libcrt::Ftol((static_cast<double>(now) - startTime_) * speed_ + offset_));
        startTime_ = now;
        RenderText();
    }
    CUiSurface::Blit();
}

void CUiScrollingSurface::Start(float speed, bool fromBelow) {
    endOffset_ = 0;
    offset_ = fromBelow ? -HeightInTwips(this) : 0;
    speed_ = speed;
    startTime_ = GameTime();
}
