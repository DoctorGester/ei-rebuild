#pragma once

// The interface windows (C19, C28). The image builds the windows as C++ classes; the harness names a window as a
// scenario does (menu, battle, briefing, ...: image/harness_windows_image.cpp), else by its class. Verify build only.

#include "image/image_side.h"

#include <string>

namespace ei::verify {

// `window`'s name (a scenario's, else its class's).
std::string WindowKind(const void* window);

// The window on top (Interface3D::GetActiveWindow); null without one.

void* TopWindow();

// The window's clickable regions (null ones too), and region `index` (false for a null one).
int WindowRegionCount(void* window);
bool WindowRegion(void* window, int index, image::RegionInfo* region);

} // namespace ei::verify
