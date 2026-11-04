// The window surface (UiSurface): a rectangle of the interface a window draws into, drawn into the frame each frame
// by its Blit. A window that paints its surface (PaintWith) draws its current state into the frame from Blit; a
// surface whose window does not paint yet keeps the draws it was given in the coordinates they came in and Blit
// draws them again at the current layout, with the current fonts. Every live surface is in a registry so a
// resolution change places them all again (UiSurface::RelayoutAll). See docs/notes/ui-native.md, section 3.
#include "app.h"

#include "asset_text.h"
#include "screen_layout.h"
#include "surface.h"
#include "ui.h"
#include "ui_font.h"
#include "lib/crt.h"
#include "lib/rect.h"
#include "platform/log.h"
#include "render/render_text.h"
#include "render/render_ui.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

// A draw a surface keeps (UiSurface::draws). A fill or frame is in surface pixels at the size `at` it was made at
// (whole: all of the surface); a text either in layout units with one of the interface's fonts or in surface pixels
// with its own font, laid out again when the font or its pixel rectangle changed.
struct UiSurfaceDraw {
    enum class Kind : std::uint8_t { Fill, Frame, Text };
    Kind kind = Kind::Fill;
    bool whole = false;
    Rect area;
    Size at;
    std::uint32_t colour = 0;
    // Text:
    bool layoutUnits = false;
    int font = 0;                 // with layoutUnits: ui->fonts[font]
    UiFont pixelFont;             // without
    std::u32string text;
    std::uint32_t format = 0;
    bool shadow = false;
    render::text::TextFont* laidFont = nullptr;
    Rect laidRect;
    render::text::TextLayout layout;
};

namespace {
// Every surface alive, for RelayoutAll (windows, the interface's own and those a window keeps outside its list).
std::vector<UiSurface*> g_surfaces;

constexpr std::uint32_t kShadowColour = 0x080808;
constexpr int kRectSlack = 2;           // pixels added to a scaled rectangle's right and bottom
// DT_MODIFYSTRING (the connection list's names, with DT_END_ELLIPSIS): DrawText would write the shortened text back
// into the caller's buffer, which the game never reads; nothing is written.
constexpr std::uint32_t kModifyString = 0x10000;
// More draws kept than any window makes: a window that adds without clearing.
constexpr std::size_t kManyDraws = 4096;

// `r` made at `from` scaled to `to`.
Rect ScaledArea(const Rect& r, Size from, Size to) {
    if (from == to || from.w <= 0 || from.h <= 0) return r;
    auto sx = [&](int v) { return static_cast<int>(static_cast<long long>(v) * to.w / from.w); };
    auto sy = [&](int v) { return static_cast<int>(static_cast<long long>(v) * to.h / from.h); };
    return Rect{sx(r.left), sy(r.top), sx(r.right), sy(r.bottom)};
}
} // namespace

UiSurface::~UiSurface() {
    if (auto it = std::find(g_surfaces.begin(), g_surfaces.end(), this); it != g_surfaces.end()) {
        *it = g_surfaces.back();
        g_surfaces.pop_back();
    }
    render::ui::DropPicture(this);
}

UiSurface::UiSurface(Interface3D* ui, int left, int top, int right, int bottom, SurfaceBlend blend, bool scale)
    : UiSurface(ui, Rect{left, top, right, bottom}, blend, scale) {}

// A new surface is black, as the original's was.
UiSurface::UiSurface(Interface3D* ownerUi, const Rect& area, SurfaceBlend newBlend, bool scale)
    : ui(ownerUi), blend(newBlend), rect(area), layoutRect(area), scaled(scale) {
    g_surfaces.push_back(this);
    if (scale) ui->ScaleRectToScreen(&rect);
    width = rect.Width();
    height = rect.Height();
    if (width <= 0 || height <= 0) ThrowSurfaceNotMade(width, height);
    Fill(0);
}

UiSurface::UiSurface(Interface3D* ui, const Rect* rect, SurfaceBlend blend)
    : UiSurface(ui, *rect, blend, true) {}

