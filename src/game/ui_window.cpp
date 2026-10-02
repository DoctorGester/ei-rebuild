// Interface windows, their regions and the aligned pointer arrays they keep. See ui.h.
#include "ui.h"

#include "ei/hook.h"
#include "lib/crt.h"
#include "lib/gameapi.h"

#include <cstring>

namespace {
// 32-bit wrapping multiply (IMUL r32, r/m32).
int Mul(int a, int b) {
    return static_cast<int>(static_cast<std::uint32_t>(a) * static_cast<std::uint32_t>(b));
}

// Growth step of the interface arrays when none is set: size / 8 clamped to [4, 1024].
int DefaultGrowth(int size) {
    int grow = size / 8;
    if (grow < 4) return 4;
    if (grow > 0x400) return 0x400;
    return grow;
}

// The capacity a full array grows to when it must hold `newSize` elements.
int GrownCapacity(int size, int capacity, int growBy, int newSize) {
    int grow = growBy != 0 ? growBy : DefaultGrowth(size);
    int newCapacity = grow + capacity;
    return newSize < newCapacity ? newCapacity : newSize;
}

// Virtual slot 19 of a window (called with the new enabled state).
void CallSlot19(CUiWindow* window, int value) {
    using Fn = void(__thiscall*)(CUiWindow*, int);
    reinterpret_cast<Fn>(window->vtable_[19])(window, value);
}

// The deleting destructor (slot 0) of a region's tooltip.
void DeleteTooltip(CUiTooltip* tip) {
    using Fn = void*(__thiscall*)(CUiTooltip*, int);
    reinterpret_cast<Fn>(tip->vtable_[0])(tip, 1);
}
} // namespace

// --- Aligned storage -------------------------------------------------------------------------

void* AllocAligned(std::size_t bytes) {
    auto* block = static_cast<char*>(gamecrt::operator_new(bytes + 0x24));
    auto* data = reinterpret_cast<char*>((reinterpret_cast<std::uintptr_t>(block) + 0x23) & ~std::uintptr_t{0x1F});
    reinterpret_cast<void**>(data)[-1] = block;
    return data;
}

void FreeAligned(void* data) { gamecrt::operator_delete(static_cast<void**>(data)[-1]); }

// --- UiPtrArray ------------------------------------------------------------------------------

template <typename T>
void UiPtrArray<T>::SetSize(int newSize, int growBy) {
    if (growBy != -1) growBy_ = growBy;
    if (newSize == 0) {
        if (data_) {
            FreeAligned(data_);
            data_ = nullptr;
        }
        capacity_ = 0;
        size_ = 0;
        return;
    }
    if (!data_) {
        data_ = static_cast<T**>(AllocAligned(static_cast<std::size_t>(newSize) * 4));
        capacity_ = newSize;
        size_ = newSize;
        return;
    }
    if (newSize <= capacity_) {
        size_ = newSize;
        return;
    }
    int newCapacity = GrownCapacity(size_, capacity_, growBy_, newSize);
    auto** data = static_cast<T**>(AllocAligned(static_cast<std::size_t>(newCapacity) * 4));
    std::memcpy(data, data_, static_cast<std::size_t>(size_) * 4);
    FreeAligned(data_);
    data_ = data;
    size_ = newSize;
    capacity_ = newCapacity;
}

template <typename T>
void UiPtrArray<T>::Append(T* item) {
    int index = size_;
    int newSize = index + 1;
    if (newSize == 0) {
        // Unreachable in practice (size -1); the original then stores through a null array.
        if (data_) {
            FreeAligned(data_);
            data_ = nullptr;
        }
        capacity_ = 0;
        size_ = 0;
    } else if (!data_) {
        data_ = static_cast<T**>(AllocAligned(static_cast<std::size_t>(newSize) * 4));
        capacity_ = newSize;
        size_ = newSize;
    } else if (newSize <= capacity_) {
        size_ = newSize;
    } else {
        int newCapacity = GrownCapacity(index, capacity_, growBy_, newSize);
        auto** data = static_cast<T**>(AllocAligned(static_cast<std::size_t>(newCapacity) * 4));
        std::memcpy(data, data_, static_cast<std::size_t>(size_) * 4);
        FreeAligned(data_);
        data_ = data;
        size_ = newSize;
        capacity_ = newCapacity;
    }
    data_[index] = item;
}

