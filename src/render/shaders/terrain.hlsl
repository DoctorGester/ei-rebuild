// The landscape's pages (render_terrain.cpp), lit here (scene_lighting.hlsli): the vertices carry the normal, the
// material colour (colour word), on the ground the light lost under the water (`loss`, 0 or more; below 0 on the
// water layer, whose specular word carries the wave crests) and the ground's own occlusion of the ambient light
// (`extra` there, 1 none: render_terrain.cpp's height field), which the screen's (ambient_occlusion.hlsli) darkens
// more. On the water `depth` is its depth over the ground (negative where the ground is above it) and `extra` its
// kind (0 water, 1 a swamp, 2 lava: render_terrain.cpp's WaterKind), which the water's look (Water, below) starts
// from. On the ground `depth` is the depth of a swamp over it (-1 where there is none near), for the swamps' muddy rim.
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
#define ROOF_MAP_SLOT 8
#include "shadow_receiver.hlsli"
#include "point_shadow.hlsli"
#include "cloud_shadow.hlsli"
#include "scene_lighting.hlsli"
#include "ambient_occlusion.hlsli"
#include "roof.hlsli"
struct TerrainFsParams {
    float4 fog;            // rgb: the fog colour
    float4 flags;          // x: water (alpha from the vertex colour; 2: the level map, render::TerrainLevels), y: fog on
    float4 ripples;        // the two ripple layers' offsets at the client time (render_water.cpp): xy, zw
    float4 slowRipples;    // ... the swamps' slower ones
    float4 weather;        // the surfaces' weather (render_weather.cpp): x wet, y puddles, z snow, w the rain falling
    float4 rain;           // x: the rain's rings' clock (client ticks)
    float4 occlusion;      // ambient_occlusion's `on`
    float4 occlusionMap;   // ... and `map`
};
ConstantBuffer<TerrainFsParams> fs : FS_BLOCK(0);
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
Texture2D<float4> ripples : FS_TEXTURE(4);   // render_water.cpp: rg the slopes, b the foam's pattern
SamplerState rippleSmp : FS_SAMPLER(4);

// The weather on the ground and the water (render_weather.cpp; docs/notes/renderer.md section 13, item 11b): wet ground
// darker and glossy, puddles in low flat ground, snow on ground turned up, the rain's rings and splashes on the
// puddles and the water.
static const float kRingCell = 0.8;       // a ring's cell, landscape units
static const float kRingPeriodA = 16.0;   // client ticks a cell's ring lasts, layer A (render_weather.cpp kRingWrap)
static const float kRingPeriodB = 21.0;   // ... layer B
static const float kRingReach = 0.25;     // a ring's radius at its end, in cells (its centre 0.25 or more from the edge)
static const float kRingWidth = 0.07;     // in cells
static const float kSnowScale = 1.0 / 11.0;     // the snow's edge pattern's repeats per landscape unit
static const float kPuddleScale = 1.0 / 7.0;    // the puddles' pattern's
static const float kWetDark = 0.6;              // wet ground's albedo, of the dry
static const float kPuddleDark = 0.55;          // a puddle's ground under the water, of the wet
static const float3 kSnow = float3(0.74, 0.76, 0.81);

