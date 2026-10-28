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

// The low dword the compiler's division by 600 (a multiply by a magic constant) leaves as the result.
int DivideBy600Residue(int dividend) { return Mul(0x1B4E81B5, dividend); }

const CInterface3D* WithoutMargin(const CInterface3D* ui) { return widescreen::Active() ? nullptr : ui; }

} // namespace

// --- CInterface3D ----------------------------------------------------------------------------

// The same three with the interface given: none for Rebirth's WideScreen sites (ui.h).

int CInterface3D::CallScaleRectToScreenNoMargin(RECT* rect) const {
    return WithoutMargin(this)->ScaleRectToScreenEntry(rect);
}
int CInterface3D::CallScalePointToScreenNoMargin(POINT* point) const {
    return WithoutMargin(this)->ScalePointToScreenEntry(point);
}
int CInterface3D::CallScalePointFromScreenNoMargin(POINT* point) const {
    return WithoutMargin(this)->ScalePointFromScreenEntry(point);
}

// The scaling functions below are Rebirth's WideScreen too (ui_widescreen.h): with the flag on, addon's
// versions, which skip the margin when called without an interface (addon's code calls them so; the entries
// below handle that).

int CInterface3D::ScaleRectToScreen(RECT* rect) const {
    if (widescreen::Active()) return widescreen::RectToScreen(rect, true);
    rect->left = Mul(rect->left, width_) / kLayoutWidth;
    rect->top = Mul(height_, rect->top) / kLayoutHeight;
    rect->right = Mul(rect->right, width_) / kLayoutWidth;
    int bottom = Mul(rect->bottom, height_);
    rect->bottom = bottom / kLayoutHeight;
    return DivideBy600Residue(bottom);
}

// An identical copy of this function exists in the game that nothing calls; addon points it at the same code
// as this one.
int CInterface3D::ScalePointToScreen(POINT* point) const {
    if (widescreen::Active()) return widescreen::PointToScreen(point, true);
    point->x = Mul(point->x, width_) / kLayoutWidth;
    int y = Mul(height_, point->y);
    point->y = y / kLayoutHeight;
    return DivideBy600Residue(y);
}

int CInterface3D::ScaleRectFromScreen(RECT* rect) const {
    if (widescreen::Active()) return widescreen::RectFromScreen(rect, true);
    rect->left = Mul(rect->left, kLayoutWidth) / width_;
    rect->top = Mul(rect->top, kLayoutHeight) / height_;
    rect->right = Mul(rect->right, kLayoutWidth) / width_;
    rect->bottom = Mul(rect->bottom, kLayoutHeight) / height_;
    return rect->bottom;
}

int CInterface3D::ScalePointFromScreen(POINT* point) const {
    if (widescreen::Active()) return widescreen::PointFromScreen(point, true);
    point->x = Mul(point->x, kLayoutWidth) / width_;
    point->y = Mul(point->y, kLayoutHeight) / height_;
    return point->y;
}

int CInterface3D::ScalePointFromScreenCopy(POINT* point) const {
    if (widescreen::Active()) return widescreen::PointFromScreen(point, true);
    point->x = Mul(point->x, kLayoutWidth) / width_;
    point->y = Mul(point->y, kLayoutHeight) / height_;
    return point->y;
}

// With Rebirth's code in place these are also reached without an interface (addon's entries;
// ui_widescreen.h).
int CInterface3D::ScaleRectToScreenEntry(RECT* rect) const {
    if (!this && widescreen::Active()) return widescreen::RectToScreen(rect, false);
    return ScaleRectToScreen(rect);
}

int CInterface3D::ScalePointToScreenEntry(POINT* point) const {
    if (!this && widescreen::Active()) return widescreen::PointToScreen(point, false);
    return ScalePointToScreen(point);
}

int CInterface3D::ScalePointFromScreenCopyEntry(POINT* point) const {
    if (!this && widescreen::Active()) return widescreen::PointFromScreen(point, false);
    return ScalePointFromScreenCopy(point);
}

int CInterface3D::ScaleRectFromScreenEntry(RECT* rect) const {
    if (!this && widescreen::Active()) return widescreen::RectFromScreen(rect, false);
    return ScaleRectFromScreen(rect);
}

int CInterface3D::ScalePointFromScreenEntry(POINT* point) const {
    if (!this && widescreen::Active()) return widescreen::PointFromScreen(point, false);
    return ScalePointFromScreen(point);
}

CUiWindow* CInterface3D::GetActiveWindow() const {
    return stack_.begin() != stack_.end() ? stack_.end()[-1] : nullptr;
}

void CInterface3D::SetPendingValue(std::uint32_t value) {
    pendingFlag_ = true;
    pendingValue_ = value;
}

// --- CUiSurface ------------------------------------------------------------------------------

void CUiSurface::SetRect(int left, int top, int right, int bottom) {
    rect_.left = left;
    rect_.top = top;
    rect_.right = right;
    rect_.bottom = bottom;
    interface_->ScaleRectToScreenEntry(&rect_);
}

void CUiSurface::MoveTo(int x, int y) {
    POINT point{x, y};
    interface_->ScalePointToScreenEntry(&point);
    int width = rect_.right - rect_.left;
    int height = rect_.bottom - rect_.top;
    rect_.left = point.x;
    rect_.top = point.y;
    rect_.right = width + point.x;
    rect_.bottom = height + point.y;
}

// --- CUiModel --------------------------------------------------------------------------------

void CUiModel::SetScreenPosition(float x, float y, float depth) {
    // (1/400), (1), (0.75): layout coordinates to the view
    // plane, (x / 400 - 1, y / 400 - 0.75) * k * depth. Each product is rounded once to float.
    constexpr float kLayoutScale = 0x1.47ae14p-9f;
    constexpr float kViewScale = 0x1.ed21e6p-2f;
    float px = (x * kLayoutScale - 1.0f) * kViewScale * depth;
    float py = (y * kLayoutScale - 0.75f) * kViewScale * depth;
    Vector3& position = object_->localPosition_;
    float value = px;
    position.x = value;
    value = py;
    position.y = value;
    position.z = depth;
}

// --- Record lookups --------------------------------------------------------------------------
