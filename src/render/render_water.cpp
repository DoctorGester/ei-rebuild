// The water's small ripples: the map the water's look samples (shaders/terrain.hlsl, Water) and its two layers'
// motion. See docs/notes/renderer.md, "The renderer's source files".
#include "render_internal.h"

#include "lib/maths.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace render {

// The map: kRippleSize texels a side, repeating, with its levels down to 1 x 1.
constexpr int kRippleSize = 256;
constexpr int kRippleLevels = 9;
static_assert(1 << (kRippleLevels - 1) == kRippleSize);
constexpr float kTwoPi = 6.28318531f;

static gpu::Texture g_ripples;

// The map is the same on every run and draws nothing from the game's generators.
struct MapRandom {
    std::uint32_t state = 0x2545F491u;
    float Next() {
        state = state * 1664525u + 1013904223u;
        return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
    }
};

// A wave of the map: whole cycles across it in x and y (so the map repeats without a seam), its height and phase.
struct Wave {
    int kx = 0, ky = 0;
    float amplitude = 0.0f, cosPhase = 0.0f, sinPhase = 0.0f;
};

// `count` waves in random directions, from `longest` to `shortest` cycles across the map, the heights falling as
// the cycles to the power `falloff`.
static std::vector<Wave> Waves(MapRandom& random, int count, float longest, float shortest, float falloff) {
    std::vector<Wave> waves;
    while (static_cast<int>(waves.size()) < count) {
        const float angle = random.Next() * kTwoPi;
        const float cycles = longest + (shortest - longest) * random.Next();
        const float phase = random.Next() * kTwoPi;
        const int kx = static_cast<int>(std::lround(cycles * lib::Cos(angle)));
        const int ky = static_cast<int>(std::lround(cycles * lib::Sin(angle)));
        if (kx == 0 && ky == 0) continue;
        const float length = lib::Hypot(static_cast<float>(kx), static_cast<float>(ky));
        waves.push_back(Wave{kx, ky, lib::Pow(length, -falloff), lib::Cos(phase), lib::Sin(phase)});
    }
    return waves;
}

static std::uint8_t Signed(float v) { return static_cast<std::uint8_t>(std::lround(127.5f + 127.0f * v)); }
static std::uint8_t Unsigned(float v) { return static_cast<std::uint8_t>(std::lround(255.0f * v)); }

// Level 0: r, g the slopes of a sum of waves along x and y (-1 .. 1 as 0 .. 255, the steepest at the ends), b the
// foam's pattern (a finer sum's height, 0 .. 1), a 255.
static std::vector<std::uint8_t> FirstLevel() {
    MapRandom random;
    const std::vector<Wave> surface = Waves(random, 40, 3.0f, 22.0f, 1.5f);
    const std::vector<Wave> foam = Waves(random, 24, 10.0f, 40.0f, 0.8f);
    // A wave's angle at a texel is a whole number of kRippleSize-th turns: its cosine and sine from these.
    std::vector<float> cosines(kRippleSize), sines(kRippleSize);
    for (int k = 0; k < kRippleSize; ++k) {
        cosines[k] = lib::Cos(kTwoPi * static_cast<float>(k) / kRippleSize);
        sines[k] = lib::Sin(kTwoPi * static_cast<float>(k) / kRippleSize);
    }
    constexpr int kMask = kRippleSize - 1;
    const std::size_t texels = static_cast<std::size_t>(kRippleSize) * kRippleSize;
    std::vector<float> sx(texels), sy(texels), pattern(texels);
    for (const Wave& w : surface)
        for (int y = 0; y < kRippleSize; ++y) {
            const int rowTurns = w.ky * y;
            for (int x = 0; x < kRippleSize; ++x) {
                const int i = (rowTurns + w.kx * x) & kMask;
                const float c = cosines[i] * w.cosPhase - sines[i] * w.sinPhase;   // cos(the angle + the phase)
                const std::size_t at = static_cast<std::size_t>(y) * kRippleSize + x;
                sx[at] += w.amplitude * static_cast<float>(w.kx) * c;
                sy[at] += w.amplitude * static_cast<float>(w.ky) * c;
            }
        }
    for (const Wave& w : foam)
        for (int y = 0; y < kRippleSize; ++y) {
            const int rowTurns = w.ky * y;
            for (int x = 0; x < kRippleSize; ++x) {
                const int i = (rowTurns + w.kx * x) & kMask;
                pattern[static_cast<std::size_t>(y) * kRippleSize + x] +=
                    w.amplitude * (sines[i] * w.cosPhase + cosines[i] * w.sinPhase);   // sin(the angle + the phase)
            }
        }
    float steepest = 1e-9f, low = 1e30f, high = -1e30f;
    for (std::size_t at = 0; at < texels; ++at) {
        steepest = std::fmax(steepest, std::fmax(std::fabs(sx[at]), std::fabs(sy[at])));
        low = std::fmin(low, pattern[at]);
        high = std::fmax(high, pattern[at]);
    }
    std::vector<std::uint8_t> level(texels * 4);
    for (std::size_t at = 0; at < texels; ++at) {
        level[4 * at] = Signed(sx[at] / steepest);
        level[4 * at + 1] = Signed(sy[at] / steepest);
        level[4 * at + 2] = Unsigned((pattern[at] - low) / (high - low + 1e-9f));
        level[4 * at + 3] = 255;
    }
    return level;
}

