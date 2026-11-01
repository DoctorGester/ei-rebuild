// Bink audio, RDFT variant: a packet holds transform frames, each the spectrum of a block of interleaved samples. A
// frame gives two raw values (the spectrum's ends), a quantiser per critical band, then the other coefficients in runs
// sharing a bit width; an inverse real DFT turns it into samples, and each frame's start is blended with the last
// frame's tail. Written from the format's description (multimedia.cx: Bink Audio) and checked against an independent
// decoder (docs/notes/platform.md, "Movies").
#include "video/bink_audio.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace bink {

namespace {

// The bands' lower edges in Hz.
constexpr unsigned kCriticalFrequencies[] = {0,    100,  200,  300,  400,  510,  630,  770,  920,
                                             1080, 1270, 1480, 1720, 2000, 2320, 2700, 3150, 3700,
                                             4400, 5300, 6400, 7700, 9500, 12000, 15500};
// The lengths (in eights of coefficients) of a run of coefficients sharing a width, when not 8.
constexpr int kRunLengths[16] = {2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15, 16, 32, 64};

// A 29-bit float: 23 bits of mantissa (above the 5-bit exponent), the sign at bit 28; mantissa * 2^(exponent - 23).
float ReadFloat(BitReader& bits) {
    const std::uint32_t low = bits.Bits(16);
    const std::uint32_t value = low | bits.Bits(13) << 16;
    const float magnitude = std::ldexp(static_cast<float>(value >> 5 & 0x7FFFFF), static_cast<int>(value & 31) - 23);
    return value & 0x10000000 ? -magnitude : magnitude;
}

int FloorDivide(int a, int b) {
    const int q = a / b;
    return q * b != a && a < 0 ? q - 1 : q;
}

} // namespace

AudioDecoder::AudioDecoder(int rate, int channels) {
    const int base = rate >= 44100 ? 2048 : rate >= 22050 ? 1024 : 512;
    frameSize = base * channels;
    overlap = frameSize / 16;
    scale = static_cast<float>(2.0 / std::sqrt(static_cast<double>(frameSize)));
    const unsigned half = static_cast<unsigned>(frameSize / 2);
    const unsigned nyquist = (static_cast<unsigned>(rate * channels) + 1) / 2;
    for (unsigned frequency : kCriticalFrequencies) {
        if (frequency >= nyquist) break;
        bands.push_back(static_cast<int>(frequency * half / nyquist));
    }
    bands.push_back(static_cast<int>(half));
    coefficients.resize(static_cast<std::size_t>(frameSize));
    work.resize(half);
    twiddles.resize(half);
    for (unsigned k = 0; k < half; ++k)
        twiddles[k] = std::polar(1.0, 2 * std::numbers::pi * k / frameSize);
    samples.resize(static_cast<std::size_t>(frameSize));
    previous.resize(static_cast<std::size_t>(overlap));
}

void AudioDecoder::Decode(const std::uint8_t* data, std::size_t size, std::size_t count, std::vector<std::int16_t>& out) {
    BitReader bits(data, size);
    const std::size_t perFrame = static_cast<std::size_t>(frameSize - overlap);
    while (count > 0 && bits.More()) {
        DecodeFrame(bits);
        const std::size_t take = std::min(count, perFrame);
        out.insert(out.end(), samples.begin(), samples.begin() + static_cast<std::ptrdiff_t>(take));
        count -= take;
        bits.AlignToWord();
    }
}

