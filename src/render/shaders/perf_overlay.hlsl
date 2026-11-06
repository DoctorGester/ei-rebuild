// The performance overlay (render_perf.cpp): its text image (one channel: the text's coverage, 0 in the backing) over the
// back buffer, the backing translucent black.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct PerfOverlayParams {
    float2 origin;   // the box's top left corner (pixels)
};
ConstantBuffer<PerfOverlayParams> params : FS_BLOCK(0);
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 texel = int2(floor(pos.xy - params.origin));
    float v = tex0.Load(int3(texel, 0)).r;
    // White text of coverage v over the backing's 60% black: the blend (alpha, 1 - alpha) gives v white + (1 - v) 40% of
    // the frame.
    float a = v + (1.0 - v) * 0.6;
    return float4(v / a, v / a, v / a, a);
}
#endif
