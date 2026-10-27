// The interface windows by game.exe's table (C19): see harness_windows.h.
#include "harness_windows.h"

#include "exe_objects.h"
#include "image/image_side.h"

#include "ei/hook.h"

namespace ei::verify {

std::uintptr_t WindowKind(const void* window) {
    if (ei::Standalone()) return image::WindowKind(window);
    return *static_cast<const std::uintptr_t*>(window);   // game.exe's windows keep game.exe's tables
}

void* TopWindow() {
    if (ei::Standalone()) return image::TopWindow();
    // game.exe's CInterface3D::GetActiveWindow (005F7590, thiscall) on its interface (007C7D50).
    return OriginalCode<void*(__thiscall*)(void*)>(0x005F7590)(&ei::OriginalGameData<std::uint8_t>(0x007C7D50));
}

int WindowRegionCount(void* window) {
    if (ei::Standalone()) return image::RegionCount(window);
    return static_cast<const exe::CUiWindow*>(window)->regions_.size;
}

bool WindowRegion(void* window, int index, image::RegionInfo* region) {
    if (ei::Standalone()) return image::Region(window, index, region);
    const exe::CUiRegion* found = static_cast<const exe::CUiWindow*>(window)->regions_.data[index];
    if (!found) return false;
    region->left = found->bounds_.left;
    region->top = found->bounds_.top;
    region->right = found->bounds_.right;
    region->bottom = found->bounds_.bottom;
    region->offsetX = found->offsetX_;
    region->offsetY = found->offsetY_;
    region->enabled = found->enabled_;
    return true;
}

} // namespace ei::verify
