// The PNG writer: png.h.
#include "lib/png.h"

#include "miniz/miniz.h"

#include <cstdlib>
#include <string_view>
#include <vector>

namespace png {
namespace {

void PutBigEndian(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(value >> shift));
}

// A chunk: its length, type, data and the CRC of type and data.
void PutChunk(std::string& out, std::string_view type, std::string_view data) {
    PutBigEndian(out, static_cast<std::uint32_t>(data.size()));
    const std::size_t start = out.size();
    out.append(type);
    out.append(data);
    const auto* crcBytes = reinterpret_cast<const unsigned char*>(out.data() + start);
    PutBigEndian(out, static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, crcBytes, out.size() - start)));
}

int Paeth(int left, int up, int upLeft) {
    const int estimate = left + up - upLeft;
    const int toLeft = std::abs(estimate - left), toUp = std::abs(estimate - up),
              toUpLeft = std::abs(estimate - upLeft);
    if (toLeft <= toUp && toLeft <= toUpLeft) return left;
    return toUp <= toUpLeft ? up : upLeft;
}

// The PNG filters: none, sub, up, average, Paeth.
constexpr int kFilters = 5;

// Row `row` filtered by `filter` into `out`; `prior` the row above it (zeros for the first), `step` the bytes of a
// pixel.
void Filter(int filter, const std::uint8_t* row, const std::uint8_t* prior, std::size_t bytes, std::size_t step,
            std::uint8_t* out) {
    for (std::size_t i = 0; i < bytes; ++i) {
        const int left = i >= step ? row[i - step] : 0;
        const int up = prior[i];
        const int upLeft = i >= step ? prior[i - step] : 0;
        int predicted = 0;
        switch (filter) {
        case 1:
            predicted = left;
            break;
        case 2:
            predicted = up;
            break;
        case 3:
            predicted = (left + up) / 2;
            break;
        case 4:
            predicted = Paeth(left, up, upLeft);
            break;
        default:
            break;
        }
        out[i] = static_cast<std::uint8_t>(row[i] - predicted);
    }
}

// What the choice of filter minimises: the filtered bytes' sum as signed values.
std::size_t Cost(const std::uint8_t* bytes, std::size_t count) {
    std::size_t sum = 0;
    for (std::size_t i = 0; i < count; ++i)
        sum += static_cast<std::size_t>(std::abs(static_cast<std::int8_t>(bytes[i])));
    return sum;
}

constexpr std::uint8_t kColourTypes[] = {0, 0, 4, 2, 6};   // by channel count: grey, grey and alpha, RGB, RGBA

}   // namespace

std::string Write(int width, int height, int channels, int bits, std::span<const std::uint16_t> samples) {
    if (width <= 0 || height <= 0 || channels < 1 || channels > 4 || (bits != 8 && bits != 16)) return {};
    const std::size_t sampleBytes = static_cast<std::size_t>(bits / 8);
    const std::size_t step = static_cast<std::size_t>(channels) * sampleBytes;
    const std::size_t rowBytes = static_cast<std::size_t>(width) * step;
    if (samples.size() !=
        static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels))
        return {};

    // The samples as the PNG holds them: big-endian.
    std::vector<std::uint8_t> raw(rowBytes * static_cast<std::size_t>(height));
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (sampleBytes == 2) {
            raw[2 * i] = static_cast<std::uint8_t>(samples[i] >> 8);
            raw[2 * i + 1] = static_cast<std::uint8_t>(samples[i]);
        } else {
            raw[i] = static_cast<std::uint8_t>(samples[i]);
        }
    }
    std::vector<std::uint8_t> filtered;
    filtered.reserve((rowBytes + 1) * static_cast<std::size_t>(height));
    const std::vector<std::uint8_t> zeros(rowBytes, 0);
    std::vector<std::uint8_t> candidate(rowBytes), best(rowBytes);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = raw.data() + static_cast<std::size_t>(y) * rowBytes;
        const std::uint8_t* prior = y > 0 ? row - rowBytes : zeros.data();
        int bestFilter = 0;
        std::size_t bestCost = SIZE_MAX;
        for (int filter = 0; filter < kFilters; ++filter) {
            Filter(filter, row, prior, rowBytes, step, candidate.data());
            const std::size_t cost = Cost(candidate.data(), rowBytes);
            if (cost >= bestCost) continue;
            bestCost = cost;
            bestFilter = filter;
            best.swap(candidate);
        }
        filtered.push_back(static_cast<std::uint8_t>(bestFilter));
        filtered.insert(filtered.end(), best.begin(), best.end());
    }

    mz_ulong packedSize = mz_compressBound(static_cast<mz_ulong>(filtered.size()));
    std::vector<unsigned char> packed(packedSize);
    if (mz_compress2(packed.data(), &packedSize, filtered.data(), static_cast<mz_ulong>(filtered.size()),
                     MZ_DEFAULT_LEVEL) != MZ_OK)
        return {};

    std::string header;
    PutBigEndian(header, static_cast<std::uint32_t>(width));
    PutBigEndian(header, static_cast<std::uint32_t>(height));
    header.push_back(static_cast<char>(bits));
    header.push_back(static_cast<char>(kColourTypes[channels]));
    header.append(3, '\0');   // deflate, the filters above, no interlace
    std::string png("\x89PNG\r\n\x1a\n", 8);
    PutChunk(png, "IHDR", header);
    PutChunk(png, "IDAT", std::string_view(reinterpret_cast<const char*>(packed.data()), packedSize));
    PutChunk(png, "IEND", {});
    return png;
}

}   // namespace png