template <typename T>
void UiPtrArray<T>::RemoveSlot(int index) {
    // REP MOVSD of the size - index - 1 following slots (skipped when that is 0).
    int count = size_ - index - 1;
    for (int i = 0; i < count; ++i) data_[index + i] = data_[index + i + 1];
    --size_;
}

template <typename T>
void UiPtrArray<T>::FreeStorage() {
    if (data_) {
        FreeAligned(data_);
        data_ = nullptr;
    }
    capacity_ = 0;
    size_ = 0;
}

template struct UiPtrArray<CEngineObject>;
template struct UiPtrArray<CUiSurface>;
template struct UiPtrArray<CUiRegion>;
template struct UiPtrArray<CUiModel>;

// The original has a separate (identical) copy of SetSize for each of the window's arrays; the two
// CEngineObject lists share one instantiation here.
EI_REPLACE_METHOD(0x006003D0, UiPtrArray<CEngineObject>::SetSize);
EI_REPLACE_METHOD(0x006004E0, UiPtrArray<CEngineObject>::SetSize);
EI_REPLACE_METHOD(0x00600730, UiPtrArray<CUiSurface>::SetSize);
EI_REPLACE_METHOD(0x00600840, UiPtrArray<CUiRegion>::SetSize);
EI_REPLACE_METHOD(0x00600950, UiPtrArray<CUiModel>::SetSize);

// --- CUiTooltip ------------------------------------------------------------------------------

__declspec(naked) void CUiTooltip::OnMouseMove(int, int) EI_ORIGINAL(0x0065E580)

// --- CUiRegion -------------------------------------------------------------------------------

__declspec(naked) void CUiRegion::ReleaseContents() EI_ORIGINAL(0x005FC600)

BOOL CUiRegion::HitTest(int x, int y) const {
    x -= offsetX_;
    y -= offsetY_;
    if (!enabled_) return 0;
    if (!gameapi::PtInRect(&bounds_, POINT{x, y})) return 0;
    if (rectOnly_) return 1;
    // Convex polygon: the point must not lie on the outer side of any edge a -> b. The products
    // wrap like the original's 32-bit IMULs.
    int count = pointCount_;
    for (int i = 0; i < count;) {
        const UiPoint& a = points_[i];
        ++i;
        const UiPoint& b = points_[i % count];
        int lhs = Mul(y, b.x) + Mul(b.y, a.x) + Mul(a.y, x);
        int rhs = Mul(a.y, b.x) + Mul(y, a.x) + Mul(b.y, x);
        if (lhs < rhs) return 0;
    }
    return 1;
}
EI_REPLACE_METHOD(0x005FC8F0, CUiRegion::HitTest);

BOOL CUiRegion::HasActiveTip() const {
    if (!tip_) return 0;
    int state = tip_->state_;
    return state != 0 && state != 1;
}
EI_REPLACE_METHOD(0x005FC660, CUiRegion::HasActiveTip);

void CUiRegion::SetRect(int left, int top, int right, int bottom) {
    if (points_) {
        FreeAligned(points_);
        points_ = nullptr;
    }
    pointCapacity_ = 0;
    pointCount_ = 0;
    bounds_.left = left;
    bounds_.right = right;
    bounds_.top = top;
    bounds_.bottom = bottom;
    enabled_ = true;
    rectOnly_ = true;
}
EI_REPLACE_METHOD(0x005FC9E0, CUiRegion::SetRect);

void CUiRegion::ClearShape() {
    if (points_) {
        FreeAligned(points_);
        points_ = nullptr;
    }
    pointCapacity_ = 0;
    pointCount_ = 0;
    enabled_ = false;
    rectOnly_ = false;
}
EI_REPLACE_METHOD(0x005FCA40, CUiRegion::ClearShape);

