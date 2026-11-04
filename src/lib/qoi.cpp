// The QOI codec (qoi.h).
#include "qoi.h"

#include <cstring>

namespace lib {

namespace {

constexpr std::uint8_t kOpIndex = 0x00, kOpDiff = 0x40, kOpLuma = 0x80, kOpRun = 0xC0, kOpRgb = 0xFE, kOpRgba = 0xFF;
constexpr std::uint8_t kTagMask = 0xC0;
constexpr int kHeaderBytes = 14;
constexpr std::uint8_t kEnd[8] = {0, 0, 0, 0, 0, 0, 0, 1};
constexpr int kLongestRun = 62;

struct Pixel {
    std::uint8_t r = 0, g = 0, b = 0, a = 0;
    bool operator==(const Pixel&) const = default;
};

int Hash(const Pixel& p) { return (p.r * 3 + p.g * 5 + p.b * 7 + p.a * 11) % 64; }

void Put32(std::vector<std::uint8_t>& out, std::uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8) out.push_back(static_cast<std::uint8_t>(v >> shift));
}

std::uint32_t Get32(const std::uint8_t* p) {
    return (static_cast<std::uint32_t>(p[0]) << 24) | (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | p[3];
}

} // namespace

std::vector<std::uint8_t> EncodeQoi(const std::uint8_t* rgba, int width, int height, int channels) {
    std::vector<std::uint8_t> out;
    if (!rgba || width <= 0 || height <= 0 || (channels != 3 && channels != 4)) return out;
    const std::size_t count = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    out.reserve(kHeaderBytes + count + sizeof(kEnd));
    out.insert(out.end(), {'q', 'o', 'i', 'f'});
    Put32(out, static_cast<std::uint32_t>(width));
    Put32(out, static_cast<std::uint32_t>(height));
    out.push_back(static_cast<std::uint8_t>(channels));
    out.push_back(0);   // sRGB with linear alpha
    Pixel index[64];
    Pixel previous{0, 0, 0, 255};
    int run = 0;
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint8_t* s = rgba + i * 4;
        const Pixel p{s[0], s[1], s[2], channels == 4 ? s[3] : std::uint8_t{255}};
        if (p == previous) {
            ++run;
            if (run == kLongestRun || i + 1 == count) {
                out.push_back(static_cast<std::uint8_t>(kOpRun | (run - 1)));
                run = 0;
            }
            continue;
        }
        if (run > 0) {
            out.push_back(static_cast<std::uint8_t>(kOpRun | (run - 1)));
            run = 0;
        }
        const int slot = Hash(p);
        if (index[slot] == p) {
            out.push_back(static_cast<std::uint8_t>(kOpIndex | slot));
        } else {
            index[slot] = p;
            if (p.a == previous.a) {
                const int dr = static_cast<std::int8_t>(p.r - previous.r);
                const int dg = static_cast<std::int8_t>(p.g - previous.g);
                const int db = static_cast<std::int8_t>(p.b - previous.b);
                const int drg = dr - dg, dbg = db - dg;
                if (dr >= -2 && dr <= 1 && dg >= -2 && dg <= 1 && db >= -2 && db <= 1) {
                    out.push_back(static_cast<std::uint8_t>(kOpDiff | ((dr + 2) << 4) | ((dg + 2) << 2) | (db + 2)));
                } else if (drg >= -8 && drg <= 7 && dg >= -32 && dg <= 31 && dbg >= -8 && dbg <= 7) {
                    out.push_back(static_cast<std::uint8_t>(kOpLuma | (dg + 32)));
                    out.push_back(static_cast<std::uint8_t>(((drg + 8) << 4) | (dbg + 8)));
                } else {
                    out.insert(out.end(), {kOpRgb, p.r, p.g, p.b});
                }
            } else {
                out.insert(out.end(), {kOpRgba, p.r, p.g, p.b, p.a});
            }
        }
        previous = p;
    }
    out.insert(out.end(), std::begin(kEnd), std::end(kEnd));
    return out;
}

bool DecodeQoi(const std::uint8_t* data, std::size_t size, int mostSide, int* width, int* height, std::vector<std::uint8_t>* rgba) {
    if (!data || size < kHeaderBytes + sizeof(kEnd) || std::memcmp(data, "qoif", 4) != 0) return false;
    const std::uint32_t w = Get32(data + 4), h = Get32(data + 8);
    const std::uint8_t channels = data[12];
    if (w == 0 || h == 0 || w > static_cast<std::uint32_t>(mostSide) || h > static_cast<std::uint32_t>(mostSide) ||
        (channels != 3 && channels != 4))
        return false;
    const std::size_t count = static_cast<std::size_t>(w) * h;
    rgba->assign(count * 4, 0);
    const std::size_t chunks = size - sizeof(kEnd);
    std::size_t at = kHeaderBytes;
    Pixel index[64];
    Pixel p{0, 0, 0, 255};
    int run = 0;
    for (std::size_t i = 0; i < count; ++i) {
        if (run > 0) {
            --run;
        } else {
            if (at >= chunks) return false;
            const std::uint8_t b1 = data[at++];
            if (b1 == kOpRgb) {
                if (chunks - at < 3) return false;
                p.r = data[at];
                p.g = data[at + 1];
                p.b = data[at + 2];
                at += 3;
            } else if (b1 == kOpRgba) {
                if (chunks - at < 4) return false;
                p = Pixel{data[at], data[at + 1], data[at + 2], data[at + 3]};
                at += 4;
            } else if ((b1 & kTagMask) == kOpIndex) {
                p = index[b1];
            } else if ((b1 & kTagMask) == kOpDiff) {
                p.r = static_cast<std::uint8_t>(p.r + ((b1 >> 4) & 3) - 2);
                p.g = static_cast<std::uint8_t>(p.g + ((b1 >> 2) & 3) - 2);
                p.b = static_cast<std::uint8_t>(p.b + (b1 & 3) - 2);
            } else if ((b1 & kTagMask) == kOpLuma) {
                if (at >= chunks) return false;
                const std::uint8_t b2 = data[at++];
                const int dg = (b1 & 0x3F) - 32;
                p.r = static_cast<std::uint8_t>(p.r + dg - 8 + ((b2 >> 4) & 0x0F));
                p.g = static_cast<std::uint8_t>(p.g + dg);
                p.b = static_cast<std::uint8_t>(p.b + dg - 8 + (b2 & 0x0F));
            } else {
                run = b1 & 0x3F;
            }
            index[Hash(p)] = p;
        }
        std::uint8_t* o = rgba->data() + i * 4;
        o[0] = p.r;
        o[1] = p.g;
        o[2] = p.b;
        o[3] = channels == 4 ? p.a : 255;
    }
    *width = static_cast<int>(w);
    *height = static_cast<int>(h);
    return true;
}

} // namespace lib
