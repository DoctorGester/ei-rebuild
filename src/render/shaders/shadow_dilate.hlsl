// The nearest and farthest depth of the cells within `cellsAround` cells around each (render_shadows.cpp: enough for the
// receivers' search and filter), with the cell's own nearest.
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
struct ShadowDilateParams {
    int cellsAround;
    int lastCell;
};
ConstantBuffer<ShadowDilateParams> params : FS_BLOCK(0);
Texture2D<float4> cells : FS_TEXTURE(0);
SamplerState point_smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 c = int2(pos.xy);
    int last = params.lastCell;
    float lo = 1.0, hi = 0.0;
    for (int j = -params.cellsAround; j <= params.cellsAround; ++j) {
        for (int i = -params.cellsAround; i <= params.cellsAround; ++i) {
            float4 r = cells.Load(int3(clamp(c + int2(i, j), 0, last), 0));
            lo = min(lo, r.x);
            hi = max(hi, r.y);
        }
    }
    return float4(lo, hi, cells.Load(int3(c, 0)).x, 0.0);
}
#endif
