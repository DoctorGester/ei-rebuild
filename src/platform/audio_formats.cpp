// RIFF WAVE headers, the voices' length rule and the MP3 frame scan (platform/audio.h, audio_formats.h): the same for
// every backend.
#include "platform/audio.h"
#include "platform/audio_formats.h"

#include <algorithm>
#include <cstring>

namespace platform::audio {

namespace {

std::uint32_t Read32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint16_t Read16(const std::uint8_t* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }

// The chunks a wave can have before its data are short; a size past this is not a wave.
constexpr std::uint32_t kLongestHeader = 1u << 20;

} // namespace

std::uint32_t WaveBytesPerSecond(const WaveInfo& info) {
    if (info.format == kImaAdpcm) {
        if (info.channels <= 0 || info.blockSize <= 4u * static_cast<std::uint32_t>(info.channels)) return 0;
        const std::uint32_t perBlock = (info.blockSize - 4u * info.channels) * 8 / (4u * info.channels) + 1;
        return static_cast<std::uint32_t>(static_cast<std::uint64_t>(info.rate) * info.blockSize / perBlock);
    }
    return info.rate * static_cast<std::uint32_t>(info.channels * info.bits) / 8;
}

WaveInfo ReadWaveInfo(const void* wave, std::size_t size) {
    WaveInfo info;
    const auto* p = static_cast<const std::uint8_t*>(wave);
    if (!p || size < 12 || std::memcmp(p, "RIFF", 4) != 0 || std::memcmp(p + 8, "WAVE", 4) != 0) return info;
    bool fact = false;
    for (std::uint64_t at = 12; at + 8 <= size && at < kLongestHeader;) {
        const std::uint8_t* chunk = p + at;
        const std::uint32_t length = Read32(chunk + 4);
        const std::uint64_t body = at + 8;
        if (std::memcmp(chunk, "fmt ", 4) == 0) {
            if (body + 16 > size) break;
            info.format = Read16(chunk + 8);
            info.channels = Read16(chunk + 10);
            info.rate = Read32(chunk + 12);
            info.blockSize = Read16(chunk + 20);
            info.bits = Read16(chunk + 22);
        } else if (std::memcmp(chunk, "fact", 4) == 0) {
            if (body + 4 > size) break;
            info.samples = Read32(chunk + 8);
            fact = true;
        } else if (std::memcmp(chunk, "data", 4) == 0) {
            info.dataLength = static_cast<std::uint32_t>(std::min<std::uint64_t>(length, size - body));
            info.data = chunk + 8;
            break;
        }
        at = body + length + (length & 1);
    }
    if (!info.data) return WaveInfo{};
    if (!fact || info.format != kImaAdpcm) {
        const std::uint32_t bitsPerFrame = static_cast<std::uint32_t>(info.bits * info.channels);
        info.samples = bitsPerFrame != 0 ? static_cast<std::uint32_t>(static_cast<std::uint64_t>(info.dataLength) * 8 / bitsPerFrame) : 0;
    }
    return info;
}

std::uint32_t WaveMilliseconds(const WaveInfo& info) {
    const std::uint32_t rate = WaveBytesPerSecond(info);
    if (!info.data || rate == 0) return 0;
    return static_cast<std::uint32_t>(static_cast<std::uint64_t>(info.dataLength) * 1000 / rate);
}

namespace {

// Layer 3 bit rates in kbit/s by the header's index: MPEG-1, then MPEG-2 and 2.5.
constexpr int kBitRates[2][16] = {
    {0, 32, 40, 48, 56, 64, 80, 96, 112, 128, 160, 192, 224, 256, 320, 0},
    {0, 8, 16, 24, 32, 40, 48, 56, 64, 80, 96, 112, 128, 144, 160, 0},
};
// Sample rates by the header's index: MPEG-1, MPEG-2, MPEG-2.5.
constexpr unsigned kRates[3][3] = {{44100, 48000, 32000}, {22050, 24000, 16000}, {11025, 12000, 8000}};

} // namespace

Mp3Layout ScanMp3(const void* data, std::size_t size) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    std::size_t at = 0;
    if (size >= 10 && std::memcmp(p, "ID3", 3) == 0)
        at = 10 + (static_cast<std::size_t>(p[6] & 0x7F) << 21 | static_cast<std::size_t>(p[7] & 0x7F) << 14 |
                   static_cast<std::size_t>(p[8] & 0x7F) << 7 | (p[9] & 0x7F));
    Mp3Layout layout;
    while (at + 4 <= size) {
        const std::uint32_t h = static_cast<std::uint32_t>(p[at]) << 24 | static_cast<std::uint32_t>(p[at + 1]) << 16 |
                                static_cast<std::uint32_t>(p[at + 2]) << 8 | p[at + 3];
        const unsigned version = h >> 19 & 3;   // 3 MPEG-1, 2 MPEG-2, 0 MPEG-2.5
        const unsigned layer = h >> 17 & 3;     // 1 layer 3
        const unsigned rateIndex = h >> 12 & 15;
        const unsigned sampleIndex = h >> 10 & 3;
        if ((h >> 21) != 0x7FF || layer != 1 || version == 1 || rateIndex == 0 || rateIndex == 15 || sampleIndex == 3) {
            ++at;
            continue;
        }
        const bool mpeg1 = version == 3;
        const unsigned rate = kRates[mpeg1 ? 0 : version == 2 ? 1 : 2][sampleIndex];
        const unsigned kbps = static_cast<unsigned>(kBitRates[mpeg1 ? 0 : 1][rateIndex]);
        const std::size_t length = (mpeg1 ? 144000u : 72000u) * kbps / rate + (h >> 9 & 1);
        if (at + length > size) break;
        if (layout.frames == 0) {
            layout.rate = rate;
            layout.channels = (h >> 6 & 3) == 3 ? 1 : 2;
        }
        ++layout.frames;
        layout.samples += mpeg1 ? 1152 : 576;
        at += length;
    }
    return layout;
}

} // namespace platform::audio
