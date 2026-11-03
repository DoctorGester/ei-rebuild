// The present (render_window.cpp): the frame onto the back buffer through the game's gamma ramp.
#include "bindings.hlsli"
struct VsOut {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    float4 pos : SV_Position;
};
#ifdef EI_VERTEX
struct PresentParams {
    float4 source;   // the frame's part shown: left, top, right, bottom (0..1)
};
ConstantBuffer<PresentParams> params : VS_BLOCK(0);
VsOut vs_main(uint id : SV_VertexID) {
    float2 p = float2((id << 1) & 2, id & 2);
    VsOut o;
    o.pos = float4(p.x * 2.0 - 1.0, 1.0 - p.y * 2.0, 0.0, 1.0);
    o.uv = lerp(params.source.xy, params.source.zw, p);
    return o;
}
#endif
#ifdef EI_FRAGMENT
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
Texture2D<float4> ramp : FS_TEXTURE(1);
SamplerState ramp_smp : FS_SAMPLER(1);
float4 ps_main(VsOut i) : SV_Target0 {
    float3 colour = tex.SampleLevel(smp, i.uv, 0.0).rgb;
    int3 k = int3(saturate(colour) * 255.0 + 0.5);
    return float4(ramp.Load(int3(k.r, 0, 0)).r, ramp.Load(int3(k.g, 0, 0)).g, ramp.Load(int3(k.b, 0, 0)).b, 1.0);
}
#endif
