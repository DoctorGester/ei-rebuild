#pragma once

// A short PNG writer (docs/notes/mods.md, "Maps as folders"): grey, grey and alpha, RGB or RGBA of 8 or 16 bits a
// sample, each row with the filter whose bytes sum least as signed values (the usual choice), deflated by miniz.
// stb_image_write writes no 16-bit PNG; stb_image reads these.

#include <cstdint>
#include <span>
#include <string>

namespace png {

// The PNG of `width` x `height` pixels of `channels` samples (1 grey, 2 grey and alpha, 3 RGB, 4 RGBA) of `bits` (8
// or 16) each: `samples` row by row from the top, a pixel's channels together, each below 2^bits (the high bits of a
// larger one are dropped). Empty when the size, the kind or the sample count does not fit.
std::string Write(int width, int height, int channels, int bits, std::span<const std::uint16_t> samples);

}   // namespace png
