// The landscape's pages (render_terrain.cpp), lit here (scene_lighting.hlsli): the vertices carry the normal, the
// material colour (colour word), on the ground the light lost under the water (`loss`, 0 or more; below 0 on the
// water layer, whose specular word carries the wave crests) and the ground's own occlusion of the ambient light
// (`extra` there, 1 none: render_terrain.cpp's height field), which the screen's (ambient_occlusion.hlsli) darkens
// more. On the water `depth` is its depth over the ground (negative where the ground is above it) and `extra` its
// kind (0 water, 1 a swamp, 2 lava: render_terrain.cpp's WaterKind), which the water's look (Water, below) starts
// from.
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
    [[vk::location(7)]] float extra : TEXCOORD7;
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
    [[vk::location(8)]] float occlusion : TEXCOORD8;   // the ground's own (1 on the water)
    [[vk::location(9)]] float kind : TEXCOORD9;        // the water's kind (0 on the ground)
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
    const bool water = i.loss < 0.0;
    o.occlusion = water ? 1.0 : i.extra;
    o.kind = water ? i.extra : 0.0;
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
#define OCCLUSION_MAP_SLOT 6
#define OCCLUSION_DEPTH_SLOT 7
#include "shadow_receiver.hlsli"
#include "point_shadow.hlsli"
#include "cloud_shadow.hlsli"
#include "scene_lighting.hlsli"
#include "ambient_occlusion.hlsli"
struct TerrainFsParams {
    float4 fog;            // rgb: the fog colour
    float4 flags;          // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels), y: fog on
    float4 ripples;        // the two ripple layers' offsets at the client time (render_water.cpp): xy, zw
    float4 slowRipples;    // ... the swamps' slower ones
    float4 occlusion;      // ambient_occlusion's `on`
    float4 occlusionMap;   // ... and `map`
};
ConstantBuffer<TerrainFsParams> fs : FS_BLOCK(0);
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
Texture2D<float4> ripples : FS_TEXTURE(4);   // render_water.cpp: rg the slopes, b the foam's pattern
SamplerState rippleSmp : FS_SAMPLER(4);

// The water (docs/notes/renderer.md, "shaders/terrain.hlsl: the water"): the game's textured and lit layer `base`
// (alpha `opacity`, the material's) seen through small ripples, more opaque and darker with the depth, the sky and
// the sun reflected by the rippled normal, and foam where the depth runs out at the shore; it fades in from the shore
// line. A swamp is calmer, murkier and duller, with scum in place of the foam; lava keeps the game's look. `clouded`:
// the share of the sun the clouds hide.
static const float kRippleScaleA = 1.0 / 9.0;    // the ripple map's repeats per landscape unit, layer A
static const float kRippleScaleB = 1.0 / 5.0;    // layer B, turned against A
static const float kRippleSlope = 0.15;          // the slope at the map's steepest texel
static const float kSwampSlope = 0.3;            // a swamp's ripples, of the water's
static const float kRippleShade = 2.0;           // how much the ripples' turn to the sun lightens or darkens
static const float kClearDepth = 0.5;            // the depth's look reached at depth d: 1 - e^(-0.5 d)
static const float kFoamDepth = 0.5;             // the foam's band, from the shore
static const float kShoreFade = 0.25;            // the depth over which the water fades in from the shore line
static const float kScumScale = 1.0 / 17.0;      // the swamps' scum patches' repeats per landscape unit
static const float3 kScum = float3(0.24, 0.27, 0.12);

// The two ripple layers at `xy` moved by `offsets`: the slopes (xy) and the foam's pattern (z).
float3 ripple_layers(float2 xy, float4 offsets) {
    float4 a = ripples.Sample(rippleSmp, xy * kRippleScaleA + offsets.xy);
    float4 b = ripples.Sample(rippleSmp, float2(0.8 * xy.x - 0.6 * xy.y, 0.6 * xy.x + 0.8 * xy.y) * kRippleScaleB +
                                             offsets.zw);
    return float3((a.xy * 2.0 - 1.0) + (b.xy * 2.0 - 1.0) * 0.45, a.z * 0.6 + b.z * 0.4);
}

