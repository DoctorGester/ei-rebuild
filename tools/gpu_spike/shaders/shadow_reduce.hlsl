// The shadow map's depth range per cell (src/render/shaders/shadow_reduce.glsl) for SDL_GPU (the spike): the nearest
// and farthest texel of each 16 x 16 cell, four texels a gather. The map t0 / s0 in space2.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float> map : register(t0, space2);
SamplerState point_smp : register(s0, space2);
static const int kCell = 16;
static const float kTexels = 4096.0;
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    float2 base = floor(pos.xy) * kCell;
    float lo = 1.0, hi = 0.0;
    for (int j = 0; j < kCell / 2; ++j) {
        for (int i = 0; i < kCell / 2; ++i) {
            float4 g = map.GatherRed(point_smp, (base + float2(i * 2 + 1, j * 2 + 1)) / kTexels);
            lo = min(lo, min(min(g.x, g.y), min(g.z, g.w)));
            hi = max(hi, max(max(g.x, g.y), max(g.z, g.w)));
        }
    }
    return float4(lo, hi, 0.0, 0.0);
}
#endif