void CUiRegion::AddPoint(int x, int y) {
    if (pointCount_ == 0) {
        bounds_.left = x;
        bounds_.top = y;
        bounds_.right = x;
        bounds_.bottom = y;
    } else {
        if (x < bounds_.left) bounds_.left = x;
        if (y < bounds_.top) bounds_.top = y;
        if (x > bounds_.right) bounds_.right = x;
        if (y > bounds_.bottom) bounds_.bottom = y;
    }
    // The vertex array grows like UiPtrArray, with 8-byte elements (allocated by 004F65B0).
    int index = pointCount_;
    int newCount = index + 1;
    if (newCount == 0) {
        if (points_) {
            FreeAligned(points_);
            points_ = nullptr;
        }
        pointCapacity_ = 0;
        pointCount_ = 0;
    } else if (!points_) {
        points_ = static_cast<UiPoint*>(AllocAligned(static_cast<std::size_t>(newCount) * 8));
        pointCapacity_ = newCount;
        pointCount_ = newCount;
    } else if (newCount <= pointCapacity_) {
        pointCount_ = newCount;
    } else {
        int newCapacity = GrownCapacity(index, pointCapacity_, pointGrowBy_, newCount);
        auto* points = static_cast<UiPoint*>(AllocAligned(static_cast<std::size_t>(newCapacity) * 8));
        std::memcpy(points, points_, static_cast<std::size_t>(pointCount_) * 8);
        FreeAligned(points_);
        points_ = points;
        pointCount_ = newCount;
        pointCapacity_ = newCapacity;
    }
    points_[index].x = x;
    points_[index].y = y;
    if (pointCount_ > 2) enabled_ = true;
}
EI_REPLACE_METHOD(0x005FCA70, CUiRegion::AddPoint);

void CUiRegion::OnMouseMove(int x, int y) {
    if (!tip_) return;
    BOOL inside = HitTest(x, y);
    int state = tip_->state_;
    if (inside) {
        if (state == 0 || state == 1) tip_->state_ = 1;
    } else if (state != 0 && state != 1) {
        tip_->state_ = 3;
    }
    tip_->OnMouseMove(x, y);
}
EI_REPLACE_METHOD(0x005FC880, CUiRegion::OnMouseMove);

// --- CUiWindow -------------------------------------------------------------------------------

int CUiWindow::RegionAt(int x, int y) const {
    POINT point{x, y};
    interface_->CallScalePointFromScreen(&point);
    for (int i = 0; i < regions_.size_; ++i) {
        if (regions_.data_[i]->HitTest(point.x, point.y)) return i;
    }
    return -1;
}
EI_REPLACE_METHOD(0x005FFCC0, CUiWindow::RegionAt);

BOOL CUiWindow::ContainsPoint(int x, int y) const {
    POINT point{x, y};
    interface_->CallScalePointFromScreen(&point);
    return gameapi::PtInRect(&rect_, point);
}
EI_REPLACE_METHOD(0x005FFD30, CUiWindow::ContainsPoint);

void CUiWindow::UpdateRegionTips(int) {
    POINT mouse{interface_->mouseX_, interface_->mouseY_};
    interface_->CallScalePointFromScreen(&mouse);
    // Inlines CUiRegion::OnMouseMove for every region with a tooltip.
    for (int i = 0; i < regions_.size_; ++i) {
        CUiRegion* region = regions_.data_[i];
        if (region->tip_) region->OnMouseMove(mouse.x, mouse.y);
    }
}
EI_REPLACE_METHOD(0x005FEBF0, CUiWindow::UpdateRegionTips);

void CUiWindow::SetEnabled(int enabled, int notify) {
    enabled_ = enabled;
    if (notify) CallSlot19(this, enabled);
}
EI_REPLACE_METHOD(0x006002A0, CUiWindow::SetEnabled);

void CUiWindow::AddObject(CEngineObject* object) {
    if (!object) return;
    objects_.Append(object);
    ++object->refCount_;
}
EI_REPLACE_METHOD(0x005FEDD0, CUiWindow::AddObject);

void CUiWindow::RemoveObject(CEngineObject* object) {
    if (!object) return;
    for (int i = 0; i < objects_.size_; ++i) {
        if (objects_.data_[i] == object) {
            objects_.data_[i]->Release();
            objects_.RemoveSlot(i);
            return;
        }
    }
}
EI_REPLACE_METHOD(0x005FF110, CUiWindow::RemoveObject);

void CUiWindow::RemoveObjectAt(int index) {
    objects_.data_[index]->Release();
    objects_.RemoveSlot(index);
}
EI_REPLACE_METHOD(0x005FF180, CUiWindow::RemoveObjectAt);

