// The casters into a depth map (render_shadows.cpp's CasterDraws: the sun's map, the point lights' faces):
// positions through the light matrix; a pass with alpha test or blending cut out where its stage-0 texel's alpha
// is below the reference. The texel is the texture's own (level 0): seen from the sun at a grazing angle a smaller
// level's averaged alpha would cut out edges and fronds that the camera's view keeps. The caster's own data is
// the draw's instance (CasterInstance): its light matrix by rows, its texture offsets and its cut.
struct vs_in {
    float3 pos : POSITION;
    float2 uv : TEXCOORD0;
    float4 row0 : TEXCOORD1;
    float4 row1 : TEXCOORD2;
    float4 row2 : TEXCOORD3;
    float4 row3 : TEXCOORD4;
    float4 cut : TEXCOORD5;   // xy: the texture offsets, z: cut out, w: the least alpha kept
};
struct vs_out { float2 uv : TEXCOORD0; nointerpolation float2 alpha : TEXCOORD1; float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(i.row0, p), dot(i.row1, p), dot(i.row2, p), dot(i.row3, p));
    // A caster before the near plane (the map starts at the nearest one the world's draw found)
    // is drawn at it: it still hides everything behind it (the map's projection is orthographic, w 1).
    o.pos.z = max(o.pos.z, 0.0);
    o.uv = i.uv + i.cut.xy;
    o.alpha = i.cut.zw;
    return o;
}
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
// Depth only.
void ps_main(vs_out i) {
    if (i.alpha.x > 0.5 && tex.SampleLevel(smp, i.uv, 0.0).a < i.alpha.y) discard;
}
