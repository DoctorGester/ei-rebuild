// CInterface3D coordinate scaling and small accessors, surface placement, model previews and the
// spell book's record lookups. See ui.h.
#include "ui.h"

#include "ei/hook.h"
#include "ei/rebirth.h"
#include "originals_ui.h"
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

__declspec(naked) int CInterface3D::CallScaleRectToScreen(RECT*) const EI_ORIGINAL(0x005F7CA0)
__declspec(naked) int CInterface3D::CallScalePointToScreen(POINT*) const EI_ORIGINAL(0x005F7D20)
__declspec(naked) int CInterface3D::CallScalePointFromScreen(POINT*) const EI_ORIGINAL(0x005F7E00)

// The same three with the interface (ECX) given: none for Rebirth's WideScreen sites (ui.h).
namespace {
__declspec(naked) int __fastcall ScaleRectToScreenAt(const CInterface3D*, void*, RECT*) EI_ORIGINAL(0x005F7CA0)
__declspec(naked) int __fastcall ScalePointToScreenAt(const CInterface3D*, void*, POINT*) EI_ORIGINAL(0x005F7D20)
__declspec(naked) int __fastcall ScalePointFromScreenAt(const CInterface3D*, void*, POINT*) EI_ORIGINAL(0x005F7E00)
const CInterface3D* WithoutMargin(const CInterface3D* ui) { return widescreen::Active() ? nullptr : ui; }
} // namespace

int CInterface3D::CallScaleRectToScreenNoMargin(RECT* rect) const {
    return ScaleRectToScreenAt(WithoutMargin(this), nullptr, rect);
}
int CInterface3D::CallScalePointToScreenNoMargin(POINT* point) const {
    return ScalePointToScreenAt(WithoutMargin(this), nullptr, point);
}
int CInterface3D::CallScalePointFromScreenNoMargin(POINT* point) const {
    return ScalePointFromScreenAt(WithoutMargin(this), nullptr, point);
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
int __fastcall ScaleRectToScreenEntry(const CInterface3D* self, void*, RECT* rect) {
    if (!self && widescreen::Active()) return widescreen::RectToScreen(rect, false);
    return self->CInterface3D::ScaleRectToScreen(rect);
}
EI_REPLACE(0x005F7CA0, ScaleRectToScreenEntry);
EI_REBIRTH_IMPLEMENTS(0x005F7CA0, ei::rebirth::Patch::WideScreen);

int __fastcall ScalePointToScreenEntry(const CInterface3D* self, void*, POINT* point) {
    if (!self && widescreen::Active()) return widescreen::PointToScreen(point, false);
    return self->CInterface3D::ScalePointToScreen(point);
}
EI_REPLACE(0x005F7D20, ScalePointToScreenEntry);
EI_REBIRTH_IMPLEMENTS(0x005F7D20, ei::rebirth::Patch::WideScreen);

int __fastcall ScalePointFromScreenCopyEntry(const CInterface3D* self, void*, POINT* point) {
    if (!self && widescreen::Active()) return widescreen::PointFromScreen(point, false);
    return self->CInterface3D::ScalePointFromScreenCopy(point);
}
EI_REPLACE(0x005F7E30, ScalePointFromScreenCopyEntry);
EI_REBIRTH_IMPLEMENTS(0x005F7E30, ei::rebirth::Patch::WideScreen);
EI_REBIRTH_LIVE_STOLEN(0x005F7E30);

int __fastcall ScaleRectFromScreenEntry(const CInterface3D* self, void*, RECT* rect) {
    if (!self && widescreen::Active()) return widescreen::RectFromScreen(rect, false);
    return self->CInterface3D::ScaleRectFromScreen(rect);
}
EI_REPLACE(0x005F7DA0, ScaleRectFromScreenEntry);
EI_REBIRTH_IMPLEMENTS(0x005F7DA0, ei::rebirth::Patch::WideScreen);
EI_REBIRTH_LIVE_STOLEN(0x005F7DA0);

int __fastcall ScalePointFromScreenEntry(const CInterface3D* self, void*, POINT* point) {
    if (!self && widescreen::Active()) return widescreen::PointFromScreen(point, false);
    return self->CInterface3D::ScalePointFromScreen(point);
}
EI_REPLACE(0x005F7E00, ScalePointFromScreenEntry);
EI_REBIRTH_IMPLEMENTS(0x005F7E00, ei::rebirth::Patch::WideScreen);
EI_REBIRTH_LIVE_STOLEN(0x005F7E00);

CUiWindow* CInterface3D::GetActiveWindow() const {
    return stackBegin_ != stackEnd_ ? stackEnd_[-1] : nullptr;
}
EI_REPLACE_METHOD(0x005F7590, CInterface3D::GetActiveWindow);

void CInterface3D::SetPendingValue(std::uint32_t value) {
    pendingFlag_ = true;
    pendingValue_ = value;
}
EI_REPLACE_METHOD(0x005F74E0, CInterface3D::SetPendingValue);

// --- CUiSurface ------------------------------------------------------------------------------

void CUiSurface::ReleaseContents() { SurfaceReleaseContentsOriginal(this); }

void CUiSurface::SetRect(int left, int top, int right, int bottom) {
    rect_.left = left;
    rect_.top = top;
    rect_.right = right;
    rect_.bottom = bottom;
    interface_->CallScaleRectToScreen(&rect_);
}
EI_REPLACE_METHOD(0x005FD3A0, CUiSurface::SetRect);

void CUiSurface::MoveTo(int x, int y) {
    POINT point{x, y};
    interface_->CallScalePointToScreen(&point);
    int width = rect_.right - rect_.left;
    int height = rect_.bottom - rect_.top;
    rect_.left = point.x;
    rect_.top = point.y;
    rect_.right = width + point.x;
    rect_.bottom = height + point.y;
}
EI_REPLACE_METHOD(0x005FE400, CUiSurface::MoveTo);

// --- CUiModel --------------------------------------------------------------------------------

// /fp:fast would fold the two constant factors into one (different rounding); keep the original's
// order of operations.
#pragma float_control(precise, on, push)
void CUiModel::SetScreenPosition(float x, float y, float depth) {
    // 0074015C (1/400), 0073B84C (1), 0073C524 (0.75), 00740ED4: layout coordinates to the view
    // plane, (x / 400 - 1, y / 400 - 0.75) * k * depth. Each product is rounded once to float.
    constexpr float kLayoutScale = 0x1.47ae14p-9f;
    constexpr float kViewScale = 0x1.ed21e6p-2f;
    volatile float px = (x * kLayoutScale - 1.0f) * kViewScale * depth;
    volatile float py = (y * kLayoutScale - 0.75f) * kViewScale * depth;
    Vector3& position = object_->localPosition_;
    float value = px;
    std::memcpy(&position.x, &value, sizeof(float));
    value = py;
    std::memcpy(&position.y, &value, sizeof(float));
    std::memcpy(&position.z, &depth, sizeof(float));
}
#pragma float_control(pop)
EI_REPLACE_METHOD(0x005FC450, CUiModel::SetScreenPosition);

// --- Record lookups --------------------------------------------------------------------------

char* UiRecordTable::SpellAt(std::uint32_t index) const { return At(index, 0x84); }
EI_REPLACE_METHOD(0x005F2EA0, UiRecordTable::SpellAt);

char* UiRecordTable::ModifierAt(std::uint32_t index) const { return At(index, 0x24); }
EI_REPLACE_METHOD(0x005F2EE0, UiRecordTable::ModifierAt);
