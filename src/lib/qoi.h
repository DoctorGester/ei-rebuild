#pragma once

// QOI ("Quite OK Image", qoiformat.org, specification 1.0): a lossless picture format of byte-sized operations, the
// save pictures' full-colour copy (src/game/save_picture.h).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace lib {

// `width` x `height` RGBA8 pixels (rows from the top) as a QOI file with `channels` 3 (the alpha left out: every pixel
// taken as opaque) or 4; empty for a size of 0.
std::vector<std::uint8_t> EncodeQoi(const std::uint8_t* rgba, int width, int height, int channels);
// A QOI file's pixels as RGBA8 (alpha 255 for a 3-channel file), rows from the top. False for a file that is not one, is
// cut short, or is wider or higher than `mostSide`.
bool DecodeQoi(const std::uint8_t* data, std::size_t size, int mostSide, int* width, int* height, std::vector<std::uint8_t>* rgba);

} // namespace lib
