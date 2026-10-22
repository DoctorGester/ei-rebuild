// The ground's casters into the shadow map (R16i; render_shadows.cpp, render_terrain.cpp): the sectors'
// ground vertices through the light matrix. Only the faces turned away from the sun write their depth (as
// the vertex normals say): a lit face never hides itself, the far side of a ridge hides what lies behind it.
cbuffer vs_params : register(b0) { float4 row0; float4 row1; float4 row2; float4 row3; };
cbuffer fs_params : register(b0) { float4 sun; };   // xyz: the direction the sun's light travels
struct vs_in { float3 pos : POSITION; float3 normal : NORMAL; };
struct vs_out { float3 normal : TEXCOORD0; float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    o.pos.z = max(o.pos.z, 0.0);   // before the near plane: at it (as shadow_caster.hlsl)
    o.normal = i.normal;
    return o;
}
// The caster id image (R22) gets 0: the ground is no object.
uint ps_main(vs_out i) : SV_Target0 {
    if (dot(i.normal, sun.xyz) < 0.0) discard;
    return 0;
}
