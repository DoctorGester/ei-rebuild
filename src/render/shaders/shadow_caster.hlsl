// The casters into a depth map (render_shadows.cpp's CasterDraws: the sun's map, the point lights' faces): positions
// through the light matrix; a pass with alpha test or blending cut out where its stage-0 texel's alpha is below the
// reference. The texel is the texture's own (level 0): seen from the sun at a grazing angle a smaller level's averaged
// alpha would cut out edges and fronds that the camera's view keeps. The caster's own data is the draw's instance
// (CasterInstance): its light matrix by rows, its texture offsets and its cut. Depth only.
#include "bindings.hlsli"
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
#ifdef EI_VERTEX
VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(i.row0, p), dot(i.row1, p), dot(i.row2, p), dot(i.row3, p));
    // A caster before the near plane (the map starts at the nearest one the world's draw found) is drawn at it: it
    // still hides everything behind it (the map's projection is orthographic, w 1).
    o.pos.z = max(o.pos.z, 0.0);
    o.uv = i.uv + i.cut.xy;
    o.alpha = i.cut.zw;
    return o;
}
#endif
#ifdef EI_FRAGMENT
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
void ps_main(VsOut i) {
    if (i.alpha.x > 0.5 && tex.SampleLevel(smp, i.uv, 0.0).a < i.alpha.y) discard;
}
#endif
