// The interface layout for any window shape (Rebirth's WideScreen folded in for every aspect ratio): the window's
// 4:3 part, the layout widths and the scaling. See screen_layout.h.
#include "screen_layout.h"

#include "ui.h"

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
    // width43 / width (addon's 10024954) and width * 0.0075 / height43 (10024902), written as 0.01 times the
    // window's width over its 4:3 width so a 4:3 window gets 0.01 exactly.
    layout.axisScale = layout.width43 / static_cast<float>(width);
    layout.axisScaleY = layout.height43 / static_cast<float>(height);
    layout.fontScale = 0.01f * (static_cast<float>(width) * 0.75f / static_cast<float>(layout.height43));
    return layout;
}

Layout g_layout = Compute(kLayoutWidth, kLayoutHeight);   // an 800x600 window's until Configure

int g_panelOffset;                 // ours (addon's 100BD0A0)

// Division of the 64-bit product, as addon's multiply and divide do (no 32-bit wrap).
int MulDiv64(int a, int b, int divisor) {
    return static_cast<int>(static_cast<std::int64_t>(a) * b / divisor);
}

constexpr int kHeroScreenSprites = 21;   // the hero screen's 19 and the two curtains
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
    g_layoutWidth = g_layout.extra + 800.0f;
    g_panelRight = g_layout.extra + 695.0f;
    ei::Log("Interface layout: %dx%d, 4:3 part %dx%d, margins %d, %d pixels (%d layout units); layout width %g, panel "
            "right %g",
            g_layout.width, g_layout.height, g_layout.width43, g_layout.height43, g_layout.margin, g_layout.marginY,
            g_layout.extra, g_layoutWidth, g_panelRight);
}

const Layout& Get() { return g_layout; }

int PanelOffset() { return g_panelOffset; }
void SetPanelOffset(int offset) { g_panelOffset = offset; }

int EdgeShift() { return Get().extra; }

// addon's entry handlers (load the extra into the offset; one negates it first) and exit handlers (zero the
// offset).
void EnterLeftPanel() { SetPanelOffset(-Get().extra); }
void EnterRightPanel() { SetPanelOffset(Get().extra); }
void LeavePanel() { SetPanelOffset(0); }

// Layout to screen: x by the 4:3 part's width plus its left margin (with `margin`), y by its height plus its top
// margin (addon's code for x).
void PointToScreen(Point* point, bool margin) {
    const Layout& l = Get();
    point->x = MulDiv64(l.width43, point->x, kLayoutWidth) + (margin ? l.margin : 0);
    point->y = MulDiv64(l.height43, point->y, kLayoutHeight) + (margin ? l.marginY : 0);
}

void RectToScreen(Rect* rect, bool margin) {
    Point topLeft = rect->TopLeft(), bottomRight = rect->BottomRight();
    PointToScreen(&topLeft, margin);
    PointToScreen(&bottomRight, margin);
    *rect = Rect::FromCorners(topLeft, bottomRight);
}

// Screen to layout.
void PointFromScreen(Point* point, bool margin) {
    const Layout& l = Get();
    point->x = MulDiv64(point->x - (margin ? l.margin : 0), kLayoutWidth, l.width43);
    point->y = MulDiv64(point->y - (margin ? l.marginY : 0), kLayoutHeight, l.height43);
}

void RectFromScreen(Rect* rect, bool margin) {
    Point topLeft = rect->TopLeft(), bottomRight = rect->BottomRight();
    PointFromScreen(&topLeft, margin);
    PointFromScreen(&bottomRight, margin);
    *rect = Rect::FromCorners(topLeft, bottomRight);
}

Size TextExtentToLayout(Size pixels) {
    const Layout& l = Get();
    return Size{MulDiv64(pixels.w, kLayoutWidth, l.width43), MulDiv64(pixels.h, kLayoutHeight, l.height43)};
}

float AxisScale() { return Get().axisScale; }
float AxisScaleY() { return Get().axisScaleY; }

float FontScale() { return Get().fontScale; }

Point PickShift() {
    const Layout& l = Get();
    return Point{l.margin / 2, l.marginY / 2};
}

namespace {

// addon's 100234AB / 100235BA (the right edge's margin + 800.0 summed as a double).
void ShapeCurtains(UiMesh* left, UiMesh* right) {
    const int margin = Get().margin;
    left->SetRectangle(2.0f, 100.0f, static_cast<float>(-margin), 0.0f, 0.0f, 600.0f, 0xE3, 0x3E, 0x100, 0xB0);
    right->SetRectangle(2.0f, 100.0f, 800.0f, 0.0f, margin + 800.0f, 600.0f, 0x100, 0xB0, 0xE3, 0x3E);
}

} // namespace

int AddCurtains(UiWindow* window, int visible) {
    const int first = static_cast<int>(std::ssize(window->sceneObjects));
    UiMesh* left = window->AddBlankSprite();
    UiMesh* right = window->AddBlankSprite();
    ShapeCurtains(left, right);
    SetCurtainLook(left, visible);
    SetCurtainLook(right, visible);
    return first;
}

void PlaceCurtains(UiWindow* window, int first) {
    if (first < 0 || first + 1 >= std::ssize(window->sceneObjects)) return;
    UiMesh* left = window->SceneMesh(first);
    UiMesh* right = window->SceneMesh(first + 1);
    const int visible = left->visible;
    ShapeCurtains(left, right);
    SetCurtainLook(left, visible);
    SetCurtainLook(right, visible);
}

void ShowHeroCurtains(UiWindow* window, int mode) {
    if (std::ssize(window->sceneObjects) != kHeroScreenSprites) return;
    const int shown = mode != kHeroListMode ? 1 : 0;
    window->SceneMesh(kHeroCurtains)->visible = shown;
    window->SceneMesh(kHeroCurtains + 1)->visible = shown;
}

// addon's apply function: 0x320 + extra written at one operand, -0x320 - extra at the other.
int LayoutRight() { return kLayoutWidth + Get().extra; }

} // namespace screen_layout
