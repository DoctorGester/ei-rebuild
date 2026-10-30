// Interface3D coordinate scaling and small accessors, surface placement, model previews and the
// spell book's record lookups. See ui.h.
#include "ui.h"

#include "ui_widescreen.h"

#include <cstring>

namespace {
// The layout every interface screen is designed for.
constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;

// 32-bit wrapping multiply.
int Mul(int a, int b) {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

const Interface3D* WithoutMargin(const Interface3D* ui) { return widescreen::Active() ? nullptr : ui; }

} // namespace

// --- Interface3D ----------------------------------------------------------------------------

// The same three with the interface given: none for Rebirth's WideScreen sites (ui.h).

void Interface3D::CallScaleRectToScreenNoMargin(Rect* rect) const { WithoutMargin(this)->ScaleRectToScreenEntry(rect); }
void Interface3D::CallScalePointToScreenNoMargin(Point* point) const {
    WithoutMargin(this)->ScalePointToScreenEntry(point);
}
void Interface3D::CallScalePointFromScreenNoMargin(Point* point) const {
    WithoutMargin(this)->ScalePointFromScreenEntry(point);
}

// The scaling functions below are Rebirth's WideScreen too (ui_widescreen.h): with the flag on, addon's
// versions, which skip the margin when called without an interface (addon's code calls them so; the entries
// below handle that).

void Interface3D::ScaleRectToScreen(Rect* rect) const {
    if (widescreen::Active()) {
        widescreen::RectToScreen(rect, true);
        return;
    }
    rect->left = Mul(rect->left, width) / kLayoutWidth;
    rect->top = Mul(height, rect->top) / kLayoutHeight;
    rect->right = Mul(rect->right, width) / kLayoutWidth;
    rect->bottom = Mul(rect->bottom, height) / kLayoutHeight;
}

// An identical copy of this function exists in the game that nothing calls; addon points it at the same code
// as this one.
void Interface3D::ScalePointToScreen(Point* point) const {
    if (widescreen::Active()) {
        widescreen::PointToScreen(point, true);
        return;
    }
    point->x = Mul(point->x, width) / kLayoutWidth;
    point->y = Mul(height, point->y) / kLayoutHeight;
}

void Interface3D::ScaleRectFromScreen(Rect* rect) const {
    if (widescreen::Active()) {
        widescreen::RectFromScreen(rect, true);
        return;
    }
    rect->left = Mul(rect->left, kLayoutWidth) / width;
    rect->top = Mul(rect->top, kLayoutHeight) / height;
    rect->right = Mul(rect->right, kLayoutWidth) / width;
    rect->bottom = Mul(rect->bottom, kLayoutHeight) / height;
}

void Interface3D::ScalePointFromScreen(Point* point) const {
    if (widescreen::Active()) {
        widescreen::PointFromScreen(point, true);
        return;
    }
    point->x = Mul(point->x, kLayoutWidth) / width;
    point->y = Mul(point->y, kLayoutHeight) / height;
}

void Interface3D::ScalePointFromScreenCopy(Point* point) const { ScalePointFromScreen(point); }

// With Rebirth's code in place these are also reached without an interface (addon's entries;
// ui_widescreen.h).
void Interface3D::ScaleRectToScreenEntry(Rect* rect) const {
    if (!this && widescreen::Active())
        widescreen::RectToScreen(rect, false);
    else
        ScaleRectToScreen(rect);
}

void Interface3D::ScalePointToScreenEntry(Point* point) const {
    if (!this && widescreen::Active())
        widescreen::PointToScreen(point, false);
    else
        ScalePointToScreen(point);
}

void Interface3D::ScalePointFromScreenCopyEntry(Point* point) const { ScalePointFromScreenEntry(point); }

void Interface3D::ScaleRectFromScreenEntry(Rect* rect) const {
    if (!this && widescreen::Active())
        widescreen::RectFromScreen(rect, false);
    else
        ScaleRectFromScreen(rect);
}

void Interface3D::ScalePointFromScreenEntry(Point* point) const {
    if (!this && widescreen::Active())
        widescreen::PointFromScreen(point, false);
    else
        ScalePointFromScreen(point);
}

UiWindow* Interface3D::GetActiveWindow() const {
    return stack.begin() != stack.end() ? stack.end()[-1] : nullptr;
}

void Interface3D::SetPendingValue(std::uint32_t value) {
    pendingFlag = true;
    pendingValue = value;
}

// --- UiSurface ------------------------------------------------------------------------------

void UiSurface::SetRect(int left, int top, int right, int bottom) {
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    ui->ScaleRectToScreenEntry(&rect);
}

void UiSurface::MoveTo(int x, int y) {
    Point point{x, y};
    ui->ScalePointToScreenEntry(&point);
    int rectWidth = rect.right - rect.left;
    int rectHeight = rect.bottom - rect.top;
    rect.left = point.x;
    rect.top = point.y;
    rect.right = rectWidth + point.x;
    rect.bottom = rectHeight + point.y;
}

// --- UiModel --------------------------------------------------------------------------------

void UiModel::SetScreenPosition(float x, float y, float depth) {
    // (1/400), (1), (0.75): layout coordinates to the view
    // plane, (x / 400 - 1, y / 400 - 0.75) * k * depth. Each product is rounded once to float.
    constexpr float kLayoutScale = 0x1.47ae14p-9f;
    constexpr float kViewScale = 0x1.ed21e6p-2f;
    float px = (x * kLayoutScale - 1.0f) * kViewScale * depth;
    float py = (y * kLayoutScale - 0.75f) * kViewScale * depth;
    Vector3& position = object->localPosition;
    float value = px;
    position.x = value;
    value = py;
    position.y = value;
    position.z = depth;
}

// --- Record lookups --------------------------------------------------------------------------
