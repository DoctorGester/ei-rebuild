#pragma once

// Bink audio (the RDFT variant): a movie's sound track decoded a packet at a time into interleaved 16-bit samples.

#include "platform/bink/bit_reader.h"

#include <complex>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace platform::bink {

class AudioDecoder {
public:
    // A track of `rate` samples a second per channel, 1 or 2 channels.
    AudioDecoder(int rate, int channels);

    // Appends the samples of a frame's audio packet to `out`: `samples` of them (the packet's own count, all channels
    // together), from the packet's transform frames.
    void Decode(const std::uint8_t* data, std::size_t size, std::size_t samples, std::vector<std::int16_t>& out);
    // The next packet starts the sound afresh (after a jump), without blending into the samples before.
    void Reset() { first = true; }

private:
    void DecodeFrame(BitReader& bits);
    void InverseTransform();

    int frameSize;   // the values of one transform, the channels interleaved
    int overlap;     // the samples of a frame blended into the next one's start
    float scale;     // from the transform's values to samples
    std::vector<int> bands;   // the quantiser bands' first coefficient pairs, and the end
    std::vector<float> coefficients;
    std::vector<std::complex<double>> work;
    std::vector<std::complex<double>> twiddles;   // e^(2 pi i k / frameSize), k < frameSize / 2
    std::vector<std::int16_t> samples;            // the frame decoded last
    std::vector<std::int16_t> previous;           // the last frame's tail, blended into the next frame
    bool first = true;
};

} // namespace platform::bink
