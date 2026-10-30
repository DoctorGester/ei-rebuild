#pragma once

// Bink 1 video, revision i (BIKi), decoded a frame at a time into Y, U and V planes (4:2:0, 8 bits).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform::bink {

// One plane: `width` x `height` bytes `stride` apart, the decoded size rounded up to whole 8x8 blocks.
struct Plane {
    int width = 0;
    int height = 0;
    int stride = 0;
    std::vector<std::uint8_t> pixels;

    std::uint8_t* Row(int y) { return pixels.data() + static_cast<std::size_t>(y) * stride; }
    const std::uint8_t* Row(int y) const { return pixels.data() + static_cast<std::size_t>(y) * stride; }
};

class VideoDecoder {
public:
    // A movie of `width` x `height` pixels.
    VideoDecoder(int width, int height);

    // Decodes a frame's video packet; its blocks refer to the frame decoded before.
    void Decode(const std::uint8_t* packet, std::size_t size);

    // The frame decoded last: luma, then the two chroma planes at half the size.
    const Plane& Luma() const { return current[0]; }
    const Plane& ChromaU() const { return current[1]; }
    const Plane& ChromaV() const { return current[2]; }

private:
    Plane current[3];
    Plane previous[3];
};

} // namespace platform::bink
