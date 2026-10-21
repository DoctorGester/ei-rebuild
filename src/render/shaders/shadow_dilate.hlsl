// The nearest and farthest depth of the cells within `reach.x` cells around each (render_shadows.cpp: enough
// for the receivers' search and filter), with the cell's own nearest.
cbuffer fs_params : register(b0) { int4 reach; };   // x: cells around, y: the last cell
Texture2D<float4> cells : register(t0);
SamplerState point_smp : register(s0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 c = int2(pos.xy);
    int last = reach.y;
    float lo = 1.0, hi = 0.0;
    for (int j = -reach.x; j <= reach.x; ++j) {
        for (int i = -reach.x; i <= reach.x; ++i) {
            float4 r = cells.Load(int3(clamp(c + int2(i, j), 0, last), 0));
            lo = min(lo, r.x);
            hi = max(hi, r.y);
        }
    }
    return float4(lo, hi, cells.Load(int3(c, 0)).x, 0.0);
}
