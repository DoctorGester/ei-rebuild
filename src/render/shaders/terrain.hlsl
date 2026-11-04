// The landscape's pages (render_terrain.cpp), lit here (scene_lighting.hlsli): the vertices carry the normal, the
// material colour (colour word) and, on the ground, the light lost under the water (`loss`, 0 or more; below 0 on the
// water layer, whose specular word carries the wave crests). On the water `depth` is its depth over the ground
// (negative where the ground is above it), which the water's look (Water, below) starts from.
#include "bindings.hlsli"
#include "depth_copy.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float3 normal : TEXCOORD4;
    [[vk::location(5)]] float loss : TEXCOORD5;
    [[vk::location(6)]] float depth : TEXCOORD6;
};
struct VsOut {
    [[vk::location(0)]] float4 col : TEXCOORD0;
    [[vk::location(1)]] float4 spec : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 shadow : TEXCOORD3;
    [[vk::location(4)]] float3 world : TEXCOORD4;
    [[vk::location(5)]] float3 normal : TEXCOORD5;
    [[vk::location(6)]] float loss : TEXCOORD6;
    [[vk::location(7)]] float depth : TEXCOORD7;
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
    o.depth = i.depth;
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
#define CLOUD_MAP_SLOT 5   // 4 is the ripple map (render_water.cpp)
#include "shadow_receiver.hlsli"
#include "point_shadow.hlsli"
#include "cloud_shadow.hlsli"
#include "scene_lighting.hlsli"
struct TerrainFsParams {
    float4 fog;       // rgb: the fog colour
    float4 flags;     // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels), y: fog on
    float4 ripples;   // the two ripple layers' offsets at the client time (render_water.cpp): xy, zw
};
ConstantBuffer<TerrainFsParams> fs : FS_BLOCK(0);
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
Texture2D<float4> ripples : FS_TEXTURE(4);   // render_water.cpp: rg the slopes, b the foam's pattern
SamplerState rippleSmp : FS_SAMPLER(4);

// The water (docs/notes/renderer.md, "shaders/terrain.hlsl: the water"): the game's textured and lit layer `base`
// (alpha `opacity`, the material's) seen through small ripples, more opaque and darker with the depth, the sky and
// the sun reflected by the rippled normal, and foam where the depth runs out at the shore.
static const float kRippleScaleA = 1.0 / 9.0;    // the ripple map's repeats per landscape unit, layer A
static const float kRippleScaleB = 1.0 / 5.0;    // layer B, turned against A
static const float kRippleSlope = 0.22;          // the slope at the map's steepest texel
static const float kRippleShade = 3.0;           // how much the ripples' turn to the sun lightens or darkens
static const float kClearDepth = 0.5;            // the depth's look reached at depth d: 1 - e^(-0.5 d)
static const float kFoamDepth = 0.5;             // the foam's band, from the shore
float3 Water(VsOut i, float3 base, float opacity, float3 n, out float alpha) {
    float depth = max(i.depth, 0.0);
    float2 xy = i.world.xy;
    float4 a = ripples.Sample(rippleSmp, xy * kRippleScaleA + fs.ripples.xy);
    float4 b = ripples.Sample(rippleSmp, float2(0.8 * xy.x - 0.6 * xy.y, 0.6 * xy.x + 0.8 * xy.y) * kRippleScaleB +
                                             fs.ripples.zw);
    float2 slope = (a.xy * 2.0 - 1.0) + (b.xy * 2.0 - 1.0) * 0.6;
    slope *= kRippleSlope * (0.4 + 0.6 * saturate(depth * 2.0));   // calmer at the shore
    float3 rippled = normalize(n + float3(-slope, 0.0));

    float3 toEye = scene.camera.xyz - i.world;
    float3 v = toEye * rsqrt(max(dot(toEye, toEye), 1e-12));
    float facing = saturate(dot(rippled, v));
    float fresnel = 0.02 + 0.98 * pow(1.0 - facing, 5.0);   // Schlick's, water's 0.02 head-on
    float3 r = reflect(-v, rippled);
    float3 sky = fs.fog.rgb * lerp(1.0, 0.75, saturate(r.z));   // the horizon in the fog's colour, darker overhead

    float3 toSun = -scene.sun.xyz;
    float3 h = normalize(toSun + v);
    float nh = saturate(dot(rippled, h));
    float sunUp = saturate(toSun.z * 4.0);
    float3 glint = scene.sunColour.rgb * (pow(nh, 400.0) * 0.5 + pow(nh, 60.0) * 0.03) * sunUp;
    // The ripples turned to the sun or away from it, by the sun's share of the light.
    float sunShare = saturate(max(scene.sunColour.r, max(scene.sunColour.g, scene.sunColour.b))) * sunUp;
    base *= clamp(1.0 + kRippleShade * sunShare * (dot(rippled, toSun) - dot(n, toSun)), 0.75, 1.3);

    float deep = 1.0 - exp(-kClearDepth * depth);
    float body = saturate(opacity * lerp(0.5, 1.15, deep));
    float3 transmitted = base * lerp(1.0, 0.85, deep);

    // Over the ground: the transmitted layer under the reflection, then the foam over both.
    float3 colour = transmitted * body * (1.0 - fresnel) + sky * fresnel + glint;
    alpha = 1.0 - (1.0 - body) * (1.0 - fresnel);
    float pattern = a.z * 0.6 + b.z * 0.4;
    float shore = 1.0 - smoothstep(0.0, kFoamDepth, depth);
    float foam = shore * smoothstep(0.4, 0.75, pattern) * 0.4;
    float3 foamLight = saturate(scene.ambient.rgb + saturate(toSun.z) * scene.sunColour.rgb);
    colour = lerp(colour, foamLight * 0.9, foam);
    alpha = lerp(alpha, 1.0, foam);
    return saturate(colour / max(alpha, 1e-3));
}

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
    float a = fs.flags.x > 0.5 ? i.col.a : t.a;
    // The water: the clouds' shade only (the map's falls on the ground under it); the crests or the specular lights.
    if (i.loss < 0.0) {
        float3 lit = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, hidden, shadow.on.y, false, 0.0, spec);
        float seen;
        c = Water(i, saturate(t.rgb * lit + max(spec, i.spec.rgb)), a, n, seen);
        a = seen;
    } else {
        float3 lit = ground_light(i.world, n, i.col.rgb, 0.0, i.loss, hidden, shadow.on.y, pointShadow.on.x > 0.5,
                                  pixel_turn(i.pos.xy), spec);
        c = saturate(t.rgb * lit + spec);
    }
    if (fs.flags.y > 0.5) c = lerp(fs.fog.rgb, c, i.spec.a);
    return float4(c, a);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
