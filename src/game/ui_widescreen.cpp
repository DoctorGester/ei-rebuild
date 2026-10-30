// Rebirth's WideScreen patch: the layout addon.dll computes, the layout widths it writes and its
// scaling. See ui_widescreen.h.
#include "ui_widescreen.h"

#include "ui.h"

#include "ei/rebirth.h"
#include "hook/log.h"
#include "settings.h"
#include "lib/rect.h"

#include <cmath>
#include <cstdint>

namespace widescreen {
namespace {

constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;

// The starter's switch (addon reads it from the registry): [rebirth] WideScreen of the settings file, on when missing
// (addon's default).
bool WideScreenSetting() { return settings::GetBool(settings::kRebirth, "WideScreen", true); }

// addon's apply function (10023AFC): the checks that refuse a mode (logged as failed or disabled),
// then the values, for the mode the game runs in.
Layout Compute(int width, int height) {
    Layout layout{};
    if (!ei::rebirth::Enabled(ei::rebirth::Patch::WideScreen) || !WideScreenSetting()) return layout;
    layout.width = width;
    layout.height = height;
    const int w = layout.width, h = layout.height;
    if (static_cast<unsigned>(w - 640) > 0xFD7Fu || static_cast<unsigned>(h - 480) > 0xFE1Fu || w / h < 1) return layout;
    if (w < kLayoutWidth || h < kLayoutHeight || w * 3 <= h * 4) return layout;
    layout.width43 = h * kLayoutWidth / kLayoutHeight;
    layout.margin = (w - layout.width43) / 2;
    float units = layout.margin * 800.0f / layout.width43;
    layout.extra = static_cast<int>(std::ceil(units));
    // width43 / width (addon's 10024954) and width * 0.0075 / height (10024902; addon divides in double).
    layout.axisScale = layout.width43 / static_cast<float>(w);
    layout.fontScale = w * 0.0075f / h;
    layout.active = true;
    return layout;
}

Layout g_layout;   // inactive until Configure

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

// The layout for the resolution the game loaded from its options (addon reads VIDEORESOLUTION of its own key, the
// same value on Rebirth's data; on the original's, the game's own tree), and the two widths written as addon
// writes them.
void Configure(int width, int height) {
    g_layout = Compute(width, height);
    g_layoutWidth = 800.0f;
    g_panelRight = 695.0f;
    if (!g_layout.active) return;
    // addon: extra + 800.0 / 695.0 summed as doubles, stored as floats.
    g_layoutWidth = g_layout.extra + 800.0f;
    g_panelRight = g_layout.extra + 695.0f;
    ei::Log("WideScreen: %dx%d, 4:3 part %d, margin %d pixels = %d layout units; layout width %g, panel right %g",
            g_layout.width, g_layout.height, g_layout.width43, g_layout.margin, g_layout.extra,
            g_layoutWidth, g_panelRight);
}

const Layout& Get() { return g_layout; }

int PanelOffset() { return g_panelOffset; }
void SetPanelOffset(int offset) { g_panelOffset = offset; }

int EdgeShift() {
    const Layout& l = Get();
    return l.active ? l.extra : 0;
}

// addon's entry handlers (load the extra into the offset; one negates it first) and exit handlers (zero the
// offset).
void EnterLeftPanel() {
    if (Active()) SetPanelOffset(-Get().extra);
}
void EnterRightPanel() {
    if (Active()) SetPanelOffset(Get().extra);
}
void LeavePanel() {
    if (Active()) SetPanelOffset(0);
}

// addon's layout-to-screen scaling: x scaled by the 4:3 part's width plus the margin (with `margin`), y by
// the mode's height.
void RectToScreen(Rect* rect, bool margin) {
    const Layout& l = Get();
    const int shift = margin ? l.margin : 0;
    rect->left = MulDiv64(l.width43, rect->left, kLayoutWidth) + shift;
    rect->top = MulDiv64(l.height, rect->top, kLayoutHeight);
    rect->right = MulDiv64(l.width43, rect->right, kLayoutWidth) + shift;
    rect->bottom = MulDiv64(l.height, rect->bottom, kLayoutHeight);
}

// addon's scaling of points and rectangles.
void PointToScreen(Point* point, bool margin) {
    const Layout& l = Get();
    point->x = MulDiv64(l.width43, point->x, kLayoutWidth) + (margin ? l.margin : 0);
    point->y = MulDiv64(l.height, point->y, kLayoutHeight);
}

// addon's code for the screen-to-layout scaling: left, right, top, bottom in that order.
void RectFromScreen(Rect* rect, bool margin) {
    const Layout& l = Get();
    const int shift = margin ? l.margin : 0;
    rect->left = MulDiv64(rect->left - shift, kLayoutWidth, l.width43);
    rect->right = MulDiv64(rect->right - shift, kLayoutWidth, l.width43);
    rect->top = MulDiv64(rect->top, kLayoutHeight, l.height);
    rect->bottom = MulDiv64(rect->bottom, kLayoutHeight, l.height);
}

// addon's code for the screen-to-layout scaling of a point (two entries, one jumping to the other).
void PointFromScreen(Point* point, bool margin) {
    const Layout& l = Get();
    point->x = MulDiv64(point->x - (margin ? l.margin : 0), kLayoutWidth, l.width43);
    point->y = MulDiv64(kLayoutHeight, point->y, l.height);
}

// addon's text extent code.
Size TextExtentToLayout(Size pixels) {
    const Layout& l = Get();
    return Size{MulDiv64(pixels.cx, kLayoutWidth, l.width43), MulDiv64(pixels.cy, kLayoutHeight, l.height)};
}

float AxisScale() { return Active() ? Get().axisScale : 1.0f; }

float FontScale() { return Active() ? Get().fontScale : 0.01f; }

int PickShift() { return Active() ? Get().margin / 2 : 0; }

// addon's 100234AB / 100235BA (the right edge's margin + 800.0 summed as a double).
void AddCurtains(UiWindow* window, int visible) {
    if (!Active()) return;
    const int margin = Get().margin;
    UiMesh* left = window->AddBlankSprite();
    left->SetRectangle(2.0f, 100.0f, static_cast<float>(-margin), 0.0f, 0.0f, 600.0f, 0xE3, 0x3E, 0x100, 0xB0);
    SetCurtainLook(left, visible);
    UiMesh* right = window->AddBlankSprite();
    right->SetRectangle(2.0f, 100.0f, 800.0f, 0.0f, margin + 800.0f, 600.0f, 0x100, 0xB0, 0xE3, 0x3E);
    SetCurtainLook(right, visible);
}

void ShowHeroCurtains(UiWindow* window, int mode) {
    if (!Active() || std::ssize(window->sceneObjects) != kHeroScreenSprites) return;
    const int shown = mode != kHeroListMode ? 1 : 0;
    window->SceneMesh(kHeroScreenSprites - 2)->visible = shown;
    window->SceneMesh(kHeroScreenSprites - 1)->visible = shown;
}

// addon's apply function: 0x320 + extra written at one operand, -0x320 - extra at the other.
int LayoutRight() { return kLayoutWidth + Get().extra; }


} // namespace widescreen
