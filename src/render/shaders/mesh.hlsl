// The meshes (render_mesh.cpp): the fixed-function texture stages of the old device as the game sets them
// (D3DTEXTUREOP and D3DTA values), evaluated per pixel; then specular, fog and the alpha test. A figure
// part lit on the GPU (R16i, render::MeshLighting) takes its diffuse colour from `figure_light` instead of
// the vertex colour (whose alpha it keeps).
#include "shadow_receiver.hlsli"
cbuffer vs_params : register(b0) {
    float4 row0; float4 row1; float4 row2; float4 row3;
    float4 distance_fog;   // x: on, y: start, z: 1 / (end - start); w: keep z inside the depth range
    float4 shift;          // xy: the half-pixel shift (render_internal.h); z: depth scale
    float4 lrow0; float4 lrow1; float4 lrow2; float4 lrow3;   // model space to the shadow map (R16j)
    float4 uv_offset;      // xy: added to the texture coordinates (a mesh kept on the GPU, R16d)
    float4 normal_offset;  // x: how far along its normal a receiver's point is looked up in the map
};
cbuffer fs_params : register(b0) {
    float4 fog_colour;     // rgb
    float4 factor;         // TEXTUREFACTOR
    int4 colour_ops;       // stage 0: op, arg1, arg2; w: stage 1 op
    int4 colour_ops1;      // stage 1: arg1, arg2
    int4 alpha_ops;        // stage 0: op, arg1, arg2; w: stage 1 op
    int4 alpha_ops1;       // stage 1: arg1, arg2
    int4 switches;         // x: specular, y: fog, z: alpha test func (0 off), w: alpha reference
    float4 patch_to;       // stage 0's patch (render::TexturePatch): its rectangle (u0, v0, u1, v1), zero: none
    float4 patch_map;      // the patch texture's coordinates: uv * xy + zw
    float4 patch_inside;   // ... kept inside its rectangle (u0, v0, u1, v1)
    float4 shadow_on;      // the shadow map (render_shadows.cpp, ShadowParams)
    float4 shadow_scale;
    float4 shadow_camera;
    float4 shadow_soft;
    float4 light_on;       // x: lit here, y: the crown's pull, z: the lights (render_internal.h, MeshLightParams)
    float4 light_sun;      // xyz: towards the sun in model space
    float4 light_sun_colour;
    float4 light_ambient;
    float4 light_emissive;
    float4 light_map_sun;  // xyz: towards the shadow map's light in model space (unit)
    float4 light_points[16];   // per light: model-space position, 1 / range squared; lit colour
};
struct vs_in { float3 pos : POSITION; float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0;
               float3 normal : NORMAL; };
struct vs_out { float4 col : COLOR0; float4 spec : COLOR1; float2 uv : TEXCOORD0; float4 shadow : TEXCOORD1;
                float3 model : TEXCOORD2; float3 normal : TEXCOORD3; float4 pos : SV_Position; };