void AudioDecoder::DecodeFrame(BitReader& bits) {
    coefficients[0] = ReadFloat(bits);
    coefficients[1] = ReadFloat(bits);
    const std::size_t bandCount = bands.size() - 1;
    float quantisers[std::size(kCriticalFrequencies)];
    for (std::size_t b = 0; b < bandCount; ++b)
        quantisers[b] = static_cast<float>(std::pow(10.0, bits.Bits(8) * static_cast<double>(0.0664f)));

    // The coefficients from the third on, as integers, in runs of a shared width (0: all zero).
    std::vector<std::int16_t> values(static_cast<std::size_t>(frameSize), 0);
    for (int i = 2; i < frameSize;) {
        int end = bits.Bit() ? i + 8 * kRunLengths[bits.Bits(4)] : i + 8;
        end = std::min(end, frameSize);
        const int width = static_cast<int>(bits.Bits(4));
        for (; i < end; ++i) {
            if (width == 0) continue;
            const int v = static_cast<int>(bits.Bits(width));
            values[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(v != 0 && bits.Bit() ? -v : v);
        }
    }
    // Pairs of coefficients times their band's quantiser; pair 0 is the two raw values.
    for (std::size_t b = 0; b < bandCount; ++b) {
        for (int pair = std::max(bands[b], 1); pair < bands[b + 1]; ++pair) {
            for (int k = 2 * pair; k < 2 * pair + 2; ++k)
                coefficients[static_cast<std::size_t>(k)] = values[static_cast<std::size_t>(k)] * quantisers[b];
        }
    }

    InverseTransform();

    if (!first) {
        for (int i = 0; i < overlap; ++i) {
            const int blended = previous[static_cast<std::size_t>(i)] * (overlap - i) + samples[static_cast<std::size_t>(i)] * i;
            samples[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(FloorDivide(blended, overlap));
        }
    }
    first = false;
    std::copy(samples.end() - overlap, samples.end(), previous.begin());
}

// The inverse real DFT of the coefficients (values 2j and 2j + 1 the real and imaginary parts of frequency j, values 0
// and 1 the real ends 0 and frameSize / 2), through a complex DFT of half the size: even samples as the real parts,
// odd ones as the imaginary.
void AudioDecoder::InverseTransform() {
    const int half = frameSize / 2;
    auto spectrum = [&](int j) {
        if (j == 0) return std::complex<double>(coefficients[0] / 2.0, 0.0);
        if (j == half) return std::complex<double>(coefficients[1] / 2.0, 0.0);
        const auto at = static_cast<std::size_t>(2 * j);
        return std::complex<double>(coefficients[at] / 2.0, -coefficients[at + 1] / 2.0);
    };
    for (int j = 0; j < half; ++j) {
        const std::complex<double> a = spectrum(j);
        const std::complex<double> b = std::conj(spectrum(half - j));
        const std::complex<double> even = a + b;
        const std::complex<double> odd = (a - b) * twiddles[static_cast<std::size_t>(j)];
        work[static_cast<std::size_t>(j)] = even + std::complex<double>(0.0, 1.0) * odd;
    }

    // In place radix 2, positive exponent, unscaled.
    for (int i = 1, j = 0; i < half; ++i) {
        int bit = half >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j |= bit;
        if (i < j) std::swap(work[static_cast<std::size_t>(i)], work[static_cast<std::size_t>(j)]);
    }
    for (int length = 2; length <= half; length <<= 1) {
        const int stride = frameSize / length;   // twiddles holds e^(2 pi i k / frameSize)
        for (int start = 0; start < half; start += length) {
            for (int k = 0; k < length / 2; ++k) {
                std::complex<double>& u = work[static_cast<std::size_t>(start + k)];
                std::complex<double>& v = work[static_cast<std::size_t>(start + k + length / 2)];
                const std::complex<double> t = v * twiddles[static_cast<std::size_t>(k * stride)];
                v = u - t;
                u += t;
            }
        }
    }

    for (int m = 0; m < half; ++m) {
        const std::complex<double> z = work[static_cast<std::size_t>(m)];
        for (int k = 0; k < 2; ++k) {
            const double x = (k == 0 ? z.real() : z.imag()) * scale;
            const long rounded = std::lrint(std::clamp(x, -32768.0, 32767.0));
            samples[static_cast<std::size_t>(2 * m + k)] = static_cast<std::int16_t>(rounded);
        }
    }
}

} // namespace bink