// The level after `level` (`size` texels a side): each texel the mean of four.
static std::vector<std::uint8_t> NextLevel(const std::vector<std::uint8_t>& level, int size) {
    const int half = size / 2;
    std::vector<std::uint8_t> next(static_cast<std::size_t>(half) * half * 4);
    for (int y = 0; y < half; ++y)
        for (int x = 0; x < half; ++x)
            for (int c = 0; c < 4; ++c) {
                auto at = [&](int dx, int dy) {
                    return level[(static_cast<std::size_t>(2 * y + dy) * size + 2 * x + dx) * 4 + c];
                };
                next[(static_cast<std::size_t>(y) * half + x) * 4 + c] =
                    static_cast<std::uint8_t>((at(0, 0) + at(1, 0) + at(0, 1) + at(1, 1) + 2) / 4);
            }
    return next;
}

bool MakeRippleMap() {
    if (g_ripples) return true;
    g_ripples = gpu::MakeTexture({kRippleSize, kRippleSize, kRippleLevels, 1, gpu::Format::Rgba8, gpu::kSampled,
                                  "ei-water-ripples"});
    if (!g_ripples) return false;
    std::vector<std::uint8_t> level = FirstLevel();
    for (int k = 0, size = kRippleSize; k < kRippleLevels; ++k, size /= 2) {
        if (k > 0) level = NextLevel(level, size * 2);
        if (!gpu::Write(g_ripples, k, {0, 0, size, size}, level.data(), static_cast<std::size_t>(size) * 4)) {
            gpu::Release(g_ripples);
            return false;
        }
    }
    return true;
}

gpu::TextureBinding RippleMap() {
    gpu::SamplerDesc d;
    d.mip = gpu::Filter::Linear;
    d.anisotropy = 8;
    static const gpu::Sampler sampler = gpu::MakeSampler(d);
    return gpu::TextureBinding{g_ripples, sampler};
}

// The layers drift apart in two directions, in map widths per client tick (about 55 ms): A about 0.27 landscape
// units a second at its scale, B about 0.2 (terrain.hlsl's kRippleScaleA / B); a swamp's a fifth as fast.
void RippleOffsets(float water[4], float swamp[4]) {
    constexpr double kDrift[4] = {0.0016, 0.0010, -0.0012, 0.0019};
    constexpr double kSwampDrift = 0.2;
    const double ticks = KeptClientTicks();
    for (int k = 0; k < 4; ++k) {
        const double moved = ticks * kDrift[k], slow = moved * kSwampDrift;
        water[k] = static_cast<float>(moved - std::floor(moved));
        swamp[k] = static_cast<float>(slow - std::floor(slow));
    }
}

}   // namespace render