vs_out vs_main(vs_in i) {
    float4 p = float4(i.pos, 1.0);
    vs_out o;
    o.pos = float4(dot(row0, p), dot(row1, p), dot(row2, p), dot(row3, p));
    // Meshes the old path projects to the screen without clipping (the interface's) are not cut at
    // the near and far planes there either.
    if (distance_fog.w > 0.5) o.pos.z = clamp(o.pos.z, 0.0, o.pos.w);
    o.pos.z *= shift.z;   // the flat geometry's depth scale (shadows drawn just in front)
    o.pos.xy += shift.xy * o.pos.w;
    o.col = i.col.zyxw;
    o.spec = i.spec.zyxw;
    // The old path's vertex fog by distance (3DfpFPU slot 35): the fog factor in the specular
    // alpha falls from the start of the range, the smaller of it and the vertex's own is kept.
    if (distance_fog.x > 0.5 && o.pos.w >= distance_fog.y)
        o.spec.a = min(o.spec.a, 1.0 - min((o.pos.w - distance_fog.y) * distance_fog.z, 1.0));
    o.uv = i.uv + uv_offset.xy;
    // The normal offset (R16i): the point looked up in the map moved along its normal (a figure part's).
    float len = length(i.normal);
    float4 q = len > 1e-6 ? float4(i.pos + i.normal * (normal_offset.x / len), 1.0) : p;
    o.shadow = float4(dot(lrow0, q), dot(lrow1, q), dot(lrow2, q), 1.0);
    o.model = i.pos;
    o.normal = i.normal;
    return o;
}
Texture2D<float4> tex0 : register(t0);
Texture2D<float4> tex1 : register(t1);
Texture2D<float4> tex2 : register(t2);   // stage 0's patch
SamplerState smp0 : register(s0);
SamplerState smp1 : register(s1);
SamplerState smp2 : register(s2);
Texture2D<float> shadow_map : register(t3);
SamplerComparisonState shadow_smp : register(s3);
Texture2D<float4> shadow_range : register(t4);
SamplerState shadow_linear : register(s4);   // the range, filtered (R25)
// A figure part's colour as 3DfpFPU's LightMesh lit it (render.h, MeshLighting), at the model-space point
// `p` with the normal `n` (as the mesh holds it): per channel the largest of the ambient colour, the sun's
// lit colour times the cosine to it and each light's lit colour times its cosine and falloff, plus the
// emissive colour, at most 1. `hidden`: how much of the sun the shadow map hides, `strength` its
// darkening: in the shade the sun's term gives way to the ambient colour, and is never lighter than the
// sunlit colour times 1 - strength on faces turned to the map's light (as the ground's, scene_lighting.hlsli).
// The crown: pulled towards its average colour, taken as the average over every direction a normal can
// face (the mean of max(ambient, sun x cosine) for a cosine spread evenly over -1 .. 1).
float3 figure_light(float3 p, float3 n, float hidden, float strength) {
    float3 ambient = light_ambient.rgb;
    float facing = dot(light_sun.xyz, n);
    float3 sunlit = max(ambient, light_sun_colour.rgb * facing);
    float turned = saturate(dot(light_map_sun.xyz, n) * rsqrt(max(dot(n, n), 1e-20)) * 6.0);
    float3 lit = min(lerp(sunlit, ambient, hidden), sunlit * (1.0 - hidden * strength * turned));
    int count = (int)light_on.z;
    for (int k = 0; k < count; ++k) {
        float4 at = light_points[2 * k], colour = light_points[2 * k + 1];
        float3 d = at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * at.w;
        if (falloff < 0.0) continue;
        float c = dot(d * rsqrt(max(distance2, 1e-12)), n);
        lit = max(lit, colour.rgb * c * falloff);
    }
    float3 c = min(1.0, lit + light_emissive.rgb);
    if (light_on.y > 0.0) {
        float3 s = light_sun_colour.rgb * length(light_sun.xyz);
        float3 t = clamp(ambient / max(s, 1e-6), -1.0, 1.0);
        float3 mean = 0.5 * (ambient * (t + 1.0) + s * (1.0 - t * t) * 0.5);
        c = lerp(c, min(1.0, mean + light_emissive.rgb), light_on.y);
    }
    return c;
}
float4 arg(int a, float4 current, float4 diffuse, float4 texel, float4 spec) {
    int sel = a & 15;
    float4 v = sel == 0 ? diffuse : sel == 2 ? texel : sel == 3 ? factor : sel == 4 ? spec : current;
    if (a & 16) v = 1.0 - v;
    if (a & 32) v = v.aaaa;
    return v;
}
float4 combine(int op, float4 a1, float4 a2, float4 current, float4 diffuse, float4 texel) {
    if (op == 2) return a1;
    if (op == 3) return a2;
    if (op == 5) return a1 * a2 * 2.0;
    if (op == 6) return a1 * a2 * 4.0;
    if (op == 7) return a1 + a2;
    if (op == 8) return a1 + a2 - 0.5;
    if (op == 9) return (a1 + a2 - 0.5) * 2.0;
    if (op == 10) return a1 - a2;
    if (op == 11) return a1 + a2 - a1 * a2;
    if (op == 12) return lerp(a2, a1, diffuse.a);
    if (op == 13) return lerp(a2, a1, texel.a);
    if (op == 14) return lerp(a2, a1, factor.a);
    if (op == 15) return a1 + a2 * (1.0 - texel.a);
    if (op == 16) return lerp(a2, a1, current.a);
    if (op == 18) return float4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op == 19) return float4(a1.rgb * a2.rgb, a1.a + a2.a);
    if (op == 20) return float4(a1.rgb + (1.0 - a1.a) * a2.rgb, a1.a);
    if (op == 24) return dot(a1.rgb * 2.0 - 1.0, a2.rgb * 2.0 - 1.0).xxxx;
    return a1 * a2;   // 4 MODULATE and anything not handled
}
bool passes(int func, float a, int reference) {
    int v = (int)round(saturate(a) * 255.0);
    if (func == 1) return false;
    if (func == 2) return v < reference;
    if (func == 3) return v == reference;
    if (func == 4) return v <= reference;
    if (func == 5) return v > reference;
    if (func == 6) return v != reference;
    if (func == 7) return v >= reference;
    return true;
}
float4 ps_main(vs_out i) : SV_Target0 {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    bool lit_here = light_on.x > 0.5;
    // How much of the sun the shadow map hides: on a figure part lit here by its normals; on the other
    // meshes (lit by the game, or not at all) only on faces turned to the sun (their face normal).
    float hidden = 0.0;
    if (shadow_on.x > 0.5) {
        hidden = shadow_amount(shadow_map, shadow_smp, shadow_range, shadow_linear, i.shadow.xyz, dsx, dsy, shadow_on,
                               shadow_scale, shadow_soft, i.pos.xy);
        if (!lit_here) hidden *= shadow_facing(i.shadow.xyz, dsx, dsy, shadow_scale, shadow_camera);
    }
    float4 diffuse = i.col;
    if (lit_here) diffuse.rgb = figure_light(i.model, i.normal, hidden, shadow_on.y);
    float4 current = diffuse;
    float4 t0 = tex0.Sample(smp0, i.uv);
    // Stage 0's patch: inside its rectangle the texels come from the patch texture (a unit's face).
    float2 w = i.uv - floor(i.uv);
    float4 tp = tex2.Sample(smp2, clamp(w * patch_map.xy + patch_map.zw, patch_inside.xy, patch_inside.zw));
    if (all(w >= patch_to.xy) && all(w < patch_to.zw)) t0 = tp;
    if (colour_ops.x > 1) {
        float3 c = combine(colour_ops.x, arg(colour_ops.y, current, diffuse, t0, i.spec),
                           arg(colour_ops.z, current, diffuse, t0, i.spec), current, diffuse, t0).rgb;
        float a = current.a;
        if (alpha_ops.x > 1)
            a = combine(alpha_ops.x, arg(alpha_ops.y, current, diffuse, t0, i.spec),
                        arg(alpha_ops.z, current, diffuse, t0, i.spec), current, diffuse, t0).a;
        current = saturate(float4(c, a));
        if (colour_ops.w > 1) {
            float4 t1 = tex1.Sample(smp1, i.uv);
            float3 c1 = combine(colour_ops.w, arg(colour_ops1.x, current, diffuse, t1, i.spec),
                                arg(colour_ops1.y, current, diffuse, t1, i.spec), current, diffuse, t1).rgb;
            float a1 = current.a;
            if (alpha_ops.w > 1)
                a1 = combine(alpha_ops.w, arg(alpha_ops1.x, current, diffuse, t1, i.spec),
                             arg(alpha_ops1.y, current, diffuse, t1, i.spec), current, diffuse, t1).a;
            current = saturate(float4(c1, a1));
        }
    }
    if (switches.z != 0 && !passes(switches.z, current.a, switches.w)) discard;
    float3 rgb = current.rgb;
    if (switches.x != 0) rgb = saturate(rgb + i.spec.rgb);
    if (shadow_on.x > 0.5) {
        if (shadow_on.z > 0.5) return float4((1.0 - hidden).xxx, current.a);   // the harness's shadowview
        if (!lit_here) rgb *= 1.0 - hidden * shadow_on.y;
    }
    if (switches.y != 0) rgb = lerp(fog_colour.rgb, rgb, i.spec.a);
    return float4(rgb, current.a);
}
