// The landscape's pages (render_terrain.cpp).
#include "shadow_receiver.hlsli"
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
struct vs_in { float3 pos : POSITION; float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; };
struct vs_out { float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; float4 shadow : TEXCOORD1;
                float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    o.pos.xy += flags.zw * o.pos.w;   // Direct3D 7 pixel centres (render_internal.h)
    o.col = i.col.zyxw;     // D3DCOLOR bytes are B, G, R, A
    o.spec = i.spec.zyxw;
    o.uv = i.uv;
    o.shadow = float4(dot(lrow0, p), dot(lrow1, p), dot(lrow2, p), 1.0);
    return o;
}
Texture2D<float4> tex : register(t0);
SamplerState smp : register(s0);
Texture2D<float> shadow_map : register(t1);
SamplerComparisonState shadow_smp : register(s1);
Texture2D<float4> shadow_range : register(t2);
SamplerState shadow_point : register(s2);
float4 ps_main(vs_out i) : SV_Target0 {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    float4 t = tex.Sample(smp, i.uv);
    if (flags.x > 1.5) return float4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
    float3 c = saturate(t.rgb * i.col.rgb + i.spec.rgb);
    if (shadow_on.x > 0.5)
        c *= 1.0 - shadow_amount(shadow_map, shadow_smp, shadow_range, i.shadow.xyz, dsx, dsy, shadow_on, shadow_scale, shadow_camera,
                                shadow_soft, i.pos.xy);
    float a = flags.x > 0.5 ? i.col.a : t.a;
    if (flags.y > 0.5) c = lerp(fog.rgb, c, i.spec.a);
    return float4(c, a);
}
