// The casters into a depth map (src/render/shaders/shadow_caster.glsl) for SDL_GPU (the spike): positions through
// the instance's light matrix rows, cut out where the stage-0 texel's alpha is below the instance's least. Depth
// only; the texture t0 / s0 in space2.
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float2 uv : TEXCOORD1;
    [[vk::location(2)]] float4 row0 : TEXCOORD2;
    [[vk::location(3)]] float4 row1 : TEXCOORD3;
    [[vk::location(4)]] float4 row2 : TEXCOORD4;
    [[vk::location(5)]] float4 row3 : TEXCOORD5;
    [[vk::location(6)]] float4 cut : TEXCOORD6;   // xy: the texture offsets, z: cut out, w: the least alpha kept
};
struct VsOut {
    [[vk::location(0)]] float2 uv : TEXCOORD0;
    [[vk::location(1)]] nointerpolation float2 alpha : TEXCOORD1;
    float4 pos : SV_Position;
};
VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(i.row0, p), dot(i.row1, p), dot(i.row2, p), dot(i.row3, p));
    o.pos.z = max(o.pos.z, 0.0);
    o.uv = i.uv + i.cut.xy;
    o.alpha = i.cut.zw;
    return o;
}
#ifdef EI_FRAGMENT
Texture2D<float4> tex : register(t0, space2);
SamplerState smp : register(s0, space2);
void ps_main(VsOut i) {
    if (i.alpha.x > 0.5 && tex.SampleLevel(smp, i.uv, 0.0).a < i.alpha.y) discard;
}
#endif
