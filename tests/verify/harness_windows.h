#pragma once

// The interface windows by game.exe's table (C19, C28; docs/notes/standalone.md, "Compiler-made vtables"). The image
// builds the windows as C++ classes; the harness names a window by game.exe's table of its kind (`&ei::OriginalGameData<const std::uint8_t>(address)`),
// which is what game.exe had at the window's +0 when the goldens were first recorded, as the digest names the AI
// modes (harness_digest.cpp, ModeKinds). Verify build only.

#include "harness_original.h"
#include "image/image_side.h"

#include <cstdint>

namespace ei::verify {

// game.exe's table of `window`'s kind: the table of its class (by its C++ type), else its +0.
std::uintptr_t WindowKind(const void* window);

// The window on top (Interface3D::GetActiveWindow); null without one.

void* TopWindow();

// The window's clickable regions (null ones too), and region `index` (false for a null one).
int WindowRegionCount(void* window);
bool WindowRegion(void* window, int index, image::RegionInfo* region);

} // namespace ei::verify
