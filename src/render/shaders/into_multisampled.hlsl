// The multisampling's copy into the multisampled targets (render_smooth.cpp): every sample of a pixel takes the
// source's colour and depth at that pixel, and the depth copy that depth (depth_copy.hlsli).
#include "fullscreen.hlsli"
#ifdef EI_FRAGMENT
Texture2D<float4> colour : FS_TEXTURE(0);
SamplerState copy_smp : FS_SAMPLER(0);
Texture2D<float> depth : FS_TEXTURE(1);
SamplerState depth_smp : FS_SAMPLER(1);
struct PsOut {
    float4 colour : SV_Target0;
    float depthCopy : SV_Target1;
    float depth : SV_Depth;
};
PsOut ps_main(float4 pos : SV_Position) {
    int3 p = int3(int2(pos.xy), 0);
    PsOut o;
    o.depth = depth.Load(p);
    o.colour = colour.Load(p);
    o.depthCopy = o.depth;
    return o;
}
#endif