float3 Water(VsOut i, float3 base, float opacity, float3 n, float clouded, out float alpha) {
    const float3 game = base;
    float depth = max(i.depth, 0.0);
    float lava = saturate(i.kind - 1.0);
    float swamp = saturate(i.kind) - lava;
    float2 xy = i.world.xy;
    float3 layers = lerp(ripple_layers(xy, fs.ripples), ripple_layers(xy, fs.slowRipples), swamp);
    float2 slope = layers.xy * kRippleSlope * lerp(1.0, kSwampSlope, swamp);
    slope *= 0.4 + 0.6 * saturate(depth * 2.0);   // calmer at the shore
    float3 rippled = normalize(n + float3(-slope, 0.0));

    float3 toEye = scene.camera.xyz - i.world;
    float3 v = toEye * rsqrt(max(dot(toEye, toEye), 1e-12));
    float facing = saturate(dot(rippled, v));
    // Schlick's, water's 0.02 head-on; a swamp's murky skin reflects less, and a darker sky.
    float fresnel = (0.02 + 0.98 * pow(1.0 - facing, 5.0)) * lerp(1.0, 0.45, swamp);
    float3 r = reflect(-v, rippled);
    // The sky: the horizon in the fog's colour, darker overhead.
    float3 sky = fs.fog.rgb * lerp(1.0, 0.75, saturate(r.z)) * lerp(1.0, 0.7, swamp);

    float3 toSun = -scene.sun.xyz;
    float3 h = normalize(toSun + v);
    float nh = saturate(dot(rippled, h));
    // The sun's glints and the ripples' sun shading, gone as the sun sets or a cloud hides it; a swamp has the broad
    // lobe only.
    float sunUp = saturate(toSun.z * 4.0) * (1.0 - clouded);
    float3 glint = scene.sunColour.rgb * (pow(nh, 400.0) * 0.25 * (1.0 - swamp) + pow(nh, 60.0) * 0.03) * sunUp;
    // The ripples turned to the sun or away from it, by the sun's share of the light.
    float sunShare = saturate(max(scene.sunColour.r, max(scene.sunColour.g, scene.sunColour.b))) * sunUp;
    base *= clamp(1.0 + kRippleShade * sunShare * (dot(rippled, toSun) - dot(n, toSun)), 0.75, 1.3);

    float deep = 1.0 - exp(-kClearDepth * depth);
    float body = saturate(opacity * lerp(lerp(0.5, 0.95, swamp), lerp(1.15, 1.35, swamp), deep));
    float3 transmitted = base * lerp(1.0, lerp(0.85, 0.6, swamp), deep);

    // Over the ground: the transmitted layer under the reflection, then the foam (a swamp's scum) over both.
    float3 colour = transmitted * body * (1.0 - fresnel) + sky * fresnel + glint;
    alpha = 1.0 - (1.0 - body) * (1.0 - fresnel);
    float shore = 1.0 - smoothstep(0.0, kFoamDepth, depth);
    float foam = shore * smoothstep(0.4, 0.75, layers.z) * 0.35 * (1.0 - swamp);
    float3 light = saturate(scene.ambient.rgb + saturate(toSun.z) * (1.0 - clouded) * scene.sunColour.rgb);
    colour = lerp(colour, light * 0.9, foam);
    alpha = lerp(alpha, 1.0, foam);
    float patches = 0.5 * cloud_map.Sample(cloud_smp, xy * kScumScale) + 0.5 * layers.z;
    float scum = swamp * smoothstep(0.7, 0.8, patches + 0.1 * (1.0 - smoothstep(0.0, 1.5, depth))) * 0.6;
    colour = lerp(colour, light * kScum, scum);
    alpha = lerp(alpha, 1.0, scum);
    colour = saturate(colour / max(alpha, 1e-3));
    alpha *= smoothstep(0.0, kShoreFade, i.depth);
    alpha = lerp(alpha, opacity, lava);
    return lerp(colour, game, lava);
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
        float3 lit = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, hidden, shadow.on.y, false, 0.0, 1.0, spec);
        float seen;
        c = Water(i, saturate(t.rgb * lit + max(spec, i.spec.rgb)), a, n, hidden, seen);
        a = seen;
    } else {
        float occlusion = i.occlusion * ambient_occlusion(i.pos.xy, i.pos.z, fs.occlusion, fs.occlusionMap);
        if (fs.occlusion.w > 0.5) return float4(occlusion.xxx, 1.0);   // the harness's occlusion view
        float3 lit = ground_light(i.world, n, i.col.rgb, 0.0, i.loss, hidden, shadow.on.y, pointShadow.on.x > 0.5,
                                  pixel_turn(i.pos.xy), occlusion, spec);
        c = saturate(t.rgb * lit + spec);
    }
    if (fs.flags.y > 0.5) c = lerp(fs.fog.rgb, c, i.spec.a);
    return float4(c, a);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
