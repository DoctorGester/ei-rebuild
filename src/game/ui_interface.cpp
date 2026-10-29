// CInterface3D coordinate scaling and small accessors, surface placement, model previews and the
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

const CInterface3D* WithoutMargin(const CInterface3D* ui) { return widescreen::Active() ? nullptr : ui; }

} // namespace

// --- CInterface3D ----------------------------------------------------------------------------

// The same three with the interface given: none for Rebirth's WideScreen sites (ui.h).

void CInterface3D::CallScaleRectToScreenNoMargin(RECT* rect) const { WithoutMargin(this)->ScaleRectToScreenEntry(rect); }
void CInterface3D::CallScalePointToScreenNoMargin(POINT* point) const {
    WithoutMargin(this)->ScalePointToScreenEntry(point);
}
void CInterface3D::CallScalePointFromScreenNoMargin(POINT* point) const {
    WithoutMargin(this)->ScalePointFromScreenEntry(point);
}

// The scaling functions below are Rebirth's WideScreen too (ui_widescreen.h): with the flag on, addon's
// versions, which skip the margin when called without an interface (addon's code calls them so; the entries
// below handle that).

void CInterface3D::ScaleRectToScreen(RECT* rect) const {
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
void CInterface3D::ScalePointToScreen(POINT* point) const {
    if (widescreen::Active()) {
        widescreen::PointToScreen(point, true);
        return;
    }
    point->x = Mul(point->x, width) / kLayoutWidth;
    point->y = Mul(height, point->y) / kLayoutHeight;
}

void CInterface3D::ScaleRectFromScreen(RECT* rect) const {
    if (widescreen::Active()) {
        widescreen::RectFromScreen(rect, true);
        return;
    }
    rect->left = Mul(rect->left, kLayoutWidth) / width;
    rect->top = Mul(rect->top, kLayoutHeight) / height;
    rect->right = Mul(rect->right, kLayoutWidth) / width;
    rect->bottom = Mul(rect->bottom, kLayoutHeight) / height;
}

void CInterface3D::ScalePointFromScreen(POINT* point) const {
    if (widescreen::Active()) {
        widescreen::PointFromScreen(point, true);
        return;
    }
    point->x = Mul(point->x, kLayoutWidth) / width;
    point->y = Mul(point->y, kLayoutHeight) / height;
}

void CInterface3D::ScalePointFromScreenCopy(POINT* point) const { ScalePointFromScreen(point); }

// With Rebirth's code in place these are also reached without an interface (addon's entries;
// ui_widescreen.h).
void CInterface3D::ScaleRectToScreenEntry(RECT* rect) const {
    if (!this && widescreen::Active())
        widescreen::RectToScreen(rect, false);
    else
        ScaleRectToScreen(rect);
}

void CInterface3D::ScalePointToScreenEntry(POINT* point) const {
    if (!this && widescreen::Active())
        widescreen::PointToScreen(point, false);
    else
        ScalePointToScreen(point);
}

void CInterface3D::ScalePointFromScreenCopyEntry(POINT* point) const { ScalePointFromScreenEntry(point); }

void CInterface3D::ScaleRectFromScreenEntry(RECT* rect) const {
    if (!this && widescreen::Active())
        widescreen::RectFromScreen(rect, false);
    else
        ScaleRectFromScreen(rect);
}

void CInterface3D::ScalePointFromScreenEntry(POINT* point) const {
    if (!this && widescreen::Active())
        widescreen::PointFromScreen(point, false);
    else
        ScalePointFromScreen(point);
}

CUiWindow* CInterface3D::GetActiveWindow() const {
    return stack.begin() != stack.end() ? stack.end()[-1] : nullptr;
}

void CInterface3D::SetPendingValue(std::uint32_t value) {
    pendingFlag = true;
    pendingValue = value;
}

// --- CUiSurface ------------------------------------------------------------------------------

void CUiSurface::SetRect(int left, int top, int right, int bottom) {
    rect.left = left;
    rect.top = top;
    rect.right = right;
    rect.bottom = bottom;
    ui->ScaleRectToScreenEntry(&rect);
}

void CUiSurface::MoveTo(int x, int y) {
    POINT point{x, y};
    ui->ScalePointToScreenEntry(&point);
    int rectWidth = rect.right - rect.left;
    int rectHeight = rect.bottom - rect.top;
    rect.left = point.x;
    rect.top = point.y;
    rect.right = rectWidth + point.x;
    rect.bottom = rectHeight + point.y;
}

// --- CUiModel --------------------------------------------------------------------------------

void CUiModel::SetScreenPosition(float x, float y, float depth) {
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
