// Bink 1 video (revision i): each plane of a frame is a grid of 8x8 blocks, coded in rows. The values the blocks need
// (block types, colours, patterns, motion vectors, DC values, run lengths) come in bundles, each read in runs of
// several blocks and coded with one of 16 fixed prefix codes; the DCT and residue coefficients and a few small numbers
// come from the bit stream directly. Written from the format's description (multimedia.cx: Bink Video, Kostya
// Shishkov's notes), the details it leaves out from the game's binkw32.dll, and checked frame for frame against an
// independent decoder (docs/notes/platform.md, "Movies").
#include "platform/bink/bink_video.h"

#include "platform/bink/bink_tables.h"
#include "platform/bink/bit_reader.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <utility>

namespace platform::bink {

namespace {

enum BlockType {
    kSkip = 0,      // the previous frame's block
    kScaled = 1,    // a 16x16 block: an 8x8 one of the subtype below doubled
    kMotion = 2,    // the previous frame's block moved
    kRun = 3,       // runs of one colour and of single colours along a pattern
    kResidue = 4,   // moved, plus a residue
    kIntra = 5,     // DCT
    kFill = 6,      // one colour
    kInter = 7,     // moved, plus a DCT residual
    kPattern = 8,   // two colours by a bit pattern
    kRaw = 9,       // 64 colours
};

// ---- Prefix codes ----

// For each of the 16 codes, indexed by the next 7 bits: the symbol index and its code's length.
struct CodeEntry {
    std::uint8_t index;
    std::uint8_t length;
};
struct CodeTables {
    CodeEntry entries[16][128];
};

const CodeTables& Codes() {
    static const CodeTables tables = [] {
        CodeTables t{};
        for (int tree = 0; tree < 16; ++tree) {
            for (int index = 0; index < 16; ++index) {
                const HuffmanCode& code = kTreeCodes[tree][index];
                for (int rest = 0; rest < 128 >> code.length; ++rest)
                    t.entries[tree][code.bits | rest << code.length] = {static_cast<std::uint8_t>(index), code.length};
            }
        }
        return t;
    }();
    return tables;
}

// Two runs of `size` symbols merged into `to`: a bit per symbol picks the second run's next (1) or the first's.
void Merge(BitReader& bits, std::uint8_t* to, const std::uint8_t* first, const std::uint8_t* second, int size) {
    int left = size, right = size;
    do {
        if (bits.Bit()) {
            *to++ = *second++;
            --right;
        } else {
            *to++ = *first++;
            --left;
        }
    } while (left && right);
    while (left-- > 0) *to++ = *first++;
    while (right-- > 0) *to++ = *second++;
}

// One of the 16 codes and the 16 symbols its indices stand for, both read at the start of a plane.
class Tree {
public:
    void Read(BitReader& bits) {
        code = static_cast<int>(bits.Bits(4));
        for (int i = 0; i < 16; ++i) symbols[i] = static_cast<std::uint8_t>(i);
        if (code == 0) return;
        if (bits.Bit()) {
            // A few symbols listed first, the others after them in order.
            const int listed = static_cast<int>(bits.Bits(3)) + 1;
            bool used[16] = {};
            for (int i = 0; i < listed; ++i) {
                symbols[i] = static_cast<std::uint8_t>(bits.Bits(4));
                used[symbols[i]] = true;
            }
            int at = listed;
            for (int s = 0; s < 16 && at < 16; ++s)
                if (!used[s]) symbols[at++] = static_cast<std::uint8_t>(s);
            return;
        }
        // Shuffled: pairs swapped or not, then merged into runs of 4, 8 and 16 up to the depth.
        const int depth = static_cast<int>(bits.Bits(2));
        std::uint8_t merged[16];
        for (int size = 1; size <= 1 << depth; size <<= 1) {
            for (int start = 0; start < 16; start += 2 * size)
                Merge(bits, merged + start, symbols + start, symbols + start + size, size);
            std::memcpy(symbols, merged, sizeof(symbols));
        }
    }
    int Decode(BitReader& bits) const {
        const CodeEntry entry = Codes().entries[code][bits.Peek(7)];
        bits.Skip(entry.length);
        return symbols[entry.index];
    }

private:
    int code = 0;
    std::uint8_t symbols[16] = {};
};

// ---- Bundles ----

// Values of one kind for the blocks of a plane, read a run at a time once the blocks have taken the run before. A run
// of 0 ends the plane's values of that kind.
struct Bundle {
    Tree tree;
    int lengthBits = 0;   // the bits of a run's length
    std::vector<std::int16_t> values;
    std::size_t next = 0;
    std::size_t end = 0;
    bool finished = false;

