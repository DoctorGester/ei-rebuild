// The global map's clouds (render_compose.cpp, render::DrawClouds): the whole texture the map's three cloud layers
// show, redrawn each frame. A density from domain-warped fractal noise that repeats across the texture (the layers
// scroll it and wrap), each octave and the warp drifting their own way with the game's clock; the alpha through the
// game's ramp of the density (its start and width, global_map_clouds.cpp); the colour a grey shade, lit from a fixed
// sun by the density's rise towards it, which the layers' colours multiply.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct MapCloudParams {
    float2 size;   // the texture's texels
    float time;    // the drift: the game's seconds times its speed, wrapped every render::kMapCloudsPeriod
    float start;   // the ramp: a texel's alpha is 1 - base^max(0, 255 x density - start)
    float base;
    int seed;      // the pattern (a random word, as a uint)
};
ConstantBuffer<MapCloudParams> clouds : FS_BLOCK(0);

// The coarsest octave's cells across the texture: every octave repeats across it.
static const int kCells = 4;
// Towards the sun, in the texture (u runs right to left on the map's layers, v down): the screen's upper left.
static const float2 kSun = float2(0.55, -0.835);

uint Hash(uint x) {
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

// The unit gradient at lattice point `c`, the lattice repeating every `period` points.
float2 Gradient(int2 c, int period, uint seed) {
    c = (c % period + period) % period;
    float a = float(Hash(uint(c.x) + Hash(uint(c.y) ^ seed)) >> 8) * (6.2831853 / 16777216.0);
    return float2(cos(a), sin(a));
}

// Gradient noise at `p` (lattice units), about -0.7 .. 0.7, repeating every `period` units.
float Noise(float2 p, int period, uint seed) {
    float2 i = floor(p);
    float2 f = p - i;
    int2 c = int2(i);
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float a = dot(Gradient(c, period, seed), f);
    float b = dot(Gradient(c + int2(1, 0), period, seed), f - float2(1.0, 0.0));
    float d = dot(Gradient(c + int2(0, 1), period, seed), f - float2(0.0, 1.0));
    float e = dot(Gradient(c + int2(1, 1), period, seed), f - float2(1.0, 1.0));
    return lerp(lerp(a, b, u.x), lerp(d, e, u.x), u.y);
}

// `octaves` octaves of noise at `p` (coarsest cells), each twice as fine as the one before and a little more than
// half as strong, moved by its own drift (`t`: the drift's clock, in coarsest cells), summed to about -0.7 .. 0.7.
// From octave `billows` on each takes the noise's size less its mean: rounded puffs with creases between.
float Fbm(float2 p, int octaves, int billows, float t, uint seed) {
    float sum = 0.0, weight = 0.5, total = 0.0;
    int period = kCells;
    float scale = 1.0;
    for (int k = 0; k < octaves; ++k) {
        // Each octave its own direction, at about a third of a coarsest cell per unit of t (whole 1/64ths: the
        // clock's wrap moves each by whole repeats).
        float angle = float(k) * 2.4;
        float2 drift = round(float2(cos(angle), sin(angle)) * 0.35 * 64.0) / 64.0;
        float n = Noise((p + drift * t) * scale, period, seed + uint(k) * 0x9E3779B9u);
        if (k >= billows) n = (abs(n) - 0.2) * 1.6;
        sum += weight * n;
        total += weight;
        weight *= 0.55;
        scale *= 2.0;
        period *= 2;
    }
    return sum / total;
}

float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint seed = asuint(clouds.seed);
    float2 p = pos.xy / clouds.size * float(kCells);
    float t = clouds.time * (1.0 / 16.0);
    // The warp: the point moved by two slow noises of its own, which bends the octaves into billows and streaks.
    float2 warp = float2(Fbm(p + float2(1.7, 9.2), 4, 4, t * 0.5, seed ^ 0x51ED27u),
                         Fbm(p + float2(8.3, 2.8), 4, 4, -t * 0.5, seed ^ 0xA3C59Bu));
    float2 q = p + warp * 0.7;
    float density = 0.5 + 1.4 * Fbm(q, 7, 7, t, seed);
    float alpha = 1.0 - pow(clouds.base, max(0.0, 255.0 * density - clouds.start));
    // The light: the density a little towards the sun against here (rising: this side faces away), and the thick
    // parts greyer.
    float near = Fbm(q, 4, 4, t, seed);
    float sunward = Fbm(q + kSun * 0.06, 4, 4, t, seed);
    float lit = saturate(0.75 + (near - sunward) * 8.0);
    float shade = lerp(1.0, lit, saturate(alpha * 1.5)) * lerp(1.0, 0.72, alpha * alpha);
    return float4(shade, shade, shade, alpha);
}
#endif
