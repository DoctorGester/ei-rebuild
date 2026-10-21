// The casters into the shadow map (render_shadows.cpp): positions through the light matrix; a pass with
// alpha test or blending cut out where its stage-0 texel's alpha is below the reference. The texel is the
// texture's own (level 0): seen from the sun at a grazing angle a smaller level's averaged alpha would cut
// out edges and fronds that the camera's view keeps.
cbuffer vs_params : register(b0) { float4 row0; float4 row1; float4 row2; float4 row3; float4 uv_offset; };
cbuffer fs_params : register(b0) { float4 alpha; };   // x: cut out, y: the least alpha kept
struct vs_in { float3 pos : POSITION; float2 uv : TEXCOORD0; };
struct vs_out { float2 uv : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    o.uv = i.uv + uv_offset.xy;
    return o;
}
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
void ps_main(vs_out i) {
    if (alpha.x > 0.5 && tex.SampleLevel(smp, i.uv, 0.0).a < alpha.y) discard;
}
