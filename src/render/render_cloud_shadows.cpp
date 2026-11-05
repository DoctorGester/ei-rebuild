// The cloud shadows: a repeating cloud map made once, its two layers drifting with the wind, the cover from the
// weather, and the receivers' part of the shadow uniforms (shaders/cloud_shadow.hlsli). See docs/notes/renderer.md,
// "The renderer's source files".
#include "render.h"
#include "render_internal.h"

#include "lib/maths.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <vector>

namespace render {

// The map: kCloudSize texels a side, repeating, R8. Gradient noise summed over kOctaves octaves, the first of
// kFirstCells cells across, each next twice as many at half the height; then each texel's value replaced by its rank
// among them (so a threshold t leaves the share 1 - t above it).
constexpr int kCloudSize = 256;
constexpr int kFirstCells = 4;
constexpr int kOctaves = 5;
constexpr double kTwoPi = 6.283185307179586;

// The two layers: the map repeats every kTile* landscape units; the second is turned (0.8, 0.6) against the first
// and weighs less. The weights and the turn are the constants written in shaders/cloud_shadow.hlsli.
constexpr double kTileA = 176.0, kTileB = 109.0;
constexpr float kWeightA = 0.65f, kWeightB = 0.35f;
constexpr double kTurnCos = 0.8, kTurnSin = 0.6;
// Each layer moves by the wind times its drift a tick (the wind's 0.4 a tick: 0.044 and 0.06; the second a little
// faster, so the patches change their shape as they go).
constexpr double kDriftA = 0.11, kDriftB = 0.15;
// The share of the ground under clouds in clear weather and in full rain or snow; the cover follows a change of the
// weather over about kCoverTicks.
constexpr float kClearCover = 0.4f, kRainCover = 0.8f;
constexpr double kCoverTicks = 150.0;
// How much of the sun a full cloud hides by day.
constexpr float kCloudLight = 0.55f;
// A step of the world's time longer than this (a load, a zone change) or back moves nothing.
constexpr double kLongestStep = 64.0;

static gpu::Texture g_cloudMap;

struct Drift {
    bool started = false;
    double time = 0.0;
    // Each layer's offset in landscape units, kept within its repeat: the first's tile, five of the second's (turned
    // by (0.8, 0.6), five tiles along x or y are whole tiles of its own: 4 and 3).
    double a[2] = {}, b[2] = {};
    float cover = 0.0f;
} g_drift;

// A lattice corner's gradient direction: a hash of the corner (taken around the octave's cells) and the octave.
static float CornerAngle(int x, int y, int octave) {
    std::uint32_t h = static_cast<std::uint32_t>(x) * 0x8DA6B343u ^ static_cast<std::uint32_t>(y) * 0xD8163841u ^
                      static_cast<std::uint32_t>(octave + 1) * 0xCB1AB31Fu;
    h ^= h >> 13;
    h *= 0x85EBCA6Bu;
    h ^= h >> 16;
    return static_cast<float>(static_cast<double>(h & 0xFFFFu) / 65536.0 * kTwoPi);
}

static double Fade(double t) { return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }

// The gradient noise of `cells` cells across the map at the texel's centre (x, y), repeating with the map.
static double Noise(int x, int y, int cells, int octave) {
    const double u = (x + 0.5) * cells / kCloudSize, v = (y + 0.5) * cells / kCloudSize;
    const int i = static_cast<int>(std::floor(u)), j = static_cast<int>(std::floor(v));
    const double fx = u - i, fy = v - j;
    double corner[4];
    for (int k = 0; k < 4; ++k) {
        const int dx = k & 1, dy = k >> 1;
        const float angle = CornerAngle((i + dx) % cells, (j + dy) % cells, octave);
        corner[k] = lib::Cos(angle) * (fx - dx) + lib::Sin(angle) * (fy - dy);
    }
    const double sx = Fade(fx), sy = Fade(fy);
    const double low = corner[0] + (corner[1] - corner[0]) * sx, high = corner[2] + (corner[3] - corner[2]) * sx;
    return low + (high - low) * sy;
}

static std::vector<std::uint8_t> CloudTexels() {
    const int texels = kCloudSize * kCloudSize;
    std::vector<double> sum(static_cast<std::size_t>(texels), 0.0);
    double height = 1.0;
    for (int octave = 0, cells = kFirstCells; octave < kOctaves; ++octave, cells *= 2, height *= 0.5)
        for (int y = 0; y < kCloudSize; ++y)
            for (int x = 0; x < kCloudSize; ++x)
                sum[static_cast<std::size_t>(y) * kCloudSize + x] += height * Noise(x, y, cells, octave);
    std::vector<int> order(static_cast<std::size_t>(texels));
    std::iota(order.begin(), order.end(), 0);
    std::sort(order.begin(), order.end(), [&](int a, int b) { return sum[a] != sum[b] ? sum[a] < sum[b] : a < b; });
    std::vector<std::uint8_t> level(static_cast<std::size_t>(texels));
    for (int rank = 0; rank < texels; ++rank)
        level[static_cast<std::size_t>(order[rank])] = static_cast<std::uint8_t>(rank * 256 / texels);
    return level;
}

// The threshold the layers' weighted sum (wa A + wb B, A and B evenly spread over 0 .. 1, wa >= wb) exceeds on
// `cover` of the ground: the inverse of the sum's spread, a trapezoid.
static float CoverThreshold(float cover) {
    const double wa = kWeightA, wb = kWeightB;
    const double below = 1.0 - std::clamp(static_cast<double>(cover), 0.0, 1.0);   // the share under the threshold
    const double corner = wb / (2.0 * wa);                                            // the share below wb
    double t;
    if (below <= corner) t = std::sqrt(2.0 * wa * wb * below);
    else if (below <= 1.0 - corner) t = below * wa + wb * 0.5;
    else t = 1.0 - std::sqrt(2.0 * wa * wb * (1.0 - below));
    return static_cast<float>(t);
}

static double Wrapped(double offset, double tile) { return offset - std::floor(offset / tile) * tile; }

void CloudShadows(const CloudWeather& weather) {
    ShadowFrame& s = g_frame.shadow;
    s.haveClouds = true;
    s.weather = weather;
}

float PrepareClouds(const ShadowFrame& shadow) {
    if (!g_cloudMap) {
        g_cloudMap = gpu::MakeTexture({kCloudSize, kCloudSize, 1, 1, gpu::Format::R8, gpu::kSampled, "ei-cloud-map"});
        const std::vector<std::uint8_t> texels = CloudTexels();
        if (!g_cloudMap || !gpu::Write(g_cloudMap, 0, {0, 0, kCloudSize, kCloudSize}, texels.data(), kCloudSize))
            Fatal("Can't make the cloud shadows' map (%s).", gpu::Error());
    }
    if (!shadow.haveClouds) return 0.0f;
    const CloudWeather& w = shadow.weather;
    const float target = kClearCover + (kRainCover - kClearCover) * std::clamp(w.rain, 0.0f, 1.0f);
    Drift& d = g_drift;
    double step = d.started ? w.time - d.time : 0.0;
    if (!(step >= 0.0 && step <= kLongestStep)) step = 0.0;
    d.cover = d.started ? d.cover + (target - d.cover) * (1.0f - lib::Exp(static_cast<float>(-step / kCoverTicks))) : target;
    d.started = true;
    d.time = w.time;
    d.a[0] = Wrapped(d.a[0] + w.windX * kDriftA * step, kTileA);
    d.a[1] = Wrapped(d.a[1] + w.windY * kDriftA * step, kTileA);
    d.b[0] = Wrapped(d.b[0] + w.windX * kDriftB * step, kTileB * 5.0);
    d.b[1] = Wrapped(d.b[1] + w.windY * kDriftB * step, kTileB * 5.0);
    return kCloudLight * std::clamp(w.day, 0.0f, 1.0f);
}

float CloudCover() { return g_drift.cover; }

void CloudUniforms(float x0, float y0, float extent, float light, ShadowParams* params) {
    // The landscape's x and y on the level plane at the map's clip point s: s * extent / 2 + (x0, y0) + extent / 2.
    // A layer's uv there is that point less the layer's drift, over its tile (the second turned first).
    const double half = extent * 0.5;
    const double ox = x0 + half, oy = y0 + half;
    const double ax = (ox - g_drift.a[0]) / kTileA, ay = (oy - g_drift.a[1]) / kTileA;
    const double bx = (kTurnCos * (ox - g_drift.b[0]) - kTurnSin * (oy - g_drift.b[1])) / kTileB;
    const double by = (kTurnSin * (ox - g_drift.b[0]) + kTurnCos * (oy - g_drift.b[1])) / kTileB;
    params->cloudScaleA = static_cast<float>(half / kTileA);
    params->cloudScaleB = static_cast<float>(half / kTileB);
    params->cloudThreshold = CoverThreshold(g_drift.cover);
    params->cloudLight = light;
    params->cloudShiftA[0] = static_cast<float>(ax - std::floor(ax));
    params->cloudShiftA[1] = static_cast<float>(ay - std::floor(ay));
    params->cloudShiftB[0] = static_cast<float>(bx - std::floor(bx));
    params->cloudShiftB[1] = static_cast<float>(by - std::floor(by));
}

void BindCloudMap(int slot) {
    static const gpu::Sampler sampler = gpu::MakeSampler(gpu::SamplerDesc{});
    const gpu::TextureBinding b{g_cloudMap, sampler};
    gpu::BindTextures(gpu::Stage::Fragment, slot, {&b, 1});
}

} // namespace render
