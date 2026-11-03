// The nearest and farthest depth of the cells around each (src/render/shaders/shadow_dilate.glsl) for SDL_GPU (the
// spike), with the cell's own nearest. The cells t0 / s0 in space2, the block b0 in space3.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
struct Params {
    int4 reach;   // x: cells around, y: the last cell
};
ConstantBuffer<Params> params : register(b0, space3);
Texture2D<float4> cells : register(t0, space2);
SamplerState point_smp : register(s0, space2);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 c = int2(pos.xy);
    int last = params.reach.y;
    float lo = 1.0, hi = 0.0;
    for (int j = -params.reach.x; j <= params.reach.x; ++j) {
        for (int i = -params.reach.x; i <= params.reach.x; ++i) {
            float4 r = cells.Load(int3(clamp(c + int2(i, j), 0, last), 0));
            lo = min(lo, r.x);
            hi = max(hi, r.y);
        }
    }
    return float4(lo, hi, cells.Load(int3(c, 0)).x, 0.0);
}
#endif
