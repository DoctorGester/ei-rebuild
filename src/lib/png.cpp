// The PNG writer and reader: png.h.
#include "lib/png.h"

#include "lib/threads.h"
#include "miniz/miniz.h"
#include "wuffs/wuffs_png.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>
#include <vector>

namespace png {
namespace {

void PutBigEndian(std::string& out, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<char>(value >> shift));
}

std::uint32_t BigEndian(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) << 24 | static_cast<std::uint32_t>(p[1]) << 16 |
           static_cast<std::uint32_t>(p[2]) << 8 | p[3];
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

// The row `row` (filtered by `filter`) back in place; `prior` the row above it unfiltered (null: none, only none and
// sub then).
void Unfilter(int filter, std::uint8_t* row, const std::uint8_t* prior, std::size_t bytes, std::size_t step) {
    switch (filter) {
    case 1:
        for (std::size_t i = step; i < bytes; ++i) row[i] = static_cast<std::uint8_t>(row[i] + row[i - step]);
        break;
    case 2:
        for (std::size_t i = 0; i < bytes; ++i) row[i] = static_cast<std::uint8_t>(row[i] + prior[i]);
        break;
    case 3:
        for (std::size_t i = 0; i < step; ++i) row[i] = static_cast<std::uint8_t>(row[i] + prior[i] / 2);
        for (std::size_t i = step; i < bytes; ++i) row[i] = static_cast<std::uint8_t>(row[i] + (row[i - step] + prior[i]) / 2);
        break;
    case 4:
        for (std::size_t i = 0; i < step; ++i) row[i] = static_cast<std::uint8_t>(row[i] + prior[i]);
        for (std::size_t i = step; i < bytes; ++i)
            row[i] = static_cast<std::uint8_t>(row[i] + Paeth(row[i - step], prior[i], prior[i - step]));
        break;
    default:
        break;
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

constexpr std::string_view kSegmentsChunk = "eiSG";
constexpr std::size_t kSegmentBytes = 256 * 1024;   // a segment's filtered rows, about
constexpr std::size_t kZlibHeader = 2, kAdler = 4;

// Adler-32 of two runs one after the other from the Adler-32 of each (`second` of `secondBytes` bytes): zlib's
// adler32_combine.
std::uint32_t CombineAdler(std::uint32_t first, std::uint32_t second, std::size_t secondBytes) {
    constexpr std::uint32_t kBase = 65521;
    const std::uint32_t rem = static_cast<std::uint32_t>(secondBytes % kBase);
    std::uint32_t sum1 = first & 0xFFFF;
    std::uint32_t sum2 = static_cast<std::uint32_t>((static_cast<std::uint64_t>(rem) * sum1) % kBase);
    sum1 += (second & 0xFFFF) + kBase - 1;
    sum2 += ((first >> 16) & 0xFFFF) + ((second >> 16) & 0xFFFF) + kBase - rem;
    if (sum1 >= kBase) sum1 -= kBase;
    if (sum1 >= kBase) sum1 -= kBase;
    if (sum2 >= (kBase << 1)) sum2 -= (kBase << 1);
    if (sum2 >= kBase) sum2 -= kBase;
    return sum1 | (sum2 << 16);
}

// The PNG of `raw` (its rows as the PNG holds them: big-endian samples), in segments when it has more than one.
std::string WriteRows(int width, int height, int channels, int bits, const std::uint8_t* raw) {
    const std::size_t step = static_cast<std::size_t>(channels) * static_cast<std::size_t>(bits / 8);
    const std::size_t rowBytes = static_cast<std::size_t>(width) * step;
    const int rowsASegment = static_cast<int>(std::max<std::size_t>(1, kSegmentBytes / (rowBytes + 1)));
    const int segments = (height + rowsASegment - 1) / rowsASegment;

    struct Segment {
        std::string packed;
        std::uint32_t adler = 1;
        std::size_t filteredBytes = 0;
        bool ok = false;
    };
    std::vector<Segment> made(static_cast<std::size_t>(segments));
    const int flags = static_cast<int>(tdefl_create_comp_flags_from_zip_params(MZ_DEFAULT_LEVEL, -15, MZ_DEFAULT_STRATEGY));
    lib::OnThreads(segments, [&](int s) {
        const int first = s * rowsASegment, last = std::min(height, first + rowsASegment);
        std::vector<std::uint8_t> filtered;
        filtered.reserve((rowBytes + 1) * static_cast<std::size_t>(last - first));
        std::vector<std::uint8_t> candidate(rowBytes), best(rowBytes);
        const std::vector<std::uint8_t> zeros(rowBytes, 0);
        for (int y = first; y < last; ++y) {
            const std::uint8_t* row = raw + static_cast<std::size_t>(y) * rowBytes;
            // A segment's first row is filtered without the row above it (none or sub), so the segment unfilters on
            // its own.
            const std::uint8_t* prior = y == first ? zeros.data() : row - rowBytes;
            const int filters = y == first ? 2 : kFilters;
            int bestFilter = 0;
            std::size_t bestCost = SIZE_MAX;
            for (int filter = 0; filter < filters; ++filter) {
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
        Segment& out = made[static_cast<std::size_t>(s)];
        out.filteredBytes = filtered.size();
        out.adler = static_cast<std::uint32_t>(mz_adler32(MZ_ADLER32_INIT, filtered.data(), filtered.size()));
        // A fresh compressor a segment: nothing refers back into the one before; each ends on a byte (a full flush),
        // the last with the final block.
        tdefl_compressor* compressor = tdefl_compressor_alloc();
        if (!compressor) return;
        const tdefl_put_buf_func_ptr put = [](const void* data, int length, void* user) -> mz_bool {
            static_cast<std::string*>(user)->append(static_cast<const char*>(data), static_cast<std::size_t>(length));
            return MZ_TRUE;
        };
        out.ok = tdefl_init(compressor, put, &out.packed, flags) == TDEFL_STATUS_OKAY &&
                 tdefl_compress_buffer(compressor, filtered.data(), filtered.size(),
                                       s + 1 == segments ? TDEFL_FINISH : TDEFL_FULL_FLUSH) ==
                     (s + 1 == segments ? TDEFL_STATUS_DONE : TDEFL_STATUS_OKAY);
        tdefl_compressor_free(compressor);
    });

    std::string stream("\x78\x9c", kZlibHeader);   // deflate with a 32 KB window, the default level
    std::string offsets;
    PutBigEndian(offsets, static_cast<std::uint32_t>(rowsASegment));
    std::uint32_t adler = MZ_ADLER32_INIT;
    for (const Segment& segment : made) {
        if (!segment.ok) return {};
        PutBigEndian(offsets, static_cast<std::uint32_t>(stream.size()));
        stream += segment.packed;
        adler = CombineAdler(adler, segment.adler, segment.filteredBytes);
    }
    PutBigEndian(stream, adler);

    std::string header;
    PutBigEndian(header, static_cast<std::uint32_t>(width));
    PutBigEndian(header, static_cast<std::uint32_t>(height));
    header.push_back(static_cast<char>(bits));
    header.push_back(static_cast<char>(kColourTypes[channels]));
    header.append(3, '\0');   // deflate, the filters above, no interlace
    std::string png("\x89PNG\r\n\x1a\n", 8);
    PutChunk(png, "IHDR", header);
    if (segments > 1) PutChunk(png, kSegmentsChunk, offsets);
    PutChunk(png, "IDAT", stream);
    PutChunk(png, "IEND", {});
    return png;
}

bool FitsWriter(int width, int height, int channels, int bits, std::size_t samples) {
    return width > 0 && height > 0 && channels >= 1 && channels <= 4 && (bits == 8 || bits == 16) &&
           samples == static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * static_cast<std::size_t>(channels);
}

// The pixels of a segmented PNG (png.h) whose rows hold 8- or 16-bit grey, grey and alpha, RGB or RGBA, as RGBA of
// `Sample`'s size (a 16-bit sample's high byte as a byte; a byte b as b * 257 in 16 bits); false for another PNG, or one
// whose segments fail on their own or do not add up to its stream's Adler-32.
template <typename Sample>
bool ReadSegments(std::string_view png, int* width, int* height, std::vector<Sample>* rgba) {
    const auto* bytes = reinterpret_cast<const unsigned char*>(png.data());
    if (png.size() < 8 + 25 || png.substr(0, 8) != std::string_view("\x89PNG\r\n\x1a\n", 8)) return false;
    std::uint32_t w = 0, h = 0;
    int bits = 0, colourType = -1, interlace = 0;
    bool transparency = false;
    std::string_view segmentsData;
    std::vector<std::string_view> idats;
    for (std::size_t at = 8; at + 12 <= png.size();) {
        const std::uint32_t length = BigEndian(bytes + at);
        if (length > png.size() - at - 12) return false;
        const std::string_view type = png.substr(at + 4, 4), data = png.substr(at + 8, length);
        const auto* d = bytes + at + 8;
        if (type == "IHDR" && length == 13) {
            w = BigEndian(d);
            h = BigEndian(d + 4);
            bits = d[8];
            colourType = d[9];
            interlace = d[12];
        } else if (type == "tRNS") {
            transparency = true;
        } else if (type == kSegmentsChunk) {
            segmentsData = data;
        } else if (type == "IDAT") {
            idats.push_back(data);
        } else if (type == "IEND") {
            break;
        }
        at += 12 + length;
    }
    const int channels = colourType == 0 ? 1 : colourType == 4 ? 2 : colourType == 2 ? 3 : colourType == 6 ? 4 : 0;
    constexpr std::uint32_t kMostSide = 1u << 15;
    if (segmentsData.size() < 8 || segmentsData.size() % 4 || channels == 0 || (bits != 8 && bits != 16) || interlace ||
        transparency || w == 0 || h == 0 || w > kMostSide || h > kMostSide || idats.empty())
        return false;
    std::string joined;
    std::string_view stream = idats.front();
    if (idats.size() > 1) {
        for (std::string_view idat : idats) joined.append(idat);
        stream = joined;
    }
    const auto* segmentBytes = reinterpret_cast<const unsigned char*>(segmentsData.data());
    const std::uint32_t rowsASegment = BigEndian(segmentBytes);
    const std::size_t segments = segmentsData.size() / 4 - 1;
    if (rowsASegment == 0 || segments != (h + rowsASegment - 1) / rowsASegment || stream.size() < kZlibHeader + kAdler)
        return false;
    std::vector<std::size_t> starts(segments + 1, stream.size() - kAdler);
    for (std::size_t s = 0; s < segments; ++s) starts[s] = BigEndian(segmentBytes + 4 + 4 * s);
    if (starts[0] != kZlibHeader) return false;
    for (std::size_t s = 0; s < segments; ++s)
        if (starts[s + 1] < starts[s]) return false;

    const std::size_t sampleBytes = static_cast<std::size_t>(bits / 8);
    const std::size_t step = static_cast<std::size_t>(channels) * sampleBytes;
    const std::size_t rowBytes = w * step;
    std::vector<std::uint8_t> filtered((rowBytes + 1) * h);
    std::vector<std::uint32_t> adlers(segments, 0);
    std::vector<char> read(segments, 0);
    rgba->resize(static_cast<std::size_t>(w) * h * 4);
    const auto* streamBytes = reinterpret_cast<const unsigned char*>(stream.data());
    lib::OnThreads(static_cast<int>(segments), [&](int s) {
        const std::size_t first = static_cast<std::size_t>(s) * rowsASegment;
        const std::size_t last = std::min<std::size_t>(h, first + rowsASegment);
        std::uint8_t* rows = filtered.data() + first * (rowBytes + 1);
        const std::size_t size = (last - first) * (rowBytes + 1);
        const std::size_t at = starts[static_cast<std::size_t>(s)];
        if (!ei_inflate(streamBytes + at, starts[static_cast<std::size_t>(s) + 1] - at, rows, size)) return;
        adlers[static_cast<std::size_t>(s)] = static_cast<std::uint32_t>(mz_adler32(MZ_ADLER32_INIT, rows, size));
        const std::uint8_t* prior = nullptr;
        for (std::size_t y = first; y < last; ++y) {
            std::uint8_t* row = filtered.data() + y * (rowBytes + 1);
            const int filter = row[0];
            if (filter >= kFilters || (!prior && filter > 1)) return;
            Unfilter(filter, row + 1, prior, rowBytes, step);
            prior = row + 1;
            Sample* out = rgba->data() + y * w * 4;
            // A sample's bytes are big-endian; grey to R, G and B; no alpha opaque.
            auto sample = [&](const std::uint8_t* p) {
                if constexpr (sizeof(Sample) == 1) return static_cast<Sample>(p[0]);
                else return static_cast<Sample>(sampleBytes == 2 ? p[0] << 8 | p[1] : p[0] * 257);
            };
            for (std::size_t x = 0; x < w; ++x, out += 4) {
                const std::uint8_t* p = row + 1 + x * step;
                const bool colour = channels >= 3;
                out[0] = sample(p);
                out[1] = sample(colour ? p + sampleBytes : p);
                out[2] = sample(colour ? p + 2 * sampleBytes : p);
                out[3] = channels == 2   ? sample(p + sampleBytes)
                         : channels == 4 ? sample(p + 3 * sampleBytes)
                                         : static_cast<Sample>(sizeof(Sample) == 1 ? 255 : 65535);
            }
        }
        read[static_cast<std::size_t>(s)] = 1;
    });
    std::uint32_t adler = MZ_ADLER32_INIT;
    for (std::size_t s = 0; s < segments; ++s) {
        if (!read[s]) return false;
        const std::size_t first = s * rowsASegment, last = std::min<std::size_t>(h, first + rowsASegment);
        adler = CombineAdler(adler, adlers[s], (last - first) * (rowBytes + 1));
    }
    if (adler != BigEndian(streamBytes + stream.size() - kAdler)) return false;
    *width = static_cast<int>(w);
    *height = static_cast<int>(h);
    return true;
}

}   // namespace

std::string Write(int width, int height, int channels, int bits, std::span<const std::uint16_t> samples) {
    if (!FitsWriter(width, height, channels, bits, samples.size())) return {};
    // The samples as the PNG holds them: big-endian.
    const std::size_t sampleBytes = static_cast<std::size_t>(bits / 8);
    std::vector<std::uint8_t> raw(samples.size() * sampleBytes);
    for (std::size_t i = 0; i < samples.size(); ++i) {
        if (sampleBytes == 2) {
            raw[2 * i] = static_cast<std::uint8_t>(samples[i] >> 8);
            raw[2 * i + 1] = static_cast<std::uint8_t>(samples[i]);
        } else {
            raw[i] = static_cast<std::uint8_t>(samples[i]);
        }
    }
    return WriteRows(width, height, channels, bits, raw.data());
}

std::string Write(int width, int height, int channels, std::span<const std::uint8_t> samples) {
    if (!FitsWriter(width, height, channels, 8, samples.size())) return {};
    return WriteRows(width, height, channels, 8, samples.data());
}

namespace {

template <typename Sample>
bool Read(std::string_view png, int* width, int* height, std::vector<Sample>* rgba, std::string* error) {
    if (ReadSegments(png, width, height, rgba)) return true;
    struct Out {
        int width = 0, height = 0;
        std::vector<Sample>* rgba = nullptr;
    } out{0, 0, rgba};
    const ei_png_allocate allocate = [](void* context, unsigned w, unsigned h) -> unsigned char* {
        Out& o = *static_cast<Out*>(context);
        constexpr unsigned kMostSide = 1u << 15;
        if (w > kMostSide || h > kMostSide) return nullptr;
        o.width = static_cast<int>(w);
        o.height = static_cast<int>(h);
        o.rgba->resize(static_cast<std::size_t>(w) * h * 4);
        return reinterpret_cast<unsigned char*>(o.rgba->data());
    };
    const char* failed = ei_png_decode(reinterpret_cast<const unsigned char*>(png.data()), png.size(),
                                       sizeof(Sample) == 2, allocate, &out);
    if (failed) {
        *error = failed;
        rgba->clear();
        return false;
    }
    *width = out.width;
    *height = out.height;
    return true;
}

}   // namespace

bool ReadRgba(std::string_view png, int* width, int* height, std::vector<std::uint8_t>* rgba, std::string* error) {
    return Read(png, width, height, rgba, error);
}

bool ReadRgba16(std::string_view png, int* width, int* height, std::vector<std::uint16_t>* rgba, std::string* error) {
    return Read(png, width, height, rgba, error);
}

}   // namespace png