    // A plane `width` pixels wide holding `perBlock` values for each block of a row.
    void Init(int width, int perBlock) {
        lengthBits = std::bit_width(static_cast<unsigned>(0x200 + (width >> 3) * perBlock - 1));
        values.assign((std::size_t{1} << lengthBits) + 64, 0);
        next = end = 0;
        finished = false;
    }
    // The length of the next run, when the run before is used up (0 when none is due or none comes).
    int Start(BitReader& bits) {
        if (finished || next != end) return 0;
        const int count = static_cast<int>(bits.Bits(lengthBits));
        if (count == 0) finished = true;
        next = 0;
        end = static_cast<std::size_t>(count);
        return count;
    }
    void Fill(int value, int count) { std::fill_n(values.begin(), count, static_cast<std::int16_t>(value)); }
    int Next() { return next < values.size() ? values[next++] : 0; }
};

// Block types: values below 12 as they are; 12..15 repeat the last value 4, 8, 12 or 32 times.
void ReadBlockTypes(BitReader& bits, Bundle& b) {
    int count = b.Start(bits);
    if (count == 0) return;
    if (bits.Bit()) {
        b.Fill(static_cast<int>(bits.Bits(4)), count);
        return;
    }
    static constexpr int kRepeats[4] = {4, 8, 12, 32};
    int last = 0;
    std::size_t at = 0;
    while (count > 0) {
        const int value = b.tree.Decode(bits);
        if (value < 12) {
            b.values[at++] = static_cast<std::int16_t>(value);
            last = value;
            --count;
        } else {
            const int repeat = std::min(kRepeats[value - 12], count);
            std::fill_n(b.values.begin() + static_cast<std::ptrdiff_t>(at), repeat, static_cast<std::int16_t>(last));
            at += static_cast<std::size_t>(repeat);
            count -= repeat;
        }
    }
}

// Colours: the high nibble coded by the code chosen by the colour before's high nibble, then the low nibble.
struct Colors {
    Bundle bundle;
    Tree high[16];
    int lastHigh = 0;