BOOL CUiWindow::RemoveSceneObject(CEngineObject* object) {
    for (int i = 0; i < sceneObjects_.size_; ++i) {
        if (sceneObjects_.data_[i] == object) {
            // Unlike RemoveObject, the slot is removed before the release.
            sceneObjects_.RemoveSlot(i);
            object->Release();
            return 1;
        }
    }
    return 0;
}
EI_REPLACE_METHOD(0x005FF700, CUiWindow::RemoveSceneObject);

void CUiWindow::RemoveSceneObjectAt(int index) {
    sceneObjects_.data_[index]->Release();
    sceneObjects_.RemoveSlot(index);
}
EI_REPLACE_METHOD(0x005FF770, CUiWindow::RemoveSceneObjectAt);

void CUiWindow::ClearSceneObjects() {
    for (int i = 0; i < sceneObjects_.size_; ++i) sceneObjects_.data_[i]->Release();
    sceneObjects_.FreeStorage();
}
EI_REPLACE_METHOD(0x005FF7D0, CUiWindow::ClearSceneObjects);

void CUiWindow::AddSurface(CUiSurface* surface) {
    if (!surface) return;
    surfaces_.Append(surface);
}
EI_REPLACE_METHOD(0x005FFA90, CUiWindow::AddSurface);

void CUiWindow::RemoveSurfaceAt(int index) {
    if (CUiSurface* surface = surfaces_.data_[index]) {
        surface->ReleaseContents();
        gamecrt::operator_delete(surface);
    }
    surfaces_.RemoveSlot(index);
}
EI_REPLACE_METHOD(0x005FFBF0, CUiWindow::RemoveSurfaceAt);

void CUiWindow::ClearSurfaces() {
    for (int i = 0; i < surfaces_.size_; ++i) {
        if (CUiSurface* surface = surfaces_.data_[i]) {
            surface->ReleaseContents();
            gamecrt::operator_delete(surface);
        }
    }
    surfaces_.FreeStorage();
}
EI_REPLACE_METHOD(0x005FFC50, CUiWindow::ClearSurfaces);

CUiRegion* CUiWindow::AddRegion() {
    auto* region = static_cast<CUiRegion*>(gamecrt::operator_new(sizeof(CUiRegion)));
    if (region) {
        // The bounds are left uninitialised, as in the original.
        region->interface_ = interface_;
        region->points_ = nullptr;
        region->pointGrowBy_ = 0;
        region->pointCapacity_ = 0;
        region->pointCount_ = 0;
        region->tip_ = nullptr;
        region->enabled_ = false;
        region->rectOnly_ = false;
        region->offsetX_ = 0;
        region->offsetY_ = 0;
    }
    regions_.Append(region);
    return region;
}
EI_REPLACE_METHOD(0x005FF830, CUiWindow::AddRegion);

void CUiWindow::RemoveRegionAt(int index) {
    if (CUiRegion* region = regions_.data_[index]) {
        region->ReleaseContents();
        gamecrt::operator_delete(region);
    }
    regions_.RemoveSlot(index);
}
EI_REPLACE_METHOD(0x005FF9C0, CUiWindow::RemoveRegionAt);

void CUiWindow::ClearRegions() {
    for (int i = 0; i < regions_.size_; ++i) {
        if (CUiRegion* region = regions_.data_[i]) {
            region->ReleaseContents();
            gamecrt::operator_delete(region);
        }
    }
    regions_.FreeStorage();
}
EI_REPLACE_METHOD(0x005FFA20, CUiWindow::ClearRegions);

CUiModel* CUiWindow::AddModel() {
    auto* model = static_cast<CUiModel*>(gamecrt::operator_new(sizeof(CUiModel)));
    if (!model) return nullptr;
    model->object_ = nullptr;   // 005FC1C0, the constructor
    models_.Append(model);
    return model;
}
EI_REPLACE_METHOD(0x005FFD80, CUiWindow::AddModel);

void CUiWindow::ClearModels() {
    for (int i = 0; i < models_.size_; ++i) {
        if (CUiModel* model = models_.data_[i]) {
            model->ReleaseObject();
            gamecrt::operator_delete(model);
        }
    }
    models_.FreeStorage();
}
EI_REPLACE_METHOD(0x005FFF40, CUiWindow::ClearModels);