void UiSurface::SetRect(int left, int top, int right, int bottom) {
    rect = {left, top, right, bottom};
    layoutRect = rect;
    scaled = true;
    ui->ScaleRectToScreen(&rect);
}

void UiSurface::MoveTo(Point at) {
    if (scaled) layoutRect.Offset(at - layoutRect.TopLeft());
    Point point = at;
    ui->ScalePointToScreen(&point);
    rect.Offset(point - rect.TopLeft());
}

// A surface over a layout rectangle takes it scaled as the layout scales now; one over the whole screen (the
// backdrop) the whole new screen. Its size: the new rectangle's when it had the old one's, else (the global map's
// half-size pick picture) scaled as the window's 4:3 part. Its draws are drawn at the new size from the next frame.
void UiSurface::Relayout(const screen_layout::Layout& before) {
    const screen_layout::Layout& now = screen_layout::Get();
    const Rect old = rect;
    if (scaled) {
        rect = layoutRect;
        ui->ScaleRectToScreen(&rect);
    } else if (rect == Rect{0, 0, before.width, before.height}) {
        rect = Rect{0, 0, now.width, now.height};
        layoutRect = rect;
    }
    int newWidth = rect.Width(), newHeight = rect.Height();
    if (width != old.Width() || height != old.Height()) {
        newWidth = static_cast<int>(static_cast<long long>(width) * now.width43 / before.width43);
        newHeight = static_cast<int>(static_cast<long long>(height) * now.height43 / before.height43);
    }
    width = std::max(newWidth, 1);
    height = std::max(newHeight, 1);
}

void UiSurface::RelayoutAll(const screen_layout::Layout& before) {
    for (UiSurface* surface : g_surfaces) surface->Relayout(before);
}

void UiSurface::Show() { shown = true; }

void UiSurface::Hide(bool) { shown = false; }

// --- Draws -------------------------------------------------------------------------------------------

// Whether a fill of `colour` leaves the surface's pixels out (keyed on that colour).
bool UiSurface::Transparent(std::uint32_t colour) const { return blend == SurfaceBlend::Keyed && colour == background; }

void UiSurface::Keep(UiSurfaceDraw&& draw) {
    draws.push_back(std::move(draw));
    if (draws.size() == kManyDraws) ei::Log("ui: a surface keeps %zu draws (drawn without being cleared)", draws.size());
}

// A fill covers what was drawn under it: the draws inside it go (all of them for the whole surface, the frame
// picture too).
void UiSurface::Cover(const Rect& area, bool whole) {
    if (whole) {
        draws.clear();
        render::ui::DropPicture(this);
        greyed = false;
        return;
    }
    const Size size{width, height};
    std::erase_if(draws, [&](const UiSurfaceDraw& d) {
        Rect bounds;
        if (d.whole) bounds = Rect{0, 0, width, height};
        else if (d.kind == UiSurfaceDraw::Kind::Text) bounds = d.laidRect.Inset(0, 0, -1, -1);
        else bounds = ScaledArea(d.area, d.at, size);
        bounds = bounds.Intersection(Rect{0, 0, width, height});
        return bounds.IsEmpty() || area.Intersection(bounds) == bounds;
    });
}

void UiSurface::AddFill(const Rect& area, bool whole, std::uint32_t colour) {
    const Rect r = whole ? Rect{0, 0, width, height} : area.Intersection(Rect{0, 0, width, height});
    if (r.IsEmpty()) return;
    if (paint) {
        if (painting && !Transparent(colour)) render::ui::Solid(r.Offsetted(rect.TopLeft()), colour, paintClip);
        return;
    }
    Cover(r, whole);
    if (Transparent(colour)) return;
    UiSurfaceDraw d;
    d.kind = UiSurfaceDraw::Kind::Fill;
    d.whole = whole;
    d.area = r;
    d.at = {width, height};
    d.colour = colour;
    Keep(std::move(d));
}