    int DecodeOne(BitReader& bits) {
        const int h = high[lastHigh].Decode(bits);
        lastHigh = h;
        return h << 4 | bundle.tree.Decode(bits);
    }
    void Read(BitReader& bits) {
        const int count = bundle.Start(bits);
        if (count == 0) return;
        if (bits.Bit()) {
            bundle.Fill(DecodeOne(bits), count);
            return;
        }
        for (int i = 0; i < count; ++i) bundle.values[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(DecodeOne(bits));
    }
};

// Pattern bytes: the low nibble, then the high one.
void ReadPatterns(BitReader& bits, Bundle& b) {
    const int count = b.Start(bits);
    for (int i = 0; i < count; ++i) {
        const int low = b.tree.Decode(bits);
        b.values[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(low | b.tree.Decode(bits) << 4);
    }
}

// Run lengths less one: nibbles, all the same (raw) or each coded.
void ReadRuns(BitReader& bits, Bundle& b) {
    const int count = b.Start(bits);
    if (count == 0) return;
    if (bits.Bit()) {
        b.Fill(static_cast<int>(bits.Bits(4)), count);
        return;
    }
    for (int i = 0; i < count; ++i) b.values[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(b.tree.Decode(bits));
}

// Motion vectors' parts: nibbles with a sign bit after each that is not 0.
void ReadMotion(BitReader& bits, Bundle& b) {
    const int count = b.Start(bits);
    if (count == 0) return;
    auto sign = [&bits](int v) { return v != 0 && bits.Bit() ? -v : v; };
    if (bits.Bit()) {
        b.Fill(sign(static_cast<int>(bits.Bits(4))), count);
        return;
    }
    for (int i = 0; i < count; ++i) b.values[static_cast<std::size_t>(i)] = static_cast<std::int16_t>(sign(b.tree.Decode(bits)));
}

// DC values: 11 bits (signed: 10 and a sign), then differences in groups of up to 8 sharing a bit width (0: repeats).
void ReadDc(BitReader& bits, Bundle& b, bool isSigned) {
    const int count = b.Start(bits);
    if (count == 0) return;
    auto sign = [&bits](int v) { return v != 0 && bits.Bit() ? -v : v; };
    int value = isSigned ? sign(static_cast<int>(bits.Bits(10))) : static_cast<int>(bits.Bits(11));
    std::size_t at = 0;
    b.values[at++] = static_cast<std::int16_t>(value);
    for (int left = count - 1; left > 0;) {
        const int group = std::min(left, 8);
        const int width = static_cast<int>(bits.Bits(4));
        for (int i = 0; i < group; ++i) {
            if (width) value += sign(static_cast<int>(bits.Bits(width)));
            b.values[at++] = static_cast<std::int16_t>(value);
        }
        left -= group;
    }
}

// ---- Coefficients ----

// The list a block's coefficients are read along: entries of a coefficient (reading order) and a mode, 0 for none.
// Entries are added in front and at the back while it is walked.
class CoefficientList {
public:
    CoefficientList(std::initializer_list<std::uint8_t> start) {
        for (std::uint8_t e : start) entries[last++] = e;
    }
    static std::uint8_t Entry(int coefficient, int mode) { return static_cast<std::uint8_t>(coefficient << 2 | mode); }
    void Prepend(std::uint8_t e) {
        if (first > 0) entries[--first] = e;
    }
    void Append(std::uint8_t e) {
        if (last < kSize) entries[last++] = e;
    }

    static constexpr int kSize = 256;
    std::uint8_t entries[kSize] = {};
    int first = kSize / 2;
    int last = kSize / 2;
};

// A DCT block's coefficients after the DC, bit plane by bit plane from the highest: in each plane the list's entries
// whose bit is set give coefficients their value (the plane's bit and the bits below it, then a sign) or split.
void ReadCoefficients(BitReader& bits, std::int16_t block[64]) {
    std::int16_t coefficients[64] = {};
    CoefficientList list{CoefficientList::Entry(4, 0), CoefficientList::Entry(24, 0), CoefficientList::Entry(44, 0),
                         CoefficientList::Entry(1, 3), CoefficientList::Entry(2, 3), CoefficientList::Entry(3, 3)};
    for (int bit = static_cast<int>(bits.Bits(4)) - 1; bit >= 0; --bit) {
        const int mask = 1 << bit;
        auto value = [&] {
            const int v = mask | static_cast<int>(bits.Bits(bit));
            return static_cast<std::int16_t>(bits.Bit() ? -v : v);
        };
        // Four coefficients from `c`: each either takes its value or waits for a later plane.
        auto quad = [&](int c) {
            for (int k = 0; k < 4; ++k, ++c) {
                if (bits.Bit())
                    list.Prepend(CoefficientList::Entry(c, 3));
                else if (c < 64)
                    coefficients[c] = value();
                else
                    value();
            }
        };
        for (int at = list.first; at < list.last;) {
            const std::uint8_t entry = list.entries[at];
            if (entry == 0 || !bits.Bit()) {
                ++at;
                continue;
            }
            const int c = entry >> 2;
            switch (entry & 3) {
            case 0:
                list.entries[at] = CoefficientList::Entry(c + 4, 1);
                quad(c);
                break;
            case 1:
                list.entries[at] = CoefficientList::Entry(c, 2);
                for (int k = 1; k <= 3; ++k) list.Append(CoefficientList::Entry(c + 4 * k, 2));
                break;
            case 2:
                list.entries[at++] = 0;
                quad(c);
                break;
            default:
                coefficients[c] = value();
                list.entries[at++] = 0;
                break;
            }
        }
    }
    for (int s = 1; s < 64; ++s) block[kScan[s]] = coefficients[s];
}

// A moved block's residue, bit plane by bit plane from the highest: in each plane the coefficients set before may grow
// by the plane's bit, then the list's entries give new ones the bit with a sign, or split. It ends once `masks` + 1
// bits have been given. The bits are 8-bit numbers: an eighth plane's is -128.
void ReadResidue(BitReader& bits, std::int8_t residue[64], int masks) {
    std::memset(residue, 0, 64);
    std::uint8_t set[64];
    int setCount = 0;
    int emitted = 0;
    CoefficientList list{CoefficientList::Entry(4, 0), CoefficientList::Entry(24, 0), CoefficientList::Entry(44, 0),
                         CoefficientList::Entry(0, 2)};
    const int planes = static_cast<int>(bits.Bits(3)) + 1;
    std::int8_t mask = static_cast<std::int8_t>(1u << (planes - 1));
    // Gives coefficient `c` the mask with the sign read; false once the last mask has been given.
    auto give = [&](int c) {
        if (setCount < 64) set[setCount++] = static_cast<std::uint8_t>(c);
        const std::int8_t v = bits.Bit() ? static_cast<std::int8_t>(-mask) : mask;
        if (c < 64) residue[c] = v;
        return emitted++ != masks;
    };
    for (int plane = 0; plane < planes; ++plane, mask = static_cast<std::int8_t>(mask >> 1)) {
        for (int i = 0, n = setCount; i < n; ++i) {
            if (!bits.Bit()) continue;
            std::int8_t& r = residue[set[i]];
            r = static_cast<std::int8_t>(r + (r < 0 ? -mask : mask));
            if (emitted++ == masks) return;
        }
        auto quad = [&](int c) {
            for (int k = 0; k < 4; ++k, ++c) {
                if (bits.Bit())
                    list.Prepend(CoefficientList::Entry(c, 3));
                else if (!give(c))
                    return false;
            }
            return true;
        };
        for (int at = list.first; at < list.last;) {
            const std::uint8_t entry = list.entries[at];
            if (entry == 0 || !bits.Bit()) {
                ++at;
                continue;
            }
            const int c = entry >> 2;
            switch (entry & 3) {
            case 0:
                list.entries[at] = CoefficientList::Entry(c + 4, 1);
                if (!quad(c)) return;
                break;
            case 1:
                list.entries[at] = CoefficientList::Entry(c, 2);
                for (int k = 1; k <= 3; ++k) list.Append(CoefficientList::Entry(c + 4 * k, 2));
                break;
            case 2:
                list.entries[at++] = 0;
                if (!quad(c)) return;
                break;
            default:
                if (!give(c)) return;
                list.entries[at++] = 0;
                break;
            }
        }
    }
}

// ---- The inverse DCT ----

// Fixed point with 11 fraction bits, in 32-bit arithmetic that wraps.
std::int32_t Scale(std::int32_t x, std::int32_t c) {
    return static_cast<std::int32_t>(static_cast<std::uint32_t>(x) * static_cast<std::uint32_t>(c)) >> 11;
}

// The 8-point inverse DCT in place (the AAN factorisation).
void Idct8(std::int32_t* v, int step) {
    std::int32_t& v0 = v[0];
    std::int32_t& v1 = v[step];
    std::int32_t& v2 = v[2 * step];
    std::int32_t& v3 = v[3 * step];
    std::int32_t& v4 = v[4 * step];
    std::int32_t& v5 = v[5 * step];
    std::int32_t& v6 = v[6 * step];
    std::int32_t& v7 = v[7 * step];
    const std::int32_t t0 = v0 + v4, t1 = v0 - v4, t3 = v2 + v6;
    const std::int32_t t2 = Scale(v2 - v6, 2896) - t3;
    const std::int32_t e0 = t0 + t3, e3 = t0 - t3, e1 = t1 + t2, e2 = t1 - t2;
    const std::int32_t z13 = v3 + v5, z10 = v5 - v3, z11 = v1 + v7, z12 = v1 - v7;
    const std::int32_t o7 = z11 + z13;
    const std::int32_t z5 = Scale(z10 + z12, 3784);
    const std::int32_t o6 = Scale(z10, -5352) - o7 + z5;
    const std::int32_t o5 = Scale(z11 - z13, 2896) - o6;
    const std::int32_t o4 = Scale(z12, 2217) - z5 + o5;
    v0 = e0 + o7;
    v7 = e0 - o7;
    v1 = e1 + o6;
    v6 = e1 - o6;
    v2 = e2 + o5;
    v5 = e2 - o5;
    v4 = e3 + o4;
    v3 = e3 - o4;
}

// The block dequantised by `quant` and transformed: 8 bits of fraction left on each value.
void InverseDct(const std::int16_t block[64], const std::int32_t quant[64], std::int32_t out[64]) {
    for (int i = 0; i < 64; ++i) out[i] = Scale(block[i], quant[i]);
    for (int column = 0; column < 8; ++column) Idct8(out + column, 8);
    for (int row = 0; row < 8; ++row) Idct8(out + 8 * row, 1);
}

std::uint8_t Pixel(std::int32_t value) { return static_cast<std::uint8_t>((value + 0x7F) >> 8); }

// ---- Blocks ----

void CopyBlock(std::uint8_t* to, int toStride, const std::uint8_t* from, int fromStride) {
    for (int y = 0; y < 8; ++y) std::memcpy(to + y * toStride, from + y * fromStride, 8);
}

// The 8x8 block of the previous frame at (x, y) moved by (dx, dy); kept inside the plane.
const std::uint8_t* MovedBlock(const Plane& from, int x, int y, int dx, int dy) {
    const int sx = std::clamp(x + dx, 0, from.width - 8);
    const int sy = std::clamp(y + dy, 0, from.height - 8);
    return from.Row(sy) + sx;
}

// The 8x8 block doubled into the 16x16 square at (x, y), cut at the plane's edge.
void PutScaled(Plane& to, int x, int y, const std::uint8_t block[64]) {
    const int width = std::min(16, to.width - x), height = std::min(16, to.height - y);
    for (int row = 0; row < height; ++row) {
        std::uint8_t* out = to.Row(y + row) + x;
        const std::uint8_t* in = block + 8 * (row >> 1);
        for (int col = 0; col < width; ++col) out[col] = in[col >> 1];
    }
}

// The bundles of one plane and the blocks they make.
class PlaneDecoder {
public:
    PlaneDecoder(BitReader& bits, int width) : bits(bits) {
        blockTypes.Init(width, 1);
        subTypes.Init(width >> 1, 1);
        colors.bundle.Init(width, 64);
        patterns.Init(width, 8);
        xOffsets.Init(width, 1);
        yOffsets.Init(width, 1);
        intraDc.Init(width, 1);
        interDc.Init(width, 1);
        runs.Init(width, 48);
        blockTypes.tree.Read(bits);
        subTypes.tree.Read(bits);
        for (Tree& t : colors.high) t.Read(bits);
        colors.bundle.tree.Read(bits);
        patterns.tree.Read(bits);
        xOffsets.tree.Read(bits);
        yOffsets.tree.Read(bits);
        runs.tree.Read(bits);
    }

    void Decode(Plane& to, const Plane& from) {
        for (int y = 0; y < to.height; y += 8) {
            ReadBlockTypes(bits, blockTypes);
            ReadBlockTypes(bits, subTypes);
            colors.Read(bits);
            ReadPatterns(bits, patterns);
            ReadMotion(bits, xOffsets);
            ReadMotion(bits, yOffsets);
            ReadDc(bits, intraDc, false);
            ReadDc(bits, interDc, true);
            ReadRuns(bits, runs);
            for (int x = 0; x < to.width;) {
                const int type = blockTypes.Next();
                if (type == kScaled) {
                    // A 16x16 block covers the next row's two blocks too; their entries there are skipped.
                    if (!(y & 8)) Scaled(to, x, y);
                    x += 16;
                    continue;
                }
                Block(type, to, from, x, y);
                x += 8;
            }
        }
    }

private:
    void Block(int type, Plane& to, const Plane& from, int x, int y) {
        std::uint8_t* out = to.Row(y) + x;
        std::uint8_t block[64];
        std::int16_t coefficients[64];
        std::int32_t transformed[64];
        switch (type) {
        case kSkip:
            CopyBlock(out, to.stride, from.Row(y) + x, from.stride);
            break;
        case kMotion:
            CopyBlock(out, to.stride, Moved(from, x, y), from.stride);
            break;
        case kRun:
            RunBlock(block);
            CopyBlock(out, to.stride, block, 8);
            break;
        case kResidue: {
            CopyBlock(block, 8, Moved(from, x, y), from.stride);
            const int masks = static_cast<int>(bits.Bits(7));
            std::int8_t residue[64];
            ReadResidue(bits, residue, masks);
            for (int s = 0; s < 64; ++s) block[kScan[s]] = static_cast<std::uint8_t>(block[kScan[s]] + residue[s]);
            CopyBlock(out, to.stride, block, 8);
            break;
        }
        case kIntra:
            IntraBlock(block, coefficients, transformed);
            CopyBlock(out, to.stride, block, 8);
            break;
        case kFill:
            std::memset(block, colors.bundle.Next(), sizeof(block));
            CopyBlock(out, to.stride, block, 8);
            break;
        case kInter: {
            CopyBlock(block, 8, Moved(from, x, y), from.stride);
            coefficients[0] = static_cast<std::int16_t>(interDc.Next());
            ReadCoefficients(bits, coefficients);
            InverseDct(coefficients, kInterQuant[bits.Bits(4)], transformed);
            for (int i = 0; i < 64; ++i) block[i] = static_cast<std::uint8_t>(block[i] + Pixel(transformed[i]));
            CopyBlock(out, to.stride, block, 8);
            break;
        }
        case kPattern:
            PatternBlock(block);
            CopyBlock(out, to.stride, block, 8);
            break;
        case kRaw:
            RawBlock(block);
            CopyBlock(out, to.stride, block, 8);
            break;
        default:
            break;
        }
    }

    void Scaled(Plane& to, int x, int y) {
        std::uint8_t block[64];
        std::int16_t coefficients[64];
        std::int32_t transformed[64];
        switch (subTypes.Next()) {
        case kRun:
            RunBlock(block);
            break;
        case kIntra:
            IntraBlock(block, coefficients, transformed);
            break;
        case kFill:
            std::memset(block, colors.bundle.Next(), sizeof(block));
            break;
        case kPattern:
            PatternBlock(block);
            break;
        case kRaw:
            RawBlock(block);
            break;
        default:
            return;
        }
        PutScaled(to, x, y, block);
    }

    const std::uint8_t* Moved(const Plane& from, int x, int y) {
        const int dx = xOffsets.Next();
        const int dy = yOffsets.Next();
        return MovedBlock(from, x, y, dx, dy);
    }

    // Runs along one of the 16 patterns: a bit per run says one colour for all of it or a colour each.
    void RunBlock(std::uint8_t block[64]) {
        const std::uint8_t* order = kRunPatterns[bits.Bits(4)];
        int i = 0;
        while (i < 63) {
            const bool one = bits.Bit();
            const int run = runs.Next() + 1;
            const int color = one ? colors.bundle.Next() : 0;
            for (int k = 0; k < run; ++k, ++i) {
                const int value = one ? color : colors.bundle.Next();
                if (i < 64) block[order[i]] = static_cast<std::uint8_t>(value);
            }
        }
        if (i == 63) block[order[63]] = static_cast<std::uint8_t>(colors.bundle.Next());
    }

    void IntraBlock(std::uint8_t block[64], std::int16_t coefficients[64], std::int32_t transformed[64]) {
        coefficients[0] = static_cast<std::int16_t>(intraDc.Next());
        ReadCoefficients(bits, coefficients);
        InverseDct(coefficients, kIntraQuant[bits.Bits(4)], transformed);
        for (int i = 0; i < 64; ++i) block[i] = Pixel(transformed[i]);
    }

    // Two colours, a pattern byte a row: a set bit picks the second colour.
    void PatternBlock(std::uint8_t block[64]) {
        const int first = colors.bundle.Next();
        const int second = colors.bundle.Next();
        for (int row = 0; row < 8; ++row) {
            const int pattern = patterns.Next();
            for (int col = 0; col < 8; ++col)
                block[8 * row + col] = static_cast<std::uint8_t>(pattern >> col & 1 ? second : first);
        }
    }

    void RawBlock(std::uint8_t block[64]) {
        for (int i = 0; i < 64; ++i) block[i] = static_cast<std::uint8_t>(colors.bundle.Next());
    }

    BitReader& bits;
    Bundle blockTypes, subTypes, patterns, xOffsets, yOffsets, intraDc, interDc, runs;
    Colors colors;
};

void Allocate(Plane& plane, int width, int height) {
    plane.width = (width + 7) & ~7;
    plane.height = (height + 7) & ~7;
    plane.stride = plane.width;
    plane.pixels.assign(static_cast<std::size_t>(plane.stride) * plane.height, 0);
}

std::uint32_t Read32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}

} // namespace

VideoDecoder::VideoDecoder(int width, int height) {
    for (Plane* set : {current, previous}) {
        Allocate(set[0], width, height);
        Allocate(set[1], (width + 1) / 2, (height + 1) / 2);
        Allocate(set[2], (width + 1) / 2, (height + 1) / 2);
    }
}

void VideoDecoder::Decode(const std::uint8_t* packet, std::size_t size) {
    for (int i = 0; i < 3; ++i) std::swap(current[i], previous[i]);
    if (size < 4) return;
    // The luma's bytes (from this word on), then luma, V and U, each from a word of its own.
    const std::size_t lumaBytes = std::min<std::size_t>(Read32(packet), size);
    std::size_t at = 4;
    for (int i : {0, 2, 1}) {
        const std::size_t start = std::min(at, size);
        BitReader bits(packet + start, size - start);
        PlaneDecoder(bits, current[i].width).Decode(current[i], previous[i]);
        at = i == 0 ? lumaBytes : start + bits.WordBytes();
    }
}

} // namespace platform::bink
