#pragma once

// What the audio backends read from waves and MP3 files without decoding them (platform/audio.h).

#include <cstddef>
#include <cstdint>

namespace platform::audio {

struct WaveInfo;

// Bytes a second of the wave takes: for IMA ADPCM the rate times the block size over the samples a block holds (4
// bytes of header per channel, then two samples a byte), truncated; 0 when the header makes no sense.
std::uint32_t WaveBytesPerSecond(const WaveInfo& info);

// An MP3 file's whole frames: their samples per channel, the first frame's rate and channels.
struct Mp3Layout {
    std::uint32_t frames = 0;
    std::uint32_t samples = 0;
    unsigned rate = 0;
    int channels = 0;
};
// The layout of the MPEG audio layer 3 frames in `data` (an ID3v2 tag at the start skipped; bytes that are not a
// frame header passed over a byte at a time; a frame cut short by the file's end not counted). No frames: zeros.
Mp3Layout ScanMp3(const void* data, std::size_t size);

} // namespace platform::audio
