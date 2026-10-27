// The interface windows by game.exe's table (C19): see harness_windows.h.
#include "harness_windows.h"

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

} // namespace ei::verify
