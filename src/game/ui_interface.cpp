// CInterface3D coordinate scaling and small accessors, surface placement, model previews and the
// spell book's record lookups. See ui.h.
#include "ui.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "ui_widescreen.h"

#include <cstring>

namespace {
// The layout every interface screen is designed for.
constexpr int kLayoutWidth = 800;
constexpr int kLayoutHeight = 600;

// 32-bit wrapping multiply (IMUL r32, r/m32).
int Mul(int a, int b) {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// What EAX holds after the compiler's division by 600: MOV EAX,0x1B4E81B5; IMUL r32 leaves the
// low dword of the product in EAX.
int DivideBy600Residue(int dividend) { return Mul(0x1B4E81B5, dividend); }
} // namespace

// --- CInterface3D ----------------------------------------------------------------------------

// The same three with the interface (ECX) given: none for Rebirth's WideScreen sites (ui.h).
namespace {
const CInterface3D* WithoutMargin(const CInterface3D* ui) { return widescreen::Active() ? nullptr : ui; }
} // namespace

int CInterface3D::CallScaleRectToScreenNoMargin(RECT* rect) const {
    return WithoutMargin(this)->ScaleRectToScreenEntry(rect);
}
int CInterface3D::CallScalePointToScreenNoMargin(POINT* point) const {
    return WithoutMargin(this)->ScalePointToScreenEntry(point);
}
int CInterface3D::CallScalePointFromScreenNoMargin(POINT* point) const {
    return WithoutMargin(this)->ScalePointFromScreenEntry(point);
}

// The scaling functions below are Rebirth's WideScreen too (ui_widescreen.h): with the flag on,
// addon's versions. addon replaces the first two with JMPs to its code and writes the last three's
// code over (with two entries of its own, 005F7D9E and 005F7DFE, that clear ECX first and skip
// the margin; its patched mouse handlers 005EB210 and 00630010 call the second). Every one of them
// skips the margin when called with ECX 0 (addon's code at 005FDE94 calls 005F7CA0 so): the
// entries the hooks use, below, handle that.

int CInterface3D::ScaleRectToScreen(RECT* rect) const {
    if (widescreen::Active()) return widescreen::RectToScreen(rect, true);
    rect->left = Mul(rect->left, width_) / kLayoutWidth;
    rect->top = Mul(height_, rect->top) / kLayoutHeight;
    rect->right = Mul(rect->right, width_) / kLayoutWidth;
    int bottom = Mul(rect->bottom, height_);
    rect->bottom = bottom / kLayoutHeight;
    return DivideBy600Residue(bottom);
}

// 005F7D60 is an identical copy of this function that nothing calls; addon points it at the same code
// as this one. Not rewritten.
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

// The hooks: with Rebirth's code in place these are also reached with ECX 0, i.e. without an
// interface (addon's entries 005F7D9E / 005F7DFE, its code at 005FDE94; ui_widescreen.h).
int CInterface3D::ScaleRectToScreenEntry(RECT* rect) const {
    if (!this && widescreen::Active()) return widescreen::RectToScreen(rect, false);
    return ScaleRectToScreen(rect);
}
EI_REPLACE_METHOD(0x005F7CA0, CInterface3D::ScaleRectToScreenEntry);

int CInterface3D::ScalePointToScreenEntry(POINT* point) const {
    if (!this && widescreen::Active()) return widescreen::PointToScreen(point, false);
    return ScalePointToScreen(point);
}
EI_REPLACE_METHOD(0x005F7D20, CInterface3D::ScalePointToScreenEntry);

int CInterface3D::ScalePointFromScreenCopyEntry(POINT* point) const {
    if (!this && widescreen::Active()) return widescreen::PointFromScreen(point, false);
    return ScalePointFromScreenCopy(point);
}
EI_REPLACE_METHOD(0x005F7E30, CInterface3D::ScalePointFromScreenCopyEntry);

int CInterface3D::ScaleRectFromScreenEntry(RECT* rect) const {
    if (!this && widescreen::Active()) return widescreen::RectFromScreen(rect, false);
    return ScaleRectFromScreen(rect);
}
EI_REPLACE_METHOD(0x005F7DA0, CInterface3D::ScaleRectFromScreenEntry);

int CInterface3D::ScalePointFromScreenEntry(POINT* point) const {
    if (!this && widescreen::Active()) return widescreen::PointFromScreen(point, false);
    return ScalePointFromScreen(point);
}
EI_REPLACE_METHOD(0x005F7E00, CInterface3D::ScalePointFromScreenEntry);

CUiWindow* CInterface3D::GetActiveWindow() const {
    return stackBegin_ != stackEnd_ ? stackEnd_[-1] : nullptr;
}
EI_REPLACE_METHOD(0x005F7590, CInterface3D::GetActiveWindow);

void CInterface3D::SetPendingValue(std::uint32_t value) {
    pendingFlag_ = true;
    pendingValue_ = value;
}
EI_REWRITTEN(0x005F74E0, CInterface3D::SetPendingValue);

// --- CUiSurface ------------------------------------------------------------------------------

void CUiSurface::SetRect(int left, int top, int right, int bottom) {
    rect_.left = left;
    rect_.top = top;
    rect_.right = right;
    rect_.bottom = bottom;
    interface_->ScaleRectToScreenEntry(&rect_);
}
EI_REWRITTEN(0x005FD3A0, CUiSurface::SetRect);

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
EI_REWRITTEN(0x005FE400, CUiSurface::MoveTo);

// --- CUiModel --------------------------------------------------------------------------------

void CUiModel::SetScreenPosition(float x, float y, float depth) {
    // 0074015C (1/400), 0073B84C (1), 0073C524 (0.75), 00740ED4: layout coordinates to the view
    // plane, (x / 400 - 1, y / 400 - 0.75) * k * depth. Each product is rounded once to float.
    constexpr float kLayoutScale = 0x1.47ae14p-9f;
    constexpr float kViewScale = 0x1.ed21e6p-2f;
    float px = (x * kLayoutScale - 1.0f) * kViewScale * depth;
    float py = (y * kLayoutScale - 0.75f) * kViewScale * depth;
    Vector3& position = object_->localPosition_;
    float value = px;
    std::memcpy(&position.x, &value, sizeof(float));
    value = py;
    std::memcpy(&position.y, &value, sizeof(float));
    std::memcpy(&position.z, &depth, sizeof(float));
}
EI_REWRITTEN(0x005FC450, CUiModel::SetScreenPosition);

// --- Record lookups --------------------------------------------------------------------------

// 005F2EA0 / 005F2EE0: the spell and modifier tables' At (spell_db_lookup.cpp).
