// One frame of the global map's clouds (clouds.hlsli): grey levels from the four layers' random words.
#include "clouds.hlsli"
#ifdef EI_FRAGMENT
uint Word(uint i) {
    int4 four = clouds.words[i >> 2];
    return asuint(four[i & 3u]);
}

// Layer k's raw noise at cell `cell` of its 32 x 32 (wrapped to 0..1023: row cell / 32, column cell % 32).
uint Noise(uint k, uint cell) {
    cell &= 0x3FFu;
    uint seed = (cell >> 5) + (cell & 31u) * 0x39u;
    uint m = (seed << 13) ^ seed;
    return ((m * m * Word(k * 3u) + Word(k * 3u + 1u)) * m + Word(k * 3u + 2u)) & 0x1FFFFFFFu;
}

// Layer k's value (0..255) at cell `cell`: its noise smoothed with the 8 cells around it in the 1024 cells' order (a
// row's first cell's left neighbour is the row above's last).
int Layer(uint k, uint cell) {
    uint edges = (Noise(k, cell - 32u) + Noise(k, cell - 1u) + Noise(k, cell + 1u) + Noise(k, cell + 32u)) >> 3;
    uint corners = (Noise(k, cell - 33u) + Noise(k, cell - 31u) + Noise(k, cell + 31u) + Noise(k, cell + 33u)) >> 4;
    return int((edges + corners + (Noise(k, cell) >> 2)) >> 21);
}

// Layer k's cells `scale` pixels wide (scale = 1 << shift) bilinearly weighted at pixel p, out of scale x scale; the
// cell to the right of a row's last is the row's first.
int Octave(uint k, uint2 p, int scale, uint shift) {
    uint cell = ((p.y >> shift) & 31u) * 32u + ((p.x >> shift) & 31u);
    uint right = ((cell + 1u) & 31u) != 0u ? cell + 1u : cell - 31u;
    int fx = int(p.x & uint(scale - 1));
    int fy = int(p.y & uint(scale - 1));
    int lower = (Layer(k, right + 32u) * fx + Layer(k, cell + 32u) * (scale - fx)) * fy;
    int upper = (Layer(k, right) * fx + Layer(k, cell) * (scale - fx)) * (scale - fy);
    return lower + upper;
}

float ps_main(float4 pos : SV_Position) : SV_Target0 {
    uint2 p = uint2(pos.xy);
    int value = Octave(0u, p, 8, 3u) - 0x1C00;
    value += Octave(1u, p, 4, 2u) * 2;
    int fine = Octave(2u, p, 2, 1u) + Layer(3u, (p.y & 31u) * 32u + (p.x & 31u)) * 2;
    value = (value + fine * 4) >> 6;
    // Over 255 is 255; under 0 keeps its low byte (the game's byte store); as an R8 texel (exactly v / 255).
    return float(uint(min(value, 255)) & 0xFFu) / 255.0;
}
#endif
