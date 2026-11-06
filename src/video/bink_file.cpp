// The Bink 1 container. The 44-byte header: "BIKi", the file's size less 8, the frame count, the largest frame's size,
// the frame count again, the width, the height, the rate as a dividend and a divider, the video flags and the number
// of sound tracks. Per track then two 16-bit numbers (unknown, channels), per track the rate and the track's flags
// (bit 13 stereo, bit 12 the DCT variant), per track a 32-bit id; then the frame index: per frame and the end of the
// last its offset in the file, bit 0 set on keyframes. A frame holds per track the packet's size (0: none) followed
// by the number of bytes it decodes to and the packet, then the video packet.
#include "video/bink_file.h"

#include "lib/bytes.h"

#include <cstring>

namespace bink {

namespace {

constexpr std::size_t kHeaderBytes = 44;
constexpr std::uint32_t kLargestSide = 4096;
constexpr unsigned kMostFrames = 1u << 20;
constexpr unsigned kMostTracks = 256;
constexpr std::size_t kLargestFrame = 64u << 20;
// Video flags this decoder does not play: an alpha plane, grayscale, and the scaled and interlaced modes.
constexpr std::uint32_t kUnplayedFlags = 0x00100000 | 0x00020000 | 0xF0000000;
constexpr std::uint16_t kStereo = 0x2000;
constexpr std::uint16_t kDctAudio = 0x1000;

} // namespace

bool File::Open(std::string_view path) {
    // Through the platform: a movie path has backslashes and the game's case, which only its file calls resolve on Linux.
    if (!file.Open(path, platform::FileMode::Read)) return false;
    std::uint8_t header[kHeaderBytes];
    if (file.Read(header, kHeaderBytes) != kHeaderBytes || std::memcmp(header, "BIKi", 4) != 0) return false;
    frameCount = lib::Load<std::uint32_t>(header + 8);
    const std::uint32_t w = lib::Load<std::uint32_t>(header + 20), h = lib::Load<std::uint32_t>(header + 24);
    rate = lib::Load<std::uint32_t>(header + 28);
    divider = lib::Load<std::uint32_t>(header + 32);
    const std::uint32_t flags = lib::Load<std::uint32_t>(header + 36);
    audioTracks = lib::Load<std::uint32_t>(header + 40);
    if (frameCount == 0 || frameCount > kMostFrames || w == 0 || w > kLargestSide || h == 0 || h > kLargestSide ||
        rate == 0 || divider == 0 || (flags & kUnplayedFlags) != 0 || audioTracks > kMostTracks)
        return false;
    width = static_cast<int>(w);
    height = static_cast<int>(h);

    std::vector<std::uint8_t> tracks(std::size_t{12} * audioTracks);
    if (file.Read(tracks.data(), static_cast<unsigned>(tracks.size())) != tracks.size()) return false;
    if (audioTracks > 0) {
        const std::uint8_t* track = tracks.data() + 4 * audioTracks;
        const std::uint16_t trackRate = lib::Load<std::uint16_t>(track), trackFlags = lib::Load<std::uint16_t>(track + 2);
        if (trackRate != 0 && !(trackFlags & kDctAudio)) {
            audioRate = trackRate;
            audioChannels = trackFlags & kStereo ? 2 : 1;
        }
    }

    std::vector<std::uint8_t> index(std::size_t{4} * (frameCount + 1));
    if (file.Read(index.data(), static_cast<unsigned>(index.size())) != index.size()) return false;
    offsets.resize(frameCount + 1);
    for (unsigned i = 0; i <= frameCount; ++i) offsets[i] = lib::Load<std::uint32_t>(index.data() + 4 * i);
    return true;
}

bool File::ReadFrame(unsigned index, FramePackets& packets) {
    packets = {};
    if (index >= frameCount) return false;
    const std::uint32_t start = offsets[index] & ~1u, end = offsets[index + 1] & ~1u;
    if (end < start || end - start > kLargestFrame) return false;
    const std::size_t size = end - start;
    // Zeros after the packet: the bit readers may look a word past their end.
    buffer.assign(size + 8, 0);
    if (!file.ReadAt(static_cast<long>(start), buffer.data(), static_cast<unsigned>(size))) return false;

    std::size_t at = 0;
    for (unsigned track = 0; track < audioTracks; ++track) {
        if (at + 4 > size) return false;
        const std::size_t bytes = lib::Load<std::uint32_t>(buffer.data() + at);
        at += 4;
        if (bytes > size - at) return false;
        if (track == 0 && audioRate != 0 && bytes >= 4) {
            packets.audioSamples = lib::Load<std::uint32_t>(buffer.data() + at) / 2;
            packets.audio = buffer.data() + at + 4;
            packets.audioBytes = bytes - 4;
        }
        at += bytes;
    }
    packets.video = buffer.data() + at;
    packets.videoBytes = size - at;
    return true;
}

} // namespace bink
