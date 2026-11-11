// The window surface (UiSurface): a rectangle of the interface a window draws into, drawn into the frame each frame
// by its Blit: its frame picture, then its window's paint (PaintWith), which draws the window's current state straight
// into the frame. Every live surface is in a registry so a resolution change places them all again
// (UiSurface::RelayoutAll). See docs/notes/ui-native.md, section 3.
#include "game/app/app.h"

#include "engine/core/asset_text.h"
#include "game/ui/screen_layout.h"
#include "engine/scene/surface.h"
#include "game/ui/ui.h"
#include "game/ui/ui_font.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "platform/log.h"
#include "render/render_text.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace {
// Every surface alive, for RelayoutAll (windows, the interface's own and those a window keeps outside its list).
std::vector<UiSurface*> g_surfaces;

constexpr std::uint32_t kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
} // namespace

UiSurface::~UiSurface() {
    if (auto it = std::find(g_surfaces.begin(), g_surfaces.end(), this); it != g_surfaces.end()) {
        *it = g_surfaces.back();
        g_surfaces.pop_back();
    }
    render::ui::DropPicture(this);
}

UiSurface::UiSurface(int left, int top, int right, int bottom, SurfaceBlend blend, bool scale)
    : UiSurface(Rect{left, top, right, bottom}, blend, scale) {}

UiSurface::UiSurface(const Rect& area, SurfaceBlend newBlend, bool scale)
    : blend(newBlend), rect(area), layoutRect(area), scaled(scale) {
    g_surfaces.push_back(this);
    if (scale) rect = screen_layout::ToScreen(rect);
    width = rect.Width();
    height = rect.Height();
    if (width <= 0 || height <= 0) ThrowSurfaceNotMade(width, height);
}

UiSurface::UiSurface(const Rect* rect, SurfaceBlend blend)
    : UiSurface(*rect, blend, true) {}

void UiSurface::MoveTo(Point at) {
    if (scaled) layoutRect.Offset(at - layoutRect.TopLeft());
    Point point = screen_layout::ToScreen(at);
    rect.Offset(point - rect.TopLeft());
}

// A surface over a layout rectangle takes it scaled as the layout scales now; one over the whole screen (the
// backdrop) the whole new screen. Its paint draws at the new size from the next frame.
void UiSurface::Relayout(const screen_layout::Layout& before) {
    if (scaled) {
        rect = screen_layout::ToScreen(layoutRect);
    } else if (rect == Rect{0, 0, before.width, before.height}) {
        const screen_layout::Layout& now = screen_layout::Get();
        rect = Rect{0, 0, now.width, now.height};
        layoutRect = rect;
    }
    width = std::max(rect.Width(), 1);
    height = std::max(rect.Height(), 1);
}

void UiSurface::RelayoutAll(const screen_layout::Layout& before) {
    for (UiSurface* surface : g_surfaces) surface->Relayout(before);
}

void UiSurface::Show() { shown = true; }

void UiSurface::Hide() { shown = false; }

// --- Draws -------------------------------------------------------------------------------------------

// Whether a fill of `colour` leaves the surface's pixels out (keyed on that colour).
bool UiSurface::Transparent(std::uint32_t colour) const { return blend == SurfaceBlend::Keyed && colour == background; }

void UiSurface::FrameRect(const Rect& area, std::uint32_t colour) {
    if (!painting || Transparent(colour)) return;
    Rect r{0, 0, width, height};
    if (!area.IsEmpty()) {
        r = screen_layout::ToScreenNoMargin(area);   // the surface's own pixels: no window margin
    }
    for (const Rect& side : render::ui::FrameRects(r.Offsetted(rect.TopLeft()))) render::ui::Solid(side, colour, paintClip);
}

void UiSurface::Fill(std::uint32_t colour) { FillRect(Rect{0, 0, width, height}, colour); }

void UiSurface::Clear() { Fill(background); }

void UiSurface::FillRect(const Rect& area, std::uint32_t colour) {
    if (!painting || Transparent(colour)) return;
    const Rect r = area.Intersection(Rect{0, 0, width, height});
    if (!r.IsEmpty()) render::ui::Solid(r.Offsetted(rect.TopLeft()), colour, paintClip);
}

void UiSurface::Line(Point from, Point to, int lineWidth, std::uint32_t colour) {
    for (const Rect& r : render::ui::LineRects(from, to, lineWidth)) FillRect(r, colour);
}