float3 hash3(float3 p) {
    p = frac(p * float3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return frac((p.xxy + p.yxx) * p.zyx);
}

// The rain's rings at `xy` (landscape units), `rain` the share of the cells a drop falls in: the slope they add (xy),
// the drops' splashes (z, 0 .. 1: a small soft spot where a drop has just fallen) and the rings' crests (w, 0 .. 1).
// Two layers of cells, each cell's drop at its own place and time on the client's clock. `footprint`: the landscape
// units a pixel spans; rings narrower than a pixel only shimmer, so they fade out. The crests are shaded apart from the
// slope (rain_dimples): seen from above, the water reflects little of the sky whatever the slope, so by the slope alone
// the rings do not show.
float4 rain_rings(float2 xy, float rain, float footprint) {
    float4 sum = 0.0;
    float seen = 1.0 - smoothstep(0.05, 0.15, footprint / kRingCell);
    if (rain <= 0.0 || seen <= 0.0) return sum;
    [unroll] for (int layer = 0; layer < 2; ++layer) {
        float2 p = xy / kRingCell + (layer == 0 ? 0.0 : float2(0.5, 0.37));
        float2 cell = floor(p);
        float3 h = hash3(float3(cell, layer * 17.0));
        float3 g = hash3(float3(cell + 31.7, layer * 17.0 + 5.0));
        if (g.x >= rain) continue;
        float period = layer == 0 ? kRingPeriodA : kRingPeriodB;
        float age = frac(fs.rain.x / period + h.z);
        float2 d = p - (cell + 0.25 + 0.5 * h.xy);
        float r = length(d);
        float x = (r - age * kRingReach) / kRingWidth;
        float fade = (1.0 - age) * (1.0 - age);
        sum.xy += d / max(r, 1e-4) * (-2.0 * x * exp(-x * x)) * fade * 0.35;
        sum.z = max(sum.z, (1.0 - smoothstep(0.0, 0.12, age)) * (1.0 - smoothstep(0.0, 0.07, r)));
        sum.w = max(sum.w, exp(-x * x) * fade * smoothstep(0.0, 0.1, age));
    }
    return sum * seen;
}

// Water of colour `c` reflecting `sky` with the rain's rings and splashes (rain_rings) on it: a ring's crest and a
// splash catch more of the sky, a little lighter than the water around and never brighter than the sky it reflects.
float3 rain_dimples(float3 c, float3 sky, float4 rings) {
    float3 lighter = max(c, lerp(c, sky, 0.6));
    return lerp(c, lighter, saturate(rings.w * 0.55 + rings.z * 0.5));
}

// How far `xy` lies from where the depth `d` (linear over a triangle) reaches 0, in landscape units: positive under
// the water, negative above it. From the depth's slope over the landscape, taken from the pixel's derivatives.
float shore_distance(float d, float2 xy) {
    float ddxd = ddx(d), ddyd = ddy(d);
    float2 dx = ddx(xy), dy = ddy(xy);
    float det = dx.x * dy.y - dx.y * dy.x;
    det = abs(det) > 1e-12 ? det : 1e-12;
    float2 grad = float2(dy.y * ddxd - dx.y * ddyd, dx.x * ddyd - dy.x * ddxd) / det;
    return d / max(length(grad), 0.02);
}

static const float kMudRim = 0.6;                      // the swamps' muddy rim on the ground, landscape units
static const float3 kMud = float3(0.95, 0.78, 0.52);   // its tint, of the ground's luminance

// The weather at a ground pixel: how wet (0 .. 1), how much of a puddle and how much snow; and how much of a swamp's
// muddy rim (wet whatever the weather). `open`: how much of it lies under the sky (roof_open), the rest stays dry.
struct GroundWeather {
    float wet, puddle, snow, mud;
};
GroundWeather ground_weather(VsOut i, float3 n, float open) {
    GroundWeather g = (GroundWeather)0;
    float dry = 1.0 - saturate(i.loss * 50.0);   // under the water: none
    if (fs.weather.z > 0.0) {
        // Snow settles on the flattest ground first, then on steeper; a little sooner higher up. A dusting: at most
        // 0.62 .. 0.87 of the ground (by the noise), so the texture shows through, with a soft edge.
        float noise = cloud_map.Sample(cloud_smp, i.world.xy * kSnowScale);
        float up = n.z + (noise - 0.5) * 0.3 + 0.1 * saturate(i.world.z / 30.0);
        float edge = lerp(1.15, 0.55, fs.weather.z);
        g.snow = smoothstep(edge - 0.15, edge + 0.15, up) * (0.62 + 0.25 * noise) * dry * open;
    }
    g.wet = fs.weather.x * dry * (1.0 - g.snow) * open;
    if (fs.weather.y > 0.0) {
        // Puddles: in flat ground, more where the ground lies low (its own occlusion), patchy.
        float low = saturate((1.0 - i.occlusion) * 3.0);
        float flat = smoothstep(0.86, 0.96, n.z);
        float noise = cloud_map.Sample(cloud_smp, i.world.xy * kPuddleScale);
        float edge = lerp(1.35, 0.75, fs.weather.y);
        g.puddle = smoothstep(edge, edge + 0.05, noise * 0.75 + low * 0.5) * flat * g.wet;
    }
    if (i.depth > -0.99) {
        g.mud = 1.0 - smoothstep(0.0, kMudRim, -shore_distance(i.depth, i.world.xy));
        g.wet = max(g.wet, g.mud * (1.0 - g.snow));
    }
    return g;
}

// The ground's colour `albedo` under the weather; the snow keeps some of the texture's light and dark.
float3 weathered(float3 albedo, GroundWeather g) {
    float shade = lerp(0.72, 1.08, saturate(dot(albedo, float3(0.3, 0.59, 0.11)) * 2.0));
    float3 mud = dot(albedo, float3(0.3, 0.59, 0.11)) * kMud * 0.8;
    albedo = lerp(albedo, mud, g.mud * 0.8);
    return lerp(albedo, kSnow * shade, g.snow) * lerp(1.0, kWetDark, g.wet) * lerp(1.0, kPuddleDark, g.puddle);
}

// The lit ground `c` with the wet ground's sheen of the sun, and in a puddle the sky and the sun reflected by its
// rippled water and the drops' rings and splashes (rain_dimples).
float3 wet_ground(float3 c, VsOut i, float3 n, GroundWeather g, float hidden, float4 rings) {
    if (g.wet <= 0.0) return c;
    float3 toEye = scene.camera.xyz - i.world;
    float3 v = toEye * rsqrt(max(dot(toEye, toEye), 1e-12));
    float3 toSun = -scene.sun.xyz;
    float3 h = normalize(toSun + v);
    float3 sun = scene.sunColour.rgb * saturate(toSun.z * 4.0) * (1.0 - hidden);
    c += sun * pow(saturate(dot(n, h)), 40.0) * 0.12 * g.wet;
    if (g.puddle <= 0.0) return saturate(c);
    float3 pn = normalize(float3(-rings.xy, 1.0));
    float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(pn, v)), 5.0);
    float3 sky = fs.fog.rgb * lerp(1.0, 0.75, saturate(reflect(-v, pn).z));
    c = lerp(c, sky, saturate(0.15 + fresnel) * g.puddle);
    c += sun * pow(saturate(dot(pn, h)), 300.0) * 0.6 * g.puddle;
    c = lerp(c, rain_dimples(c, sky, rings), g.puddle);
    return saturate(c);
}

