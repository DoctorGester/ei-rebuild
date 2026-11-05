// The rain's and the snow's field (weather_field.h): each particle's numbers from a hash of its index, kept in a table
// ordered by bin; its anchor in the repeating box, its fall, the snow's drift and the fades that hide it where it
// wraps.
#include "weather_field.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace weather {

// A particle's own numbers.
struct Seed {
    float x = 0.0f, y = 0.0f, z = 0.0f;   // its point in the box (units)
    int group = 0;                        // its fall group
    float fall = 0.0f;                    // units per tick
    float shown = 0.0f;                   // the density it shows from (0..1)
    float size = 0.0f, shade = 0.0f;
    // The snow's drift: its amplitude, each axis's turns per tick and phase, and the spin's turn at the wave's start.
    float amplitude = 0.0f;
    double rateX = 0.0, rateY = 0.0;
    float phaseX = 0.0f, phaseY = 0.0f;
    float spinCos = 1.0f, spinSin = 0.0f;
    float spinWay = 1.0f;   // 1 or -1: the way it spins
    int frame = 0;
};

namespace {

constexpr float kTurn = 2.0f * kPi;   // radians

constexpr int kRainCount = 20000;
constexpr int kSnowCount = 16000;

// The fall in units per tick (about 18 ticks a second): rain about 9 units a second, snow under 2; each particle
// falls at its group's share of it.
constexpr float kRainFall = 0.5f;
constexpr float kSnowFall = 0.1f;

// The fades: the box's walls (from this share of the half side or height out, whole a little inside the side walls,
// so the drift's step where the anchor wraps and the drift does not is never seen), the eye (from nothing at the first
// distance to whole at the second, in units).
constexpr float kSideFade = 0.8f, kSideWhole = 0.98f;
constexpr float kHeightFade = 0.85f;
constexpr float kRainNear = 0.8f, kRainWhole = 3.0f;
constexpr float kSnowNear = 0.8f, kSnowWhole = 3.0f;
// The snow's drift on each horizontal axis: the least and the most amplitude in units.
constexpr float kLeastSway = 0.15f, kMostSway = 0.5f;
// The density's soft edge: a particle fades out over this share of the density as the layer's fade lowers it.
constexpr float kDensitySoftness = 0.05f;
// The snowflake's pictures.
constexpr int kFrames = 4;

constexpr float kHalfSide = 0.5f * kBoxSide;
constexpr float kHalfHeight = 0.5f * kBoxHeight;

// The numbers a particle takes from its index, one per salt.
enum class Salt : std::uint32_t {
    X, Y, Z, Fall, Size, Shade, Shown, SwayAmplitude, SwayPeriodX, SwayPeriodY, SwayPhaseX, SwayPhaseY, Frame, Spin,
    Count
};

std::uint32_t Hash(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// 0 .. 1 (1 not reached).
float Uniform(Kind kind, int index, Salt salt) {
    const std::uint32_t kindSalt = kind == Kind::Snow ? 0x9E3779B9u : 0u;
    const std::uint32_t key = static_cast<std::uint32_t>(index) * static_cast<std::uint32_t>(Salt::Count) +
                              static_cast<std::uint32_t>(salt) + kindSalt;
    return static_cast<float>(Hash(key) >> 8) * (1.0f / 16777216.0f);
}

// sin(2 pi `turns`) for 0 <= turns < 1, within 1e-5: the turn folded to a quarter either side of 0 and the sine's
// series to its ninth power (the drift's waves are cheaper so than with the library's sine).
float SinTurn(float turns) {
    const float folded = turns < 0.25f ? turns : turns < 0.75f ? 0.5f - turns : turns - 1.0f;
    const float a = folded * kTurn;
    const float a2 = a * a;
    return a * (1.0f - a2 / 6.0f * (1.0f - a2 / 20.0f * (1.0f - a2 / 42.0f * (1.0f - a2 / 72.0f))));
}

// cos(2 pi `turns`) for 0 <= turns < 1.
float CosTurn(float turns) { return SinTurn(turns < 0.75f ? turns + 0.25f : turns - 0.75f); }

// The fraction of a turn a wave of `rate` turns per tick is at `time` (not negative), from `phase` (0..1 of a turn):
// in double (the world's time runs to millions of ticks).
float Turn(double time, double rate, double phase) {
    const double turns = time * rate + phase;
    return static_cast<float>(turns - static_cast<double>(static_cast<std::int64_t>(turns)));
}

double Wrapped(double value, double period) { return value - period * std::floor(value / period); }

// An offset of -2 `half` .. 2 `half` taken into -`half` .. `half`.
float Wrap(float offset, float half) {
    return offset < -half ? offset + 2.0f * half : offset >= half ? offset - 2.0f * half : offset;
}

// 0 below `low`, 1 above `high`, smooth between.
float SmoothStep(float low, float high, float x) {
    float t = (x - low) / (high - low);
    t = t < 0.0f ? 0.0f : t > 1.0f ? 1.0f : t;
    return t * t * (3.0f - 2.0f * t);
}

// The offsets low .. high from the box's centre on an axis (less than a side apart, low within half a side of it)
// taken into the box: one stretch, or two where the wall cuts them. Returns how many.
int WrapStretch(float low, float high, float half, float out[2][2]) {
    out[0][0] = std::max(low, -half);
    out[0][1] = std::min(high, half);
    if (high > half) {
        out[1][0] = -half;
        out[1][1] = high - 2.0f * half;
        return 2;
    }
    if (low < -half) {
        out[1][0] = low + 2.0f * half;
        out[1][1] = half;
        return 2;
    }
    return 1;
}

// A fall group's fall in units per tick: the middle of its kFallGroups-th part of the kind's span (the rain 0.85 to
// 1.15 of kRainFall, the snow 0.6 to 1.4 of kSnowFall).
float FallOf(Kind kind, int group) {
    const float share = (group + 0.5f) / kFallGroups;
    return kind == Kind::Rain ? kRainFall * (0.85f + 0.3f * share) : kSnowFall * (0.6f + 0.8f * share);
}

// The bin of a particle's own numbers.
int BinOf(const Seed& s) {
    constexpr float kPerUnit = kColumns / kBoxSide;
    const int cx = std::min(static_cast<int>(s.x * kPerUnit), kColumns - 1);
    const int cy = std::min(static_cast<int>(s.y * kPerUnit), kColumns - 1);
    const int layer = std::min(static_cast<int>(s.z * (kLayers / kBoxHeight)), kLayers - 1);
    return ((cy * kColumns + cx) * kFallGroups + s.group) * kLayers + layer;
}

struct Table {
    std::vector<Seed> seeds;   // bin by bin
    int binFirst[kBins + 1] = {};
};

Table Build(Kind kind) {
    const bool rain = kind == Kind::Rain;
    const int count = rain ? kRainCount : kSnowCount;
    std::vector<Seed> seeds(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        auto u = [&](Salt salt) { return Uniform(kind, i, salt); };
        Seed& s = seeds[static_cast<std::size_t>(i)];
        s.x = u(Salt::X) * kBoxSide;
        s.y = u(Salt::Y) * kBoxSide;
        s.z = u(Salt::Z) * kBoxHeight;
        s.group = std::min(static_cast<int>(u(Salt::Fall) * kFallGroups), kFallGroups - 1);
        s.fall = FallOf(kind, s.group);
        s.shown = u(Salt::Shown);
        s.size = rain ? 0.012f + 0.008f * u(Salt::Size) : 0.035f + 0.035f * u(Salt::Size);
        s.shade = (rain ? 0.8f : 0.85f) + (rain ? 0.2f : 0.15f) * u(Salt::Shade);
        if (!rain) {
            // The drift: a slow sway of its own on each horizontal axis, a few seconds a turn; the flake spins once a
            // turn of the x axis's sway, either way, from a turn of its own.
            s.amplitude = kLeastSway + (kMostSway - kLeastSway) * u(Salt::SwayAmplitude);
            s.rateX = 1.0 / (90.0 + 120.0 * u(Salt::SwayPeriodX));
            s.rateY = 1.0 / (70.0 + 110.0 * u(Salt::SwayPeriodY));
            s.phaseX = u(Salt::SwayPhaseX);
            s.phaseY = u(Salt::SwayPhaseY);
            const float spin = u(Salt::Spin);
            s.spinWay = spin < 0.5f ? 1.0f : -1.0f;
            s.spinSin = SinTurn(spin);
            s.spinCos = CosTurn(spin);
            s.frame = std::min(static_cast<int>(u(Salt::Frame) * kFrames), kFrames - 1);
        }
    }
    // Ordered by bin, each bin's in index order.
    Table table;
    table.seeds.resize(seeds.size());
    std::vector<int> counts(kBins);
    for (const Seed& s : seeds) ++counts[static_cast<std::size_t>(BinOf(s))];
    for (int b = 0; b < kBins; ++b) table.binFirst[b + 1] = table.binFirst[b] + counts[static_cast<std::size_t>(b)];
    std::vector<int> next(table.binFirst, table.binFirst + kBins);
    for (const Seed& s : seeds) table.seeds[static_cast<std::size_t>(next[static_cast<std::size_t>(BinOf(s))]++)] = s;
    return table;
}

const Table& TableOf(Kind kind) {
    static const Table rain = Build(Kind::Rain);
    static const Table snow = Build(Kind::Snow);
    return kind == Kind::Rain ? rain : snow;
}

} // namespace

Field::Field(Kind kind, const FieldView& view) : kind(kind), view(view) {
    const Table& table = TableOf(kind);
    seeds = table.seeds.data();
    binFirst = table.binFirst;
    count = static_cast<int>(table.seeds.size());
    // In double: the centre is a map point, the fall's height the world's time over the fall.
    baseX = static_cast<float>(Wrapped(view.carry.x - static_cast<double>(view.centre.x), kBoxSide)) - kBoxSide;
    baseY = static_cast<float>(Wrapped(view.carry.y - static_cast<double>(view.centre.y), kBoxSide)) - kBoxSide;
    for (int group = 0; group < kFallGroups; ++group) {
        const double fallen = Wrapped(FallOf(kind, group) * view.time, kBoxHeight);
        baseZ[group] = static_cast<float>(Wrapped(view.carry.z - fallen - view.centre.z, kBoxHeight)) - kBoxHeight;
    }
}

int Field::Count() const { return count; }

BinRange Field::Bin(int bin) const { return {binFirst[bin], binFirst[bin + 1]}; }

int Field::BinBounds(int bin, float margin, Bounds out[8]) const {
    constexpr float kWidth = kBoxSide / kColumns;
    constexpr float kDepth = kBoxHeight / kLayers;
    // A hundredth of a unit more: the particles' sums round.
    constexpr float kRounding = 0.01f;
    const int column = bin / (kFallGroups * kLayers);
    const int group = bin / kLayers % kFallGroups;
    const int layer = bin % kLayers;
    // The bin's start on each axis as an offset from the centre (as Locate takes a particle's), its particles from
    // there to a bin's size on (and the snow's drift either side).
    const float reach = margin + kRounding;
    const float sway = kind == Kind::Snow ? kMostSway : 0.0f;
    const float lowX = Wrap((column % kColumns) * kWidth + baseX, kHalfSide);
    const float lowY = Wrap((column / kColumns) * kWidth + baseY, kHalfSide);
    const float lowZ = Wrap(layer * kDepth + baseZ[group], kHalfHeight);
    float stretchX[2][2], stretchY[2][2], stretchZ[2][2];
    const int countX = WrapStretch(lowX - sway - reach, lowX + kWidth + sway + reach, kHalfSide, stretchX);
    const int countY = WrapStretch(lowY - sway - reach, lowY + kWidth + sway + reach, kHalfSide, stretchY);
    const int countZ = WrapStretch(lowZ - reach, lowZ + kDepth + reach, kHalfHeight, stretchZ);
    const Vector3& c = view.centre;
    int boxes = 0;
    for (int i = 0; i < countX; ++i)
        for (int j = 0; j < countY; ++j)
            for (int k = 0; k < countZ; ++k)
                out[boxes++] = {{c.x + stretchX[i][0], c.y + stretchY[j][0], c.z + stretchZ[k][0]},
                                {c.x + stretchX[i][1], c.y + stretchY[j][1], c.z + stretchZ[k][1]}};
    return boxes;
}

bool Field::Locate(int index, const Seed** seed, float* shown, Vector3* offset) const {
    const Seed& s = seeds[index];
    *shown = (view.density * (1.0f + kDensitySoftness) - s.shown) / kDensitySoftness;
    if (!(*shown > 0.0f)) return false;
    const float x = Wrap(s.x + baseX, kHalfSide);
    const float y = Wrap(s.y + baseY, kHalfSide);
    constexpr float kReach = kHalfSide + kMostDrift;
    if (LengthSquared2D(x, y) > kReach * kReach) return false;
    *seed = &s;
    *offset = {x, y, Wrap(s.z + baseZ[s.group], kHalfHeight)};
    return true;
}

bool Field::Anchor(int index, Vector3* point) const {
    const Seed* seed = nullptr;
    float shown = 0.0f;
    Vector3 offset;
    if (!Locate(index, &seed, &shown, &offset)) return false;
    *point = view.centre + offset;
    return true;
}

Particle Field::Place(int index) const {
    const bool rain = kind == Kind::Rain;
    Particle p;   // alpha 0: a particle that does not show
    const Seed* seed = nullptr;
    float shown = 0.0f;
    Vector3 offset;
    if (!Locate(index, &seed, &shown, &offset)) return p;
    const Seed& s = *seed;
    p.velocity = {view.wind.x, view.wind.y, view.wind.z - s.fall};
    p.size = s.size;
    p.shade = s.shade;
    if (!rain) {
        const float turnX = Turn(view.time, s.rateX, s.phaseX);
        const float turnY = Turn(view.time, s.rateY, s.phaseY);
        const float sinX = SinTurn(turnX), cosX = CosTurn(turnX);
        const float sinY = SinTurn(turnY), cosY = CosTurn(turnY);
        offset.x += s.amplitude * sinX;
        offset.y += 0.8f * s.amplitude * sinY;
        p.velocity.x += static_cast<float>(s.amplitude * cosX * kTurn * s.rateX);
        p.velocity.y += static_cast<float>(0.8f * s.amplitude * cosY * kTurn * s.rateY);
        // The spin: the x wave's turn (either way) from the flake's own.
        p.spinCos = cosX * s.spinCos - s.spinWay * sinX * s.spinSin;
        p.spinSin = s.spinWay * sinX * s.spinCos + cosX * s.spinSin;
        p.frame = s.frame;
    }
    p.position = view.centre + offset;

    const float side = std::sqrt(LengthSquared2D(offset.x, offset.y)) / kHalfSide;
    const float height = std::fabs(offset.z) / kHalfHeight;
    float alpha = (1.0f - SmoothStep(kSideFade, kSideWhole, side)) * (1.0f - SmoothStep(kHeightFade, 1.0f, height));
    const float distance = Distance(p.position, view.eye);
    alpha *= rain ? SmoothStep(kRainNear, kRainWhole, distance) : SmoothStep(kSnowNear, kSnowWhole, distance);
    alpha *= shown < 1.0f ? shown : 1.0f;
    p.alpha = alpha;
    return p;
}

Vector3 Carry(Vector3 carry, Vector3 wind, double ticks) {
    return {static_cast<float>(Wrapped(carry.x + wind.x * ticks, kBoxSide)),
            static_cast<float>(Wrapped(carry.y + wind.y * ticks, kBoxSide)),
            static_cast<float>(Wrapped(carry.z + wind.z * ticks, kBoxHeight))};
}

} // namespace weather