void UiSurface::Dot(Point at, std::uint32_t colour) { FillRect(Rect::Sized(at, {1, 1}), colour); }

// --- Text --------------------------------------------------------------------------------------------

// Text on the interface's surfaces in our own fonts (ui_font.h): laid out by DrawText's rules (render_text.h's
// Layout). Rebirth's WideScreen patches the rectangle (scaled without the margin), which DrawTextIn reproduces for
// every window (screen_layout.h). See docs/notes/ui-native.md, "Fonts and metrics".

int UiSurface::DrawTextIn(std::string_view text, const Rect& area, const TextStyle& style) {
    Rect frame{0, 0, rect.Width(), rect.Height()};
    if (!area.IsEmpty()) {
        // WideScreen (addon replaces the interface argument by 0): without the margin.
        frame = screen_layout::ToScreenNoMargin(area);
        frame.right += kRectSlack;
        frame.bottom += kRectSlack;
    }
    // The original scales a point whose x is an unwritten local; only y is used.
    const int textHeight = DrawTextFrame(g_interface.fonts[style.font], text, frame, style.format, style.colour, style.shadow);
    return screen_layout::FromScreenNoMargin(Point{0, textHeight}).y;
}

int UiSurface::DrawTextPixels(const UiFont& font, std::string_view text, const Rect& area, render::text::Format format, std::uint32_t colour,
                               bool shadow) {
    return DrawTextFrame(font, text, area, format, colour, shadow);
}

// `text` laid out in `font` in `frame` (surface pixels) and, inside the paint and not in the key colour, drawn at the
// surface's place, its shadow first.
int UiSurface::DrawTextFrame(const UiFont& font, std::string_view text, const Rect& frame, render::text::Format format, std::uint32_t colour,
                             bool shadow) {
    render::text::TextFont* laidFont = TextFontOf(font);
    if (!laidFont) return 0;
    render::text::TextLayout layout;
    render::text::Layout(*laidFont, ::text::CodePoints(text), frame, format, layout);
    if (painting && !(lib::Has(format, render::text::Format::CalcRect)) && !Transparent(colour)) {
        const Point origin = rect.TopLeft();
        if (shadow) render::ui::Glyphs(*laidFont, layout, origin + Point{1, 1}, kShadowColour, paintClip);
        render::ui::Glyphs(*laidFont, layout, origin, colour, paintClip);
    }
    return layout.result;
}

// --- Blit --------------------------------------------------------------------------------------------

void UiSurface::PaintWith(std::function<void(UiSurface&)> newPaint) { paint = std::move(newPaint); }

void PaintLabels(UiSurface& surface, const std::vector<UiLabel>& labels) {
    for (const UiLabel& label : labels) {
        if (label.maxLines > 0)
            surface.DrawWrappedText(label.text, label.area, label.maxLines, label.style);
        else
            surface.DrawTextIn(label.text, label.area, label.style);
    }
}

// A shown surface's draw into the frame: its frame picture (stretched over its rectangle, greyed after Greyscale), then
// its window's paint at its rectangle, clipped to it and to the screen; a keyed surface leaves out what is in its
// background colour (a text's antialiased edges blend over the frame). See docs/notes/ui.md, "Surface blit".
void UiSurface::Blit() {
    if (!shown) return;
    const Rect clip = Rect{0, 0, g_interface.width, g_interface.height}.Intersection(rect);
    if (clip.IsEmpty()) return;
    if (render::ui::HasPicture(this)) render::ui::DrawPicture(this, rect, greyed);
    if (!paint) return;
    paintClip = clip;
    painting = true;
    paint(*this);
    painting = false;
}

// --- Pictures ----------------------------------------------------------------------------------------

// The screen copied into a surface (the escape menu's backdrop, the save's picture):
// a frame picture of the surface's size, kept on the GPU (render_ui.h), drawn plain until Greyscale.
void UiSurface::CaptureScreen() {
    greyed = false;
    render::ui::CapturePicture(this, rect, width, height);
}

save_picture::Picture UiSurface::ReadPicture() const {
    std::optional<render::Picture> read = render::ui::ReadPicture(this);
    if (!read) return {};
    return {read->width, read->height, std::move(read->rgba)};
}

void UiSurface::Greyscale() { greyed = true; }
