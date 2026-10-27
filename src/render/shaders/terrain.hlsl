// The landscape's pages (render_terrain.cpp), lit here (R16i, scene_lighting.hlsli): the vertices carry the
// normal, the material colour (colour word) and, on the ground, the light lost under the water (`loss`, 0
// or more; below 0 on the water layer, whose specular word the game keeps: its specular lights and crests).
#include "shadow_receiver.hlsli"
#define POINT_SHADOW_B b2
#define POINT_SHADOW_T t3
#define POINT_SHADOW_S s3
#include "point_shadow.hlsli"
#include "scene_lighting.hlsli"
cbuffer params : register(b0) {
    float4 row0; float4 row1; float4 row2; float4 row3;
    float4 fog;     // rgb: the fog colour
    float4 flags;   // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels),
                    // y: fog on; zw: the half-pixel shift
    float4 lrow0; float4 lrow1; float4 lrow2; float4 lrow3;   // to the shadow map (R16j)
    float4 shadow_on;   // the shadow map (render_shadows.cpp, ShadowParams)
    float4 shadow_scale;
    float4 shadow_camera;
    float4 shadow_soft;
};
struct vs_in { float3 pos : POSITION; float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0;
               float3 normal : NORMAL; float loss : TEXCOORD1; };
struct vs_out { float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; float4 shadow : TEXCOORD1;
                float3 world : TEXCOORD2; float3 normal : TEXCOORD3; float loss : TEXCOORD4; float4 pos : SV_Position; };
// How far along its normal a receiver's point is moved before it is looked up in the map, in texels.
static const float kNormalOffset = 1.5;
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    o.pos.xy += flags.zw * o.pos.w;   // Direct3D 7 pixel centres (render_internal.h)
    o.col = i.col.zyxw;     // D3DCOLOR bytes are B, G, R, A
    o.spec = i.spec.zyxw;
    o.uv = i.uv;
    // The normal offset (the map's texel in world units: its extent over its texels).
    float texel = shadow_on.x > 0.5 ? shadow_scale.x * 2.0 / shadow_soft.w : 0.0;
    float4 q = float4(i.pos + i.normal * (texel * kNormalOffset), 1.0);
    o.shadow = float4(dot(lrow0, q), dot(lrow1, q), dot(lrow2, q), 1.0);
    o.world = i.pos;
    o.normal = i.normal;
    o.loss = i.loss;
    return o;
}
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
Texture2D<float> shadow_map : register(t1);
SamplerComparisonState shadow_smp : register(s1);
Texture2D<float4> shadow_range : register(t2);
SamplerState shadow_linear : register(s2);   // the range, filtered (R25)
float4 ps_main(vs_out i) : SV_Target0 {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    float4 t = tex.Sample(smp, i.uv);
    if (flags.x > 1.5) return float4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
    float hidden = 0.0;   // how much of the sun the map hides
    if (shadow_on.x > 0.5) {
        hidden = shadow_amount(shadow_map, shadow_smp, shadow_range, shadow_linear, i.shadow.xyz, dsx, dsy, shadow_on,
                               shadow_scale, shadow_soft, i.pos.xy, 2.0);
        if (shadow_on.z > 0.5) return float4((1.0 - hidden).xxx, 1.0);   // the harness's shadowview
    }
    float len = length(i.normal);
    float3 n = len > 1e-6 ? i.normal / len : float3(0.0, 0.0, 1.0);
    float3 spec, c;
    if (i.loss < 0.0) {   // the water: no shadow test in the game's lighting, its own specular word
        float3 light = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, 0.0, 0.0, false, 0.0, spec);
        c = saturate(t.rgb * light + i.spec.rgb);
    } else {
        float3 light = ground_light(i.world, n, i.col.rgb, 0.0, i.loss, hidden, shadow_on.y, point_shadow_on.x > 0.5,
                                    pixel_turn(i.pos.xy), spec);
        c = saturate(t.rgb * light + spec);
    }
    float a = flags.x > 0.5 ? i.col.a : t.a;
    if (flags.y > 0.5) c = lerp(fog.rgb, c, i.spec.a);
    return float4(c, a);
}
