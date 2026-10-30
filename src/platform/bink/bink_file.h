#pragma once

// The Bink 1 container (revision i): the header, the frame index and each frame's packets, read from the file a
// frame at a time.

#include "platform/platform.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform::bink {

// A frame's packets, inside the buffer they were read into.
struct FramePackets {
    const std::uint8_t* audio = nullptr;   // the first sound track's, null when it has none in this frame
    std::size_t audioBytes = 0;
    std::size_t audioSamples = 0;          // the samples it decodes to, the channels together
    const std::uint8_t* video = nullptr;
    std::size_t videoBytes = 0;
};

class File {
public:
    // False when the file cannot be read or is not a movie this decoder plays (revision i, without alpha, grayscale
    // or scaling).
    bool Open(const char* path);   // UTF-8

    int Width() const { return width; }
    int Height() const { return height; }
    unsigned FrameCount() const { return frameCount; }
    // Frames per second as rate / divider.
    std::uint32_t Rate() const { return rate; }
    std::uint32_t Divider() const { return divider; }
    // The first sound track: 0 samples a second when there is none this decoder plays.
    int AudioRate() const { return audioRate; }
    int AudioChannels() const { return audioChannels; }

    // Whether frame `index` (from 0) needs no frame before it.
    bool Keyframe(unsigned index) const { return (index < frameCount) && (offsets[index] & 1) != 0; }
    // Reads frame `index` (from 0); false when it cannot be read.
    bool ReadFrame(unsigned index, FramePackets& packets);

private:
    platform::File stream;
    int width = 0;
    int height = 0;
    unsigned frameCount = 0;
    std::uint32_t rate = 0;
    std::uint32_t divider = 0;
    int audioRate = 0;
    int audioChannels = 0;
    unsigned audioTracks = 0;
    std::vector<std::uint32_t> offsets;   // per frame and the end, bit 0 the keyframe flag
    std::vector<std::uint8_t> buffer;
};

} // namespace platform::bink