void UiSurface::FrameRect(const Rect* area, std::uint32_t colour) {
    UiSurfaceDraw d;
    d.kind = UiSurfaceDraw::Kind::Frame;
    if (area->IsEmpty()) {
        d.whole = true;
    } else {
        d.area = *area;
        ui->ScaleRectToScreenNoMargin(&d.area);   // the surface's own pixels: no window margin
    }
    d.at = {width, height};
    d.colour = colour;
    if (paint) {
        if (!painting || Transparent(colour)) return;
        const Rect r = d.whole ? Rect{0, 0, width, height} : d.area;
        for (const Rect& side : render::ui::FrameRects(r.Offsetted(rect.TopLeft()))) render::ui::Solid(side, colour, paintClip);
        return;
    }
    Keep(std::move(d));
}

void UiSurface::Fill(std::uint32_t colour) { AddFill({}, true, colour); }

void UiSurface::Clear() { Fill(background); }

void UiSurface::FillRect(const Rect& area, std::uint32_t colour) { AddFill(area, false, colour); }

void UiSurface::Line(Point from, Point to, int lineWidth, std::uint32_t colour) {
    for (const Rect& r : render::ui::LineRects(from, to, lineWidth)) AddFill(r, false, colour);
}

void UiSurface::Dot(Point at, std::uint32_t colour) { AddFill(Rect::Sized(at, {1, 1}), false, colour); }

// --- Text --------------------------------------------------------------------------------------------

// Text on the interface's surfaces in our own fonts (ui_font.h): laid out by DrawText's rules (render_text.h's
// Layout). Rebirth's WideScreen patches the rectangle (scaled without the margin), which DrawTextIn reproduces for
// every window (screen_layout.h). See docs/notes/ui-native.md, "Fonts and metrics".

int UiSurface::DrawTextIn(std::string_view text, const Rect* area, int font, std::uint32_t colour, std::uint32_t format, bool shadow) {
    UiSurfaceDraw d;
    d.layoutUnits = true;
    d.whole = area->IsEmpty();
    d.area = *area;
    d.font = font;
    // The original scales a point whose x is an unwritten local; only y is used.
    Point extent{0, AddText(std::move(d), text, format, colour, shadow)};
    ui->ScalePointFromScreenNoMargin(&extent);
    return extent.y;
}

int UiSurface::DrawTextPixels(const UiFont& font, std::string_view text, const Rect& area, std::uint32_t format, std::uint32_t colour,
                               bool shadow) {
    UiSurfaceDraw d;
    d.area = area;
    d.pixelFont = font;
    return AddText(std::move(d), text, format, colour, shadow);
}

int UiSurface::AddText(UiSurfaceDraw&& draw, std::string_view text, std::uint32_t format, std::uint32_t colour, bool shadow) {
    format &= ~kModifyString;
    if (format & ~render::text::kSupportedFormats) {
        static bool logged = false;
        if (!logged) ei::Log("ui: DrawText format %#x has bits the layout does not follow; laid out without them", format);
        logged = true;
        format &= render::text::kSupportedFormats;
    }
    draw.kind = UiSurfaceDraw::Kind::Text;
    draw.at = {width, height};
    draw.text = ::text::CodePoints(text);
    draw.format = format;
    draw.colour = colour;
    draw.shadow = shadow;
    if (!LayOut(draw)) return 0;
    const int result = draw.layout.result;
    if (format & render::text::kCalcRect) return result;
    if (paint) {
        if (painting) DrawLaidText(draw, paintClip);
        return result;
    }
    if (!(draw.layout.quads.empty() && draw.layout.underlines.empty())) Keep(std::move(draw));
    return result;
}

// The draw's text laid out again when its font or pixel rectangle changed; false without a font.
bool UiSurface::LayOut(UiSurfaceDraw& draw) {
    render::text::TextFont* font = TextFontOf(draw.layoutUnits ? ui->fonts[draw.font] : draw.pixelFont);
    if (!font) return false;
    Rect frame;
    if (draw.whole) {
        frame = {0, 0, rect.Width(), rect.Height()};
    } else if (draw.layoutUnits) {
        frame = draw.area;
        // WideScreen (addon replaces the interface argument by 0): without the margin.
        ui->ScaleRectToScreenNoMargin(&frame);
        frame.right += kRectSlack;
        frame.bottom += kRectSlack;
    } else {
        frame = ScaledArea(draw.area, draw.at, {width, height});
    }
    if (font == draw.laidFont && frame == draw.laidRect) return true;
    draw.layout = {};
    if (!render::text::Layout(*font, draw.text, frame, draw.format, draw.layout)) return false;
    draw.laidFont = font;
    draw.laidRect = frame;
    return true;
}