// The water (docs/notes/renderer.md, "shaders/terrain.hlsl: the water"): the game's textured and lit layer `base`
// (alpha `opacity`, the material's) seen through small ripples, more opaque and darker with the depth, the sky and
// the sun reflected by the rippled normal, and foam where the depth runs out at the shore; it fades in from the shore
// line; in the rain (`rain`: the drops' share where it falls, none under a roof) the drops' rings and splashes and a
// rougher surface. A swamp is still standing water: a dark
// olive-brown body, opaque but at its very edge, faint slow ripples, a glossy sheen of the sky and the sun, sparse scum
// in place of the foam (and a muddy rim on the ground: ground_weather); lava keeps the game's look. `clouded`: the
// share of the sun the clouds hide, `footprint` the landscape units a pixel spans.
static const float kRippleScaleA = 1.0 / 9.0;    // the ripple map's repeats per landscape unit, layer A
static const float kRippleScaleB = 1.0 / 5.0;    // layer B, turned against A
static const float kRippleSlope = 0.15;          // the slope at the map's steepest texel
static const float kSwampSlope = 0.2;            // a swamp's ripples, of the water's
static const float kRippleShade = 2.0;           // how much the ripples' turn to the sun lightens or darkens
static const float kClearDepth = 0.5;            // the depth's look reached at depth d: 1 - e^(-0.5 d)
static const float kFoamDepth = 0.5;             // the foam's band, from the shore
static const float kShoreFade = 0.25;            // the depth over which the water fades in from the shore line
static const float kScumScale = 1.0 / 17.0;      // the swamps' scum patches' repeats per landscape unit
static const float3 kScum = float3(0.24, 0.27, 0.12);
static const float3 kSwampBody = float3(0.13, 0.115, 0.05);   // a swamp's body, of the light on it
static const float kSwampEdge = 0.25;            // a swamp fades in over this distance from its shore line
static const float kSwampClear = 1.2;            // ... and is opaque from this distance

