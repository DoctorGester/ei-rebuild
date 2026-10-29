// The titles' scrolling text surface (CUiScrollingSurface, ui_screens.h): a window surface holding an RTF text
// (outro.rtf), laid out once by render_rich_text as RichEdit 2.0 placed it and drawn onto the surface's canvas as
// glyph runs at a scroll offset that moves with the game time. Twips and font sizes go to pixels at the system's
// DPI, as RichEdit's memory DC measured them (docs/notes/ui-native.md, "The titles' rich text"). See docs/notes/items_spells.md.
#include "ui.h"
#include "ui_screens.h"
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
constexpr int kExtraTwipsBelow = 1000;   // the text is formatted this far below the visible height
constexpr int kSurfaceFormat = 5;
constexpr COLORREF kWhite = 0xFFFFFF;

// The DPI twips and points are measured at: the system's (the process is system DPI aware), what GetDeviceCaps gave
// the original's memory DC.
int Dpi() { return static_cast<int>(::GetDpiForSystem()); }

int PixelsToTwips(int pixels) { return ::MulDiv(pixels, kTwipsPerInch, Dpi()); }

int HeightInTwips(const CUiScrollingSurface* surface) { return PixelsToTwips(surface->rect_.bottom - surface->rect_.top); }
} // namespace

CUiScrollingSurface::CUiScrollingSurface(CInterface3D* ui, const RECT* rect) : CUiSurface(ui, rect, kSurfaceFormat) {
    offsetX_ = 0;
    background_ = kWhite;
    speed_ = 0.0f;
    offset_ = 0;
    endOffset_ = 0;
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
    const int dpi = Dpi();
    const int width = render::text::TwipsToPixels(PixelsToTwips(rect_.right - rect_.left), dpi);
    // Drawn without smoothing, as RichEdit drew with the titles' font smoothing off.
    text_ = std::make_unique<render::text::RichLayout>(render::text::LayoutRichText(
        render::text::ReadRtf(std::string_view(static_cast<const char*>(data), size)), width, dpi, false));
    offset_ = 0;
    RenderText();
}

void CUiScrollingSurface::RenderText() {
    if (!holder_ || !text_) return;
    const int dpi = Dpi();
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