// A laid-out text into the frame at the surface's place: its shadow first; nothing in the key colour.
void UiSurface::DrawLaidText(const UiSurfaceDraw& draw, const Rect& clip) const {
    if (Transparent(draw.colour)) return;
    const Point origin = rect.TopLeft();
    if (draw.shadow) render::ui::Glyphs(*draw.laidFont, draw.layout, origin + Point{1, 1}, kShadowColour, clip);
    render::ui::Glyphs(*draw.laidFont, draw.layout, origin, draw.colour, clip);
}

// --- Blit --------------------------------------------------------------------------------------------

void UiSurface::PaintWith(std::function<void(UiSurface&)> newPaint) {
    paint = std::move(newPaint);
    draws.clear();
}

void PaintLabels(UiSurface& surface, const std::vector<UiLabel>& labels) {
    for (const UiLabel& label : labels) {
        if (label.maxLines > 0)
            surface.DrawWrappedText(label.text, &label.area, label.maxLines, label.font, label.colour, label.format, true);
        else
            surface.DrawTextIn(label.text, &label.area, label.font, label.colour, label.format, true);
    }
}

// A shown surface's draw into the frame: its frame picture (stretched over its rectangle, greyed after Greyscale),
// then its window's paint or its kept draws at its rectangle, clipped to it and to the screen; a keyed surface
// leaves out what is in its background colour (a text's antialiased edges blend over the frame). See docs/notes/ui.md,
// "Surface blit".
void UiSurface::Blit() {
    if (!shown) return;
    const Rect clip = Rect{0, 0, ui->width, ui->height}.Intersection(rect);
    if (clip.IsEmpty()) return;
    if (render::ui::HasPicture(this)) render::ui::DrawPicture(this, rect, greyed);
    if (paint) {
        paintClip = clip;
        painting = true;
        paint(*this);
        painting = false;
        return;
    }
    const Point origin = rect.TopLeft();
    const Size size{width, height};
    for (UiSurfaceDraw& d : draws) {
        if (Transparent(d.colour)) continue;
        if (d.kind == UiSurfaceDraw::Kind::Text) {
            if (LayOut(d)) DrawLaidText(d, clip);
            continue;
        }
        const Rect r = d.whole ? Rect{0, 0, width, height} : ScaledArea(d.area, d.at, size);
        if (d.kind == UiSurfaceDraw::Kind::Fill) {
            render::ui::Solid(r.Offsetted(origin), d.colour, clip);
            continue;
        }
        for (const Rect& side : render::ui::FrameRects(r.Offsetted(origin))) render::ui::Solid(side, d.colour, clip);
    }
}

// --- Pictures ----------------------------------------------------------------------------------------

// The screen copied into a surface (the escape menu's backdrop, the save's picture):
// a frame picture of the surface's size, kept on the GPU (render_ui.h), which covers what the surface held.
void UiSurface::CaptureScreen() {
    Cover({}, true);
    render::ui::CapturePicture(this, rect, width, height);
}

// The picture stretched to the wanted size as 5:6:5 pixels; black without a picture.
std::vector<std::uint16_t> UiSurface::Rgb565Copy(int copyWidth, int copyHeight) {
    std::vector<std::uint16_t> pixels(static_cast<std::size_t>(static_cast<std::uint32_t>(copyWidth) * static_cast<std::uint32_t>(copyHeight)));
    render::ui::ReadPictureRgb565(this, copyWidth, copyHeight, pixels.data());
    return pixels;
}

void UiSurface::Greyscale() { greyed = true; }
