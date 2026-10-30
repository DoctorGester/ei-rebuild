#pragma once

#include "render_rect.h"
#include "sokol_gfx.h"

#include <cstdint>
#include <vector>

namespace render::detail {
// The backend boundary for operations sokol does not expose: window drawables, readback, copies and mipmaps.
bool NativeEnvironment(sg_environment* environment);
int NativeSampleCount();
void DropNativeDevice();
sg_swapchain NativeDrawable(void* window, bool vsync, int* width, int* height);
void NativeCopyImage(sg_image from, sg_image to, const Rect& region, int x, int y);
void NativeWriteImage(sg_image image, const Rect& region, const void* bytes, int pitch);
void NativeGenerateMipmaps(sg_image image);
bool NativeReadImage(sg_image image, unsigned x, unsigned y, unsigned width, unsigned height,
                     std::vector<std::uint8_t>* bytes, std::size_t* pitch);
}
