#pragma once

// The picture saved with a game (shot.sav). The file starts as the original's: 256x192 5:6:5 pixels (0x18000 bytes,
// rows from the top, no header), what the original game reads; after them our trailer, which it never reads:
//   +0  "EIPC"   +4 version (1)   +8 width   +12 height   +16 length of the picture   +20 the picture: a QOI file
// (src/lib/qoi.h) of width x height pixels, 8 bits a channel (numbers little-endian 32 bits). A file without the trailer
// (the original's, an old save's) has only the 5:6:5 picture. See docs/notes/ui.md, "Save / load window pictures".

#include <cstddef>
#include <cstdint>
#include <vector>

namespace save_picture {

constexpr int kOldWidth = 256, kOldHeight = 192;
constexpr std::size_t kOldBytes = static_cast<std::size_t>(kOldWidth) * kOldHeight * 2;
// The largest trailer picture read (the textures' limit).
constexpr int kMostSide = 4096;

// RGBA8 pixels, rows from the top, opaque.
struct Picture {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
    bool Empty() const { return width <= 0 || height <= 0; }
};

// `picture` made `width` x `height` (each no larger than the picture's): each pixel the average of the source
// pixels its rectangle covers.
Picture Shrink(const Picture& picture, int width, int height);
// The picture as large as fits `width` x `height` without growing: the shrunk picture, or the picture as it is.
Picture Fit(const Picture& picture, int width, int height);

// The shot.sav bytes: `screen` as the 256x192 5:6:5 picture (each pixel the one under its centre, each channel's top
// bits: the original's picture of the screen) and `shown` in the trailer.
std::vector<std::uint8_t> ShotFile(const Picture& screen, const Picture& shown);
// The 5:6:5 picture of a shot.sav's bytes (each channel's bits repeated into the low ones); the rows a short file lacks
// are black.
Picture OldPicture(const std::uint8_t* bytes, std::size_t size);
// The trailer's picture; false without a trailer (a file of 0x18000 bytes or fewer, another magic) or with a broken one.
bool TrailerPicture(const std::uint8_t* bytes, std::size_t size, Picture* picture);
// The picture a shot.sav shows: the trailer's, else the 5:6:5 one.
Picture ReadShot(const std::uint8_t* bytes, std::size_t size);

} // namespace save_picture
