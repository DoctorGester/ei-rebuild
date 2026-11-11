// The interface layout for any window shape (Rebirth's WideScreen folded in for every aspect ratio): the window's
// 4:3 part, the layout widths and the scaling. See screen_layout.h.
#include "game/ui/screen_layout.h"

#include "game/ui/ui.h"
#include "game/ui/ui_mesh.h"

#include "engine/core/engine_globals.h"
#include "engine/scene/scene_renderer.h"

#include "platform/log.h"
#include "lib/rect.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace screen_layout {
namespace {

constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;

// The values for a window of width x height pixels (addon's apply function 10023AFC for a window wider than 4:3; it
// refused every other shape, which the original's stretched layout then drew).
Layout Compute(int width, int height) {
    Layout layout{};
    layout.width = width;
    layout.height = height;
    layout.width43 = std::min(width, height * kLayoutWidth / kLayoutHeight);
    layout.height43 = std::min(height, width * kLayoutHeight / kLayoutWidth);
    layout.margin = (width - layout.width43) / 2;
    layout.marginY = (height - layout.height43) / 2;
    const float units = layout.margin * 800.0f / layout.width43;
    layout.extra = static_cast<int>(std::ceil(units));
    // width * 0.0075 / height43 (addon's 10024902), written as 0.01 times the window's width over its 4:3 width so a
    // 4:3 window gets 0.01 exactly.
    layout.fontScale = 0.01f * (static_cast<float>(width) * 0.75f / static_cast<float>(layout.height43));
    return layout;
}

Layout g_layout = Compute(kLayoutWidth, kLayoutHeight);   // an 800x600 window's until Configure

// Division of the 64-bit product, as addon's multiply and divide do (no 32-bit wrap).
int MulDiv64(int a, int b, int divisor) {
    return static_cast<int>(static_cast<std::int64_t>(a) * b / divisor);
}

constexpr int kHeroListMode = 9;

void SetCurtainLook(UiMesh* curtain, int visible) {
    curtain->tint[0] = curtain->tint[1] = curtain->tint[2] = 0.3f;
    curtain->visible = visible;
}

} // namespace

float g_layoutWidth = /*00742B08*/ 800.0f;
float g_panelRight = /*007428AC*/ 695.0f;

// The layout for the window the game makes at the resolution it loaded from its options, and the two widths
// (addon: extra + 800.0 / 695.0 summed as doubles, stored as floats).
void Configure(int width, int height) {
    g_layout = Compute(width, height);
    // width43 / width (addon's 10024954) and height43 / height.
    g_sceneRenderer.axisScale = g_layout.width43 / static_cast<float>(width);
    g_sceneRenderer.axisScaleY = g_layout.height43 / static_cast<float>(height);
    g_layoutWidth = g_layout.extra + 800.0f;
    g_panelRight = g_layout.extra + 695.0f;
    ei::Log("Interface layout: %dx%d, 4:3 part %dx%d, margins %d, %d pixels (%d layout units); layout width %g, panel "
            "right %g",
            g_layout.width, g_layout.height, g_layout.width43, g_layout.height43, g_layout.margin, g_layout.marginY,
            g_layout.extra, g_layoutWidth, g_panelRight);
}

const Layout& Get() { return g_layout; }

int EdgeShift() { return Get().extra; }

// Layout to screen: x by the 4:3 part's width plus its left margin (with `margin`), y by its height plus its top
// margin (addon's code for x).
namespace {

Point PointToScreen(Point point, bool margin) {
    const Layout& l = Get();
    return {MulDiv64(l.width43, point.x, kLayoutWidth) + (margin ? l.margin : 0),
            MulDiv64(l.height43, point.y, kLayoutHeight) + (margin ? l.marginY : 0)};
}

Point PointFromScreen(Point point, bool margin) {
    const Layout& l = Get();
    return {MulDiv64(point.x - (margin ? l.margin : 0), kLayoutWidth, l.width43),
            MulDiv64(point.y - (margin ? l.marginY : 0), kLayoutHeight, l.height43)};
}

} // namespace

Point ToScreen(Point point) { return PointToScreen(point, true); }
Point ToScreenNoMargin(Point point) { return PointToScreen(point, false); }
Point FromScreen(Point point) { return PointFromScreen(point, true); }
Point FromScreenNoMargin(Point point) { return PointFromScreen(point, false); }

Rect ToScreen(const Rect& rect) { return Rect::FromCorners(ToScreen(rect.TopLeft()), ToScreen(rect.BottomRight())); }
Rect ToScreenNoMargin(const Rect& rect) {
    return Rect::FromCorners(ToScreenNoMargin(rect.TopLeft()), ToScreenNoMargin(rect.BottomRight()));
}
Rect FromScreen(const Rect& rect) {
    return Rect::FromCorners(FromScreen(rect.TopLeft()), FromScreen(rect.BottomRight()));
}

Size TextExtentToLayout(Size pixels) {
    const Layout& l = Get();
    return Size{MulDiv64(pixels.w, kLayoutWidth, l.width43), MulDiv64(pixels.h, kLayoutHeight, l.height43)};
}

float FontScale() { return Get().fontScale; }

namespace {

// addon's 100234AB / 100235BA (the right edge's margin + 800.0 summed as a double).
void ShapeCurtains(UiMesh* left, UiMesh* right) {
    const int margin = Get().margin;
    left->SetRectangle(2.0f, kSpriteScale, static_cast<float>(-margin), 0.0f, 0.0f, 600.0f, 0xE3, 0x3E, 0x100, 0xB0);
    right->SetRectangle(2.0f, kSpriteScale, 800.0f, 0.0f, margin + 800.0f, 600.0f, 0x100, 0xB0, 0xE3, 0x3E);
}

} // namespace

Curtains AddCurtains(UiWindow* window, int visible) {
    const Curtains curtains{window->AddBlankSprite(), window->AddBlankSprite()};
    ShapeCurtains(curtains.left, curtains.right);
    SetCurtainLook(curtains.left, visible);
    SetCurtainLook(curtains.right, visible);
    return curtains;
}

void PlaceCurtains(const Curtains& curtains) {
    if (!curtains.left) return;
    const int visible = curtains.left->visible;
    ShapeCurtains(curtains.left, curtains.right);
    SetCurtainLook(curtains.left, visible);
    SetCurtainLook(curtains.right, visible);
}

void ShowHeroCurtains(const Curtains& curtains, int mode) {
    if (!curtains.left) return;
    const int shown = mode != kHeroListMode ? 1 : 0;
    curtains.left->visible = shown;
    curtains.right->visible = shown;
}

// addon's apply function: 0x320 + extra written at one operand, -0x320 - extra at the other.
int LayoutRight() { return kLayoutWidth + Get().extra; }

} // namespace screen_layout
