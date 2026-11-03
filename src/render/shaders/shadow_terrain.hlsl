// The ground's casters into the shadow map (render_shadows.cpp, render_terrain.cpp): the sectors' ground vertices
// through the light matrix. Only the faces turned away from the sun write their depth (as the vertex normals say): a
// lit face never hides itself, the far side of a ridge hides what lies behind it. Depth only.
#include "bindings.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float3 normal : TEXCOORD1;
};
struct VsOut {
    [[vk::location(0)]] float3 normal : TEXCOORD0;
    float4 pos : SV_Position;
};
#ifdef EI_VERTEX
struct ShadowTerrainVsParams {
    row_major float4x4 light;   // to the shadow map
};
ConstantBuffer<ShadowTerrainVsParams> vs : VS_BLOCK(0);
VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.light[0], p), dot(vs.light[1], p), dot(vs.light[2], p), dot(vs.light[3], p));
    o.pos.z = max(o.pos.z, 0.0);   // before the near plane: at it (as shadow_caster.hlsl)
    o.normal = i.normal;
    return o;
}
#endif
#ifdef EI_FRAGMENT
struct ShadowTerrainFsParams {
    float4 sun;   // xyz: the direction the sun's light travels
};
ConstantBuffer<ShadowTerrainFsParams> fs : FS_BLOCK(0);
void ps_main(VsOut i) {
    if (dot(i.normal, fs.sun.xyz) < 0.0) discard;
}
#endif
