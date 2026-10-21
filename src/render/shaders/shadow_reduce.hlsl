// The nearest and farthest texel of each kRangeCell x kRangeCell cell of the shadow map (render_shadows.cpp;
// Gather, shader model 4.1: four texels a fetch). The constants are render_shadows.cpp's (its static_assert).
Texture2D<float> map : register(t0);
SamplerState point_smp : register(s0);
static const int kCell = 16;          // kRangeCell
static const float kTexels = 4096.0;  // kMapSize
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    float2 base = floor(pos.xy) * kCell;
    float lo = 1.0, hi = 0.0;
    [unroll] for (int j = 0; j < kCell / 2; ++j) {
        [unroll] for (int i = 0; i < kCell / 2; ++i) {
            float4 g = map.Gather(point_smp, (base + float2(i * 2 + 1, j * 2 + 1)) / kTexels);
            lo = min(lo, min(min(g.x, g.y), min(g.z, g.w)));
            hi = max(hi, max(max(g.x, g.y), max(g.z, g.w)));
        }
    }
    return float4(lo, hi, 0.0, 0.0);
}
