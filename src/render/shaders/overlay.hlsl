// A tool's panel over the frame (render_overlay.cpp): textured, coloured triangles in frame pixels (dear imgui's draw
// lists), alpha-blended over what the frame holds.
#include "rect.hlsli"
struct VsIn {
    [[vk::location(0)]] float2 position : TEXCOORD0;   // frame pixels
    [[vk::location(1)]] float2 uv : TEXCOORD1;
    [[vk::location(2)]] float4 colour : TEXCOORD2;
};
struct VsOut {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] float4 colour : TEXCOORD1;
    float4 pos : SV_Position;
};
#ifdef EI_VERTEX
VsOut vs_main(VsIn i) {
    VsOut o;
    o.pos = to_clip(i.position);
    o.uv = i.uv;
    o.colour = i.colour;
    return o;
}
#endif
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(VsOut i) : SV_Target0 {
    return i.colour * tex0.Sample(smp, i.uv);
}
#endif
