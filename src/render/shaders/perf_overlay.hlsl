// The performance overlay (render_perf.cpp): its text image (one channel: 0 the box, else the text's brightness)
// scaled up by whole pixels over the back buffer, the box translucent black.
#include "rect_vs.hlsli"
#ifdef EI_FRAGMENT
struct PerfOverlayParams {
    float4 origin;   // x, y: the box's top left corner (pixels); z: screen pixels per image pixel
};
ConstantBuffer<PerfOverlayParams> params : FS_BLOCK(0);
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(float4 pos : SV_Position) : SV_Target0 {
    int2 texel = int2(floor((pos.xy - params.origin.xy) / params.origin.z));
    float v = tex0.Load(int3(texel, 0)).r;
    return v > 0.0 ? float4(v, v, v, 1.0) : float4(0.0, 0.0, 0.0, 0.6);
}
#endif
