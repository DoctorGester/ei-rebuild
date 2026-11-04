// The save pictures (save_picture.h).
#include "save_picture.h"

#include "lib/bytes.h"
#include "lib/qoi.h"

#include <algorithm>
#include <cstring>
#include <utility>

namespace save_picture {

namespace {

constexpr std::uint8_t kMagic[4] = {'E', 'I', 'P', 'C'};
constexpr std::uint32_t kVersion = 1;
constexpr std::size_t kTrailerHeader = 20;

} // namespace

Picture Shrink(const Picture& picture, int width, int height) {
    Picture out;
    if (picture.Empty() || width <= 0 || height <= 0) return out;
    width = std::min(width, picture.width);
    height = std::min(height, picture.height);
    out.width = width;
    out.height = height;
    out.rgba.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4);
    // Each output column's source columns [x * sw / w, (x + 1) * sw / w), and the same for rows.
    std::vector<int> columns(static_cast<std::size_t>(width) + 1);
    for (int x = 0; x <= width; ++x)
        columns[static_cast<std::size_t>(x)] = static_cast<int>(static_cast<std::int64_t>(x) * picture.width / width);
    std::vector<std::uint32_t> sums(static_cast<std::size_t>(width) * 3);
    for (int y = 0; y < height; ++y) {
        const int top = static_cast<int>(static_cast<std::int64_t>(y) * picture.height / height);
        const int bottom = static_cast<int>(static_cast<std::int64_t>(y + 1) * picture.height / height);
        std::fill(sums.begin(), sums.end(), 0u);
        for (int sy = top; sy < bottom; ++sy) {
            const std::uint8_t* row = picture.rgba.data() + static_cast<std::size_t>(sy) * picture.width * 4;
            for (int x = 0; x < width; ++x) {
                std::uint32_t* s = &sums[static_cast<std::size_t>(x) * 3];
                for (int sx = columns[static_cast<std::size_t>(x)]; sx < columns[static_cast<std::size_t>(x) + 1]; ++sx) {
                    s[0] += row[sx * 4];
                    s[1] += row[sx * 4 + 1];
                    s[2] += row[sx * 4 + 2];
                }
            }
        }
        std::uint8_t* o = out.rgba.data() + static_cast<std::size_t>(y) * width * 4;
        for (int x = 0; x < width; ++x, o += 4) {
            const std::uint32_t n = static_cast<std::uint32_t>(bottom - top) *
                                    static_cast<std::uint32_t>(columns[static_cast<std::size_t>(x) + 1] - columns[static_cast<std::size_t>(x)]);
            const std::uint32_t* s = &sums[static_cast<std::size_t>(x) * 3];
            for (int c = 0; c < 3; ++c) o[c] = static_cast<std::uint8_t>((s[c] + n / 2) / n);
            o[3] = 255;
        }
    }
    return out;
}

Picture Fit(const Picture& picture, int width, int height) {
    if (picture.width <= width && picture.height <= height) return picture;
    return Shrink(picture, width, height);
}

std::vector<std::uint8_t> ShotFile(const Picture& screen, const Picture& shown) {
    std::vector<std::uint8_t> out(kOldBytes, 0);
    if (!screen.Empty()) {
        for (int y = 0; y < kOldHeight; ++y) {
            const int sy = (2 * y + 1) * screen.height / (2 * kOldHeight);
            for (int x = 0; x < kOldWidth; ++x) {
                const int sx = (2 * x + 1) * screen.width / (2 * kOldWidth);
                const std::uint8_t* p = screen.rgba.data() + (static_cast<std::size_t>(sy) * screen.width + sx) * 4;
                const unsigned v = ((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3);
                std::uint8_t* o = out.data() + (static_cast<std::size_t>(y) * kOldWidth + x) * 2;
                o[0] = static_cast<std::uint8_t>(v);
                o[1] = static_cast<std::uint8_t>(v >> 8);
            }
        }
    }
    if (shown.Empty()) return out;
    const std::vector<std::uint8_t> qoi = lib::EncodeQoi(shown.rgba.data(), shown.width, shown.height, 3);
    std::uint8_t header[kTrailerHeader];
    std::memcpy(header, kMagic, sizeof(kMagic));
    lib::Store<std::uint32_t>(header + 4, kVersion);
    lib::Store<std::uint32_t>(header + 8, static_cast<std::uint32_t>(shown.width));
    lib::Store<std::uint32_t>(header + 12, static_cast<std::uint32_t>(shown.height));
    lib::Store<std::uint32_t>(header + 16, static_cast<std::uint32_t>(qoi.size()));
    out.insert(out.end(), std::begin(header), std::end(header));
    out.insert(out.end(), qoi.begin(), qoi.end());
    return out;
}

Picture OldPicture(const std::uint8_t* bytes, std::size_t size) {
    Picture out;
    out.width = kOldWidth;
    out.height = kOldHeight;
    const std::size_t count = static_cast<std::size_t>(kOldWidth) * kOldHeight;
    out.rgba.assign(count * 4, 0);
    const std::size_t have = std::min(size, kOldBytes) / 2;
    for (std::size_t i = 0; i < count; ++i) {
        std::uint8_t* o = &out.rgba[i * 4];
        o[3] = 255;
        if (i >= have) continue;
        const unsigned c = bytes[i * 2] | (bytes[i * 2 + 1] << 8);
        const unsigned r = c >> 11, g = (c >> 5) & 63, b = c & 31;
        o[0] = static_cast<std::uint8_t>((r << 3) | (r >> 2));
        o[1] = static_cast<std::uint8_t>((g << 2) | (g >> 4));
        o[2] = static_cast<std::uint8_t>((b << 3) | (b >> 2));
    }
    return out;
}

bool TrailerPicture(const std::uint8_t* bytes, std::size_t size, Picture* picture) {
    if (!bytes || size < kOldBytes + kTrailerHeader) return false;
    const std::uint8_t* t = bytes + kOldBytes;
    if (std::memcmp(t, kMagic, sizeof(kMagic)) != 0 || lib::Load<std::uint32_t>(t + 4) != kVersion) return false;
    const std::uint32_t width = lib::Load<std::uint32_t>(t + 8), height = lib::Load<std::uint32_t>(t + 12), length = lib::Load<std::uint32_t>(t + 16);
    if (length > size - kOldBytes - kTrailerHeader) return false;
    Picture out;
    if (!lib::DecodeQoi(t + kTrailerHeader, length, kMostSide, &out.width, &out.height, &out.rgba)) return false;
    if (static_cast<std::uint32_t>(out.width) != width || static_cast<std::uint32_t>(out.height) != height) return false;
    *picture = std::move(out);
    return true;
}

Picture ReadShot(const std::uint8_t* bytes, std::size_t size) {
    Picture picture;
    if (TrailerPicture(bytes, size, &picture)) return picture;
    return OldPicture(bytes, size);
}

} // namespace save_picture
