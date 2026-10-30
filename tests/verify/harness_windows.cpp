// The interface windows by game.exe's table (C19): see harness_windows.h.
#include "harness_windows.h"

#include "image/image_side.h"

namespace ei::verify {

std::string WindowKind(const void* window) { return image::WindowKind(window); }

void* TopWindow() { return image::TopWindow(); }

int WindowRegionCount(void* window) { return image::RegionCount(window); }

bool WindowRegion(void* window, int index, image::RegionInfo* region) { return image::Region(window, index, region); }

} // namespace ei::verify
