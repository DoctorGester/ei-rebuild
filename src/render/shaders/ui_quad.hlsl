// The interface's quads drawn into the frame (render_ui.cpp): solid rectangles and glyphs, the glyphs' coverage read
// from the atlas, alpha-blended over what the frame holds.
#include "rect.hlsli"
struct VsIn {
    [[vk::location(0)]] float4 position : TEXCOORD0;   // xy: frame pixels; zw: atlas texels, x < 0 full coverage
    [[vk::location(1)]] float4 colour : TEXCOORD1;
};
struct VsOut {
    [[vk::location(0)]] float2 atlas : TEXCOORD0;
    [[vk::location(1)]] float4 colour : TEXCOORD1;
    float4 pos : SV_Position;
};
#ifdef EI_VERTEX
VsOut vs_main(VsIn i) {
    VsOut o;
    o.pos = to_clip(i.position.xy);
    o.atlas = i.position.zw;
    o.colour = i.colour;
    return o;
}
#endif
#ifdef EI_FRAGMENT
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
float4 ps_main(VsOut i) : SV_Target0 {
    float coverage = i.atlas.x < 0.0 ? 1.0 : tex0.Load(int3(int2(floor(i.atlas)), 0)).r;
    if (coverage <= 0.0) discard;
    return float4(i.colour.rgb, i.colour.a * coverage);
}
#endif
