// The landscape's pages (render_terrain.cpp), lit here (scene_lighting.hlsli): the vertices carry the normal, the
// material colour (colour word) and, on the ground, the light lost under the water (`loss`, 0 or more; below 0 on the
// water layer, whose specular word carries the wave crests).
#include "bindings.hlsli"
#include "depth_copy.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float3 normal : TEXCOORD4;
    [[vk::location(5)]] float loss : TEXCOORD5;
};
struct VsOut {
    [[vk::location(0)]] float4 col : TEXCOORD0;
    [[vk::location(1)]] float4 spec : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 shadow : TEXCOORD3;
    [[vk::location(4)]] float3 world : TEXCOORD4;
    [[vk::location(5)]] float3 normal : TEXCOORD5;
    [[vk::location(6)]] float loss : TEXCOORD6;
    float4 pos : SV_Position;
};

#ifdef EI_VERTEX
struct TerrainVsParams {
    row_major float4x4 matrix;
    row_major float4x4 light;   // to the shadow map
    float4 shift;               // xy: the half-pixel shift (render_internal.h)
    float4 normalOffset;        // x: how far along its normal a receiver's point is looked up in the map
};
ConstantBuffer<TerrainVsParams> vs : VS_BLOCK(0);
VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.matrix[0], p), dot(vs.matrix[1], p), dot(vs.matrix[2], p), dot(vs.matrix[3], p));
    o.pos.xy += vs.shift.xy * o.pos.w;   // Direct3D 7 pixel centres (render_internal.h)
    o.col = i.col.zyxw;                  // D3DCOLOR bytes are B, G, R, A
    o.spec = i.spec.zyxw;
    o.uv = i.uv;
    float4 q = float4(i.pos + i.normal * vs.normalOffset.x, 1.0);
    o.shadow = float4(dot(vs.light[0], q), dot(vs.light[1], q), dot(vs.light[2], q), 1.0);
    o.world = i.pos;
    o.normal = i.normal;
    o.loss = i.loss;
    return o;
}
#endif

#ifdef EI_FRAGMENT
#define SHADOW_BLOCK_SLOT 1
#define SHADOW_MAP_SLOT 1
#define SHADOW_RANGE_SLOT 2
#define SCENE_BLOCK_SLOT 2
#define POINT_BLOCK_SLOT 3
#define POINT_MAP_SLOT 3
#define CLOUD_MAP_SLOT 4
#include "shadow_receiver.hlsli"
#include "point_shadow.hlsli"
#include "cloud_shadow.hlsli"
#include "scene_lighting.hlsli"
struct TerrainFsParams {
    float4 fog;     // rgb: the fog colour
    float4 flags;   // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels), y: fog on
};
ConstantBuffer<TerrainFsParams> fs : FS_BLOCK(0);
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);

float4 Shade(VsOut i) {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    float4 t = tex.Sample(smp, i.uv);
    if (fs.flags.x > 1.5) return float4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
    // How much of the sun the map and the clouds hide (the water takes no map: the clouds only).
    float hidden = shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, 2.0);
    hidden = with_clouds(hidden, i.shadow.xy);
    if (shadow.on.z > 0.5) return float4((1.0 - hidden).xxx, 1.0);   // the harness's shadowview
    float len = length(i.normal);
    float3 n = len > 1e-6 ? i.normal / len : float3(0.0, 0.0, 1.0);
    float3 spec, c;
    // The water: the clouds' shade only (the map's falls on the ground under it); the crests or the specular lights.
    if (i.loss < 0.0) {
        float3 lit = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, hidden, shadow.on.y, false, 0.0, spec);
        c = saturate(t.rgb * lit + max(spec, i.spec.rgb));
    } else {
        float3 lit = ground_light(i.world, n, i.col.rgb, 0.0, i.loss, hidden, shadow.on.y, pointShadow.on.x > 0.5,
                                  pixel_turn(i.pos.xy), spec);
        c = saturate(t.rgb * lit + spec);
    }
    float a = fs.flags.x > 0.5 ? i.col.a : t.a;
    if (fs.flags.y > 0.5) c = lerp(fs.fog.rgb, c, i.spec.a);
    return float4(c, a);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