// The two ripple layers at `xy` moved by `offsets`: the slopes (xy) and the foam's pattern (z).
float3 ripple_layers(float2 xy, float4 offsets) {
    float4 a = ripples.Sample(rippleSmp, xy * kRippleScaleA + offsets.xy);
    float4 b = ripples.Sample(rippleSmp, float2(0.8 * xy.x - 0.6 * xy.y, 0.6 * xy.x + 0.8 * xy.y) * kRippleScaleB +
                                             offsets.zw);
    return float3((a.xy * 2.0 - 1.0) + (b.xy * 2.0 - 1.0) * 0.45, a.z * 0.6 + b.z * 0.4);
}

float3 Water(VsOut i, float3 base, float opacity, float3 n, float clouded, float footprint, float rain, out float alpha) {
    const float3 game = base;
    float depth = max(i.depth, 0.0);
    float lava = saturate(i.kind - 1.0);
    float swamp = saturate(i.kind) - lava;
    float2 xy = i.world.xy;
    float3 layers = lerp(ripple_layers(xy, fs.ripples), ripple_layers(xy, fs.slowRipples), swamp);
    float2 slope = layers.xy * kRippleSlope * lerp(1.0, kSwampSlope, swamp);
    slope *= 0.4 + 0.6 * saturate(depth * 2.0);   // calmer at the shore
    float4 rings = rain_rings(xy, rain, footprint);
    slope = slope * (1.0 + 0.5 * rain) + rings.xy;
    float3 rippled = normalize(n + float3(-slope, 0.0));

    float3 toEye = scene.camera.xyz - i.world;
    float3 v = toEye * rsqrt(max(dot(toEye, toEye), 1e-12));
    float facing = saturate(dot(rippled, v));
    // Schlick's, water's 0.02 head-on; a swamp's still, dark skin keeps a sheen of the sky whatever the angle.
    float schlick = 0.02 + 0.98 * pow(1.0 - facing, 5.0);
    float fresnel = lerp(schlick, 0.045 + 0.75 * schlick, swamp);
    float3 r = reflect(-v, rippled);
    // The sky: the horizon in the fog's colour, darker overhead.
    float3 sky = fs.fog.rgb * lerp(1.0, 0.75, saturate(r.z)) * lerp(1.0, 0.85, swamp);

    float3 toSun = -scene.sun.xyz;
    float3 h = normalize(toSun + v);
    float nh = saturate(dot(rippled, h));
    // The sun's glints and the ripples' sun shading, gone as the sun sets or a cloud hides it; a swamp has a glossy
    // lobe in place of the sharp one.
    float sunUp = saturate(toSun.z * 4.0) * (1.0 - clouded);
    float3 glint = scene.sunColour.rgb * sunUp *
                   (pow(nh, 400.0) * 0.25 * (1.0 - swamp) + pow(nh, 60.0) * 0.03 + pow(nh, 150.0) * 0.12 * swamp);
    // The ripples turned to the sun or away from it, by the sun's share of the light.
    float sunShare = saturate(max(scene.sunColour.r, max(scene.sunColour.g, scene.sunColour.b))) * sunUp;
    base *= clamp(1.0 + kRippleShade * sunShare * (dot(rippled, toSun) - dot(n, toSun)), 0.75, 1.3);

    float3 light = saturate(scene.ambient.rgb + saturate(toSun.z) * (1.0 - clouded) * scene.sunColour.rgb);
    float deep = 1.0 - exp(-kClearDepth * depth);
    // A swamp: see-through at its very edge, opaque a little way in; the water by its depth.
    float edge = swamp > 0.0 ? shore_distance(i.depth, xy) : 0.0;
    float swampBody = lerp(0.35, 0.97, max(smoothstep(0.0, kSwampClear, edge), smoothstep(0.0, 0.5, depth)));
    float body = lerp(saturate(opacity * lerp(0.5, 1.15, deep)), swampBody, swamp);
    float3 transmitted = lerp(base * lerp(1.0, 0.85, deep), lerp(base * 0.5, light * kSwampBody, 0.6), swamp);

    // Over the ground: the transmitted layer under the reflection, then the foam (a swamp's scum) over both.
    float3 colour = transmitted * body * (1.0 - fresnel) + sky * fresnel + glint;
    alpha = 1.0 - (1.0 - body) * (1.0 - fresnel);
    float shore = 1.0 - smoothstep(0.0, kFoamDepth, depth);
    float foam = shore * smoothstep(0.4, 0.75, layers.z) * 0.35 * (1.0 - swamp);
    colour = lerp(colour, light * 0.9, foam);
    alpha = lerp(alpha, 1.0, foam);
    float patches = 0.5 * cloud_map.Sample(cloud_smp, xy * kScumScale) + 0.5 * layers.z;
    float scum = swamp * smoothstep(0.78, 0.86, patches + 0.1 * (1.0 - smoothstep(0.0, 1.5, depth))) * 0.45;
    colour = lerp(colour, light * kScum, scum);
    alpha = lerp(alpha, 1.0, scum);
    colour = saturate(colour / max(alpha, 1e-3));
    colour = rain_dimples(colour, sky, rings);
    alpha *= lerp(smoothstep(0.0, kShoreFade, i.depth), smoothstep(0.0, kSwampEdge, edge), swamp);
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
    float footprint = length(fwidth(i.world.xy));
    float open = fs.weather.x > 0.0 || fs.weather.z > 0.0 || fs.weather.w > 0.0 ? roof_open(i.shadow.xyz, n) : 1.0;
    float3 spec, c;
    // The layer is the batch's, not the interpolated `loss`'s sign: with multisampling a pixel on a triangle's edge is
    // shaded at its centre, past the triangle, and there the ground's loss (0 on the dry ground, more under the water)
    // fell below 0 at the shore, which drew the ground's edge pixels as water, nearly transparent: the sky showed
    // through in a dotted line along the triangles (scenario render_shore_seams).
    const bool water = fs.flags.x > 0.5;
    float a = water ? i.col.a : t.a;
    // The water: the clouds' shade only (the map's falls on the ground under it); the crests or the specular lights.
    if (water) {
        float3 lit = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, hidden, shadow.on.y, false, 0.0, 1.0, spec);
        float seen;
        c = Water(i, saturate(t.rgb * lit + max(spec, i.spec.rgb)), a, n, hidden, footprint, fs.weather.w * open, seen);
        a = seen;
    } else {
        i.loss = max(i.loss, 0.0);   // past the triangle's edge (above) it can fall below 0
        float occlusion = i.occlusion * ambient_occlusion(i.pos.xy, i.pos.z, fs.occlusion, fs.occlusionMap);
        if (fs.occlusion.w > 0.5) return float4(occlusion.xxx, 1.0);   // the harness's occlusion view
        float3 lit = ground_light(i.world, n, i.col.rgb, 0.0, i.loss, hidden, shadow.on.y, pointShadow.on.x > 0.5,
                                  pixel_turn(i.pos.xy), occlusion, spec);
        GroundWeather g = ground_weather(i, n, open);
        float4 rings = 0.0;
        if (g.puddle > 0.0) rings = rain_rings(i.world.xy, fs.weather.w, footprint);
        c = wet_ground(saturate(weathered(t.rgb, g) * lit + spec), i, n, g, hidden, rings);
    }
    if (fs.flags.y > 0.5) c = lerp(fs.fog.rgb, c, i.spec.a);
    return float4(c, a);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
