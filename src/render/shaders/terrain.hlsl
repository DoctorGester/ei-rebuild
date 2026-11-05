// The landscape's pages (render_terrain.cpp), lit here (scene_lighting.hlsli): the vertices carry the normal, the
// material colour (colour word), on the ground the light lost under the water (`loss`, 0 or more; below 0 on the
// water layer, whose specular word carries the wave crests) and the ground's own occlusion of the ambient light
// (`extra` there, 1 none: render_terrain.cpp's height field), which the screen's (ambient_occlusion.hlsli) darkens
// more. On the water `depth` is its depth over the ground (negative where the ground is above it) and `extra` its
// kind (0 water, 1 a swamp, 2 lava: render_terrain.cpp's WaterKind), which the water's look (Water, below) starts
// from, and `slope` the rise of the ground under it per landscape unit. On the ground `depth` is the depth of a swamp
// over it (-1 where there is none near), for the swamps' muddy rim, and `otherWater` 1 where it lies under water that is
// not a swamp.
#include "bindings.hlsli"
#include "depth_copy.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float3 normal : TEXCOORD4;
    [[vk::location(5)]] float loss : TEXCOORD5;
    [[vk::location(6)]] float2 depth : TEXCOORD6;   // the depth; otherWater on the ground, the slope on the water
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
    [[vk::location(10)]] float otherWater : TEXCOORD10;
    [[vk::location(11)]] float slope : TEXCOORD11;     // the ground's under the water (0 on the ground)
    float4 pos : SV_Position;
};

#ifdef EI_VERTEX
struct TerrainVsParams {
    row_major float4x4 matrix;
    row_major float4x4 light;   // to the shadow map
    float2 shift;               // the half-pixel shift (render_internal.h)
    float normalOffset;         // how far along its normal a receiver's point is looked up in the map
};
ConstantBuffer<TerrainVsParams> vs : VS_BLOCK(0);
VsOut vs_main(VsIn i) {
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.matrix[0], p), dot(vs.matrix[1], p), dot(vs.matrix[2], p), dot(vs.matrix[3], p));
    o.pos.xy += vs.shift * o.pos.w;   // Direct3D 7 pixel centres (render_internal.h)
    o.col = i.col.zyxw;                  // D3DCOLOR bytes are B, G, R, A
    o.spec = i.spec.zyxw;
    o.uv = i.uv;
    float4 q = float4(i.pos + i.normal * vs.normalOffset, 1.0);
    o.shadow = float4(dot(vs.light[0], q), dot(vs.light[1], q), dot(vs.light[2], q), 1.0);
    o.world = i.pos;
    o.normal = i.normal;
    o.loss = i.loss;
    o.depth = i.depth.x;
    const bool water = i.loss < 0.0;
    o.otherWater = water ? 0.0 : i.depth.y;
    o.slope = water ? i.depth.y : 0.0;
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
#define ROOF_MAP_SLOT 8   // 9 .. 11 the water's reflections (render_water.cpp)
#include "shadow_receiver.hlsli"
#include "point_shadow.hlsli"
#include "cloud_shadow.hlsli"
#include "scene_lighting.hlsli"
#include "ambient_occlusion.hlsli"
#include "roof.hlsli"
struct TerrainFsParams {
    float3 fog;            // the fog's colour
    int fogOn;
    int mode;              // the ground, kWater (alpha from the vertex colour) or kLevels (render::TerrainLevels)
    float4 ripples;        // the two ripple layers' offsets at the client time (render_water.cpp): xy, zw
    float4 slowRipples;    // ... the swamps' slower ones
    // The surfaces' weather (render_weather.cpp): the wetness, the puddles, the snow, the rain falling and the rain's
    // rings' clock (client ticks).
    float wet;
    float puddles;
    float snow;
    float rain;
    float rainClock;
    int occlusionOn;       // ambient_occlusion's arguments
    int occlusionView;     // the harness's occlusion view (SetOcclusion's View)
    float occlusionDepthOffset;
    float occlusionDepthScale;
    float2 occlusionScale;
    float2 occlusionTexel;
    // The water's reflections (render_water.cpp): the camera's clip matrix (the landscape's space to the frame's), the
    // last frame's (its copy's), the sky image's, the dome (its centre in the landscape's space, its radius across and
    // its height above the centre), the depth's map to clip w (w = sceneDepthScale / (depth - sceneDepthOffset)),
    // whether the rays are marched (0: the water reflects the sky alone) and the half-size images' size in pixels.
    row_major float4x4 view;
    row_major float4x4 sceneView;
    row_major float4x4 skyView;
    float3 dome;
    float domeRadius;
    float domeHeight;
    float sceneDepthOffset;
    float sceneDepthScale;
    int sceneOn;
    float2 sceneSize;
};
static const int kWater = 1;
static const int kLevels = 2;
ConstantBuffer<TerrainFsParams> fs : FS_BLOCK(0);
Texture2D<float4> tex : FS_TEXTURE(0);
SamplerState smp : FS_SAMPLER(0);
Texture2D<float4> ripples : FS_TEXTURE(4);   // render_water.cpp: rg the slopes, b the foam's pattern
SamplerState rippleSmp : FS_SAMPLER(4);
// The water's reflections (render_water.cpp): the sky dome drawn mirrored in the water's plane (bilinear, clamped),
// and the last frame's world view at half size (bilinear, clamped); the rays are marched through this frame's depth
// before the water, the ambient occlusion's (occlusion_depth, read with Load).
Texture2D<float4> sky_image : FS_TEXTURE(9);
SamplerState sky_smp : FS_SAMPLER(9);
Texture2D<float4> scene_colour : FS_TEXTURE(10);
SamplerState scene_smp : FS_SAMPLER(10);

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
        float age = frac(fs.rainClock / period + h.z);
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

// About how far a point at depth `d` lies from where the depth reaches 0, in landscape units: positive under the water,
// negative above it. `slope`: the ground's rise per landscape unit there, interpolated from the vertices' normals so
// the distance runs on smoothly across the triangles. Taken per triangle (from the depth's pixel derivatives) it
// jumped at every triangle's edge where the ground bends, and the swamps' edges and muddy rims stepped along the
// triangles in straight lines and notches (scenario render_swamp_shore).
float shore_distance(float d, float slope) {
    return d / max(slope, 0.02);
}

// The rise per landscape unit of ground with the normal `n` (unit length).
float ground_slope(float3 n) {
    return length(n.xy) / max(n.z, 0.05);
}

static const float kMudRim = 0.6;                      // the swamps' muddy rim on the ground, landscape units
static const float kMudUnder = 0.3;                    // ... and under the swamp, where it fades out
static const float kMudOther = 0.25;                   // the otherWater over which it fades out
static const float3 kMud = float3(0.85, 0.8, 0.5);     // its tint, of the ground's luminance
static const float kMudShare = 0.45;                   // how much of the ground's colour turns to mud
static const float kMudWet = 0.5;                      // how wet it is

// The weather at a ground pixel: how wet (0 .. 1), how much of a puddle and how much snow; and how much of a swamp's
// muddy rim (wet whatever the weather). `open`: how much of it lies under the sky (roof_open), the rest stays dry.
struct GroundWeather {
    float wet, puddle, snow, mud;
};
GroundWeather ground_weather(VsOut i, float3 n, float open) {
    GroundWeather g = (GroundWeather)0;
    float dry = 1.0 - saturate(i.loss * 50.0);   // under the water: none
    if (fs.snow > 0.0) {
        // Snow settles on the flattest ground first, then on steeper; a little sooner higher up. A dusting: at most
        // 0.62 .. 0.87 of the ground (by the noise), so the texture shows through, with a soft edge.
        float noise = cloud_map.Sample(cloud_smp, i.world.xy * kSnowScale);
        float up = n.z + (noise - 0.5) * 0.3 + 0.1 * saturate(i.world.z / 30.0);
        float edge = lerp(1.15, 0.55, fs.snow);
        g.snow = smoothstep(edge - 0.15, edge + 0.15, up) * (0.62 + 0.25 * noise) * dry * open;
    }
    g.wet = fs.wet * dry * (1.0 - g.snow) * open;
    if (fs.puddles > 0.0) {
        // Puddles: in flat ground, more where the ground lies low (its own occlusion), patchy.
        float low = saturate((1.0 - i.occlusion) * 3.0);
        float flat = smoothstep(0.86, 0.96, n.z);
        float noise = cloud_map.Sample(cloud_smp, i.world.xy * kPuddleScale);
        float edge = lerp(1.35, 0.75, fs.puddles);
        g.puddle = smoothstep(edge, edge + 0.05, noise * 0.75 + low * 0.5) * flat * g.wet;
    }
    if (i.depth > -0.99) {
        // Where a swamp meets other water both under it, the depth runs to the other's -1 and crosses 0 under the
        // water: no rim there, gone within a quarter of the way to ground under the other water.
        float shore = shore_distance(i.depth, ground_slope(n));
        g.mud = (1.0 - smoothstep(0.0, kMudRim, -shore)) * (1.0 - smoothstep(0.0, kMudUnder, shore)) *
                (1.0 - smoothstep(0.0, kMudOther, i.otherWater));
        g.wet = max(g.wet, g.mud * kMudWet * (1.0 - g.snow));
    }
    return g;
}

// The ground's colour `albedo` under the weather; the snow keeps some of the texture's light and dark.
float3 weathered(float3 albedo, GroundWeather g) {
    float shade = lerp(0.72, 1.08, saturate(dot(albedo, float3(0.3, 0.59, 0.11)) * 2.0));
    float3 mud = dot(albedo, float3(0.3, 0.59, 0.11)) * kMud;
    albedo = lerp(albedo, mud, g.mud * kMudShare);
    return lerp(albedo, kSnow * shade, g.snow) * lerp(1.0, kWetDark, g.wet) * lerp(1.0, kPuddleDark, g.puddle);
}

// `r` turned up to at least a little above the horizon (a ripple can tip it below).
float3 upwards(float3 r) { return normalize(float3(r.xy, max(r.z, 0.02))); }

float4 project(row_major float4x4 m, float3 p) {
    float4 q = float4(p, 1.0);
    return float4(dot(m[0], q), dot(m[1], q), dot(m[2], q), dot(m[3], q));
}

// The sky the water at `p` reflects along `r` (unit, upwards): where the ray meets the dome (taken as an ellipsoid of
// its radius across and its height), looked up in the dome drawn mirrored in the water's plane. Right at any water
// level: the mirrored image is read where it shows that point of the dome.
float3 reflected_sky(float3 p, float3 r) {
    float stretch = fs.domeRadius / max(fs.domeHeight, 1e-3);
    float3 o = p - fs.dome;
    o.z *= stretch;
    float3 d = float3(r.xy, r.z * stretch);
    float a = dot(d, d), b = dot(o, d), c = dot(o, o) - fs.domeRadius * fs.domeRadius;
    float t = (-b + sqrt(max(b * b - a * c, 0.0))) / max(a, 1e-6);   // the far meeting: the point lies inside
    float4 q = project(fs.skyView, p + r * max(t, 0.0));
    float2 uv = q.xy / max(q.w, 1e-4) * float2(0.5, -0.5) + 0.5;
    return sky_image.SampleLevel(sky_smp, uv, 0.0).rgb;
}

// The screen-space reflection: the ray from `p` along `r` marched through the frame's depth before the water (the
// ambient occlusion's: the ground and the models), kMarchSteps steps over kMarchLength landscape units from a start
// moved per pixel, then refined by halving; it hits where it passes behind that depth by less than kThickness (clip w,
// landscape units). Returns the colour there (rgb), the hit point looked up in the last frame's world view through
// that frame's camera, and how much of it to take (a): less towards either frame's edges and the ray's end, 0 for no
// hit.
static const int kMarchSteps = 10;
static const int kRefineSteps = 3;
static const float kMarchLength = 12.0;
static const float kThickness = 2.0;
float scene_w(float2 uv) {
    int2 at = clamp(int2(uv * fs.sceneSize), 0, int2(fs.sceneSize) - 1);
    return fs.sceneDepthScale / (occlusion_depth.Load(int3(at, 0)) - fs.sceneDepthOffset);
}
float2 clip_uv(float4 q) { return q.xy / q.w * float2(0.5, -0.5) + 0.5; }
float4 screen_reflection(float3 p, float3 r, float2 pixel) {
    if (fs.sceneOn == 0) return 0.0;
    float4 a = project(fs.view, p), b = project(fs.view, p + r * kMarchLength);
    // A ray turning towards the camera ends before it would pass behind it.
    float end = b.w < 0.25 * a.w ? (0.75 * a.w) / max(a.w - b.w, 1e-4) : 1.0;
    b = lerp(a, b, end);
    float jitter = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float before = 0.0, at = -1.0;
    [loop] for (int k = 0; k < kMarchSteps; ++k) {
        float t = (k + jitter) / kMarchSteps;
        float4 q = lerp(a, b, t);
        float2 uv = clip_uv(q);
        if (any(uv < 0.0) || any(uv > 1.0)) break;
        float behind = q.w - scene_w(uv);
        if (behind > 0.0 && behind < kThickness) {
            at = t;
            break;
        }
        before = t;
    }
    if (at < 0.0) return 0.0;
    [unroll] for (int k = 0; k < kRefineSteps; ++k) {
        float t = 0.5 * (before + at);
        float4 q = lerp(a, b, t);
        if (q.w - scene_w(clip_uv(q)) > 0.0) at = t;
        else before = t;
    }
    float2 uv = clip_uv(lerp(a, b, at));
    float4 seen = project(fs.sceneView, p + r * (kMarchLength * end * at));
    float2 last = seen.w > 1e-4 ? clip_uv(seen) : -1.0;
    float2 edge = min(min(uv, 1.0 - uv), min(last, 1.0 - last));
    float weight = saturate(min(edge.x, edge.y) * 12.0) * (1.0 - smoothstep(0.6, 1.0, at * end));
    return float4(scene_colour.SampleLevel(scene_smp, last, 0.0).rgb, weight);
}

// The lit ground `c` with the wet ground's sheen of the sun, and in a puddle the sky and the sun reflected by its
// rippled water and the drops' rings and splashes (rain_dimples).
float3 wet_ground(float3 c, VsOut i, float3 n, GroundWeather g, float hidden, float4 rings) {
    if (g.wet <= 0.0) return c;
    float3 toEye = scene.camera - i.world;
    float3 v = toEye * rsqrt(max(dot(toEye, toEye), 1e-12));
    float3 toSun = -scene.sun;
    float3 h = normalize(toSun + v);
    float3 sun = scene.sunColour * saturate(toSun.z * 4.0) * (1.0 - hidden);
    c += sun * pow(saturate(dot(n, h)), 40.0) * 0.12 * g.wet;
    if (g.puddle <= 0.0) return saturate(c);
    float3 pn = normalize(float3(-rings.xy, 1.0));
    float fresnel = 0.02 + 0.98 * pow(1.0 - saturate(dot(pn, v)), 5.0);
    float3 sky = reflected_sky(i.world, upwards(reflect(-v, pn)));
    c = lerp(c, sky, saturate(0.15 + fresnel) * g.puddle);
    c += sun * pow(saturate(dot(pn, h)), 300.0) * 0.6 * g.puddle;
    c = lerp(c, rain_dimples(c, sky, rings), g.puddle);
    return saturate(c);
}

// The water (docs/notes/renderer.md, "shaders/terrain.hlsl: the water"): the game's textured and lit layer `base`
// (alpha `opacity`, the material's) seen through small ripples, more opaque and darker with the depth, the sky and
// the sun reflected by the rippled normal, and foam where the depth runs out at the shore; it fades in from the shore
// line; in the rain (`rain`: the drops' share where it falls, none under a roof) the drops' rings and splashes and a
// rougher surface. A swamp is still standing water: a deep green body (the game's green, darker and murkier), opaque
// but at its very edge, faint slow ripples, a glossy sheen of the sky and the sun, sparse scum in place of the foam
// (and a muddy rim on the ground:
// ground_weather); lava keeps the game's look. `hidden`: the share of the sun the shadow map and the clouds hide,
// `footprint` the landscape units a pixel spans.
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
static const float3 kSwampBody = float3(0.07, 0.12, 0.03);    // a swamp's murk, of the light on it
static const float kSwampMurk = 0.35;            // ... its share of the body, the rest the game's lit layer darkened
static const float kSwampLayer = 0.8;            // ... by this
static const float kSwampEdge = 0.25;            // a swamp fades in over this distance from its shore line
static const float kSwampClear = 1.2;            // ... and is opaque from this distance
static const float kLeastReflection = 0.12;      // the least share of the water's colour its reflection is
static const float kShadedReflection = 0.3;      // the share of the reflection a full shadow takes away

// The two ripple layers at `xy` moved by `offsets`: the slopes (xy) and the foam's pattern (z).
float3 ripple_layers(float2 xy, float4 offsets) {
    float4 a = ripples.Sample(rippleSmp, xy * kRippleScaleA + offsets.xy);
    float4 b = ripples.Sample(rippleSmp, float2(0.8 * xy.x - 0.6 * xy.y, 0.6 * xy.x + 0.8 * xy.y) * kRippleScaleB +
                                             offsets.zw);
    return float3((a.xy * 2.0 - 1.0) + (b.xy * 2.0 - 1.0) * 0.45, a.z * 0.6 + b.z * 0.4);
}

float3 Water(VsOut i, float3 base, float opacity, float3 n, float hidden, float footprint, float rain, out float alpha) {
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

    float3 toEye = scene.camera - i.world;
    float3 v = toEye * rsqrt(max(dot(toEye, toEye), 1e-12));
    float facing = saturate(dot(rippled, v));
    // Schlick's, water's 0.02 head-on, but at least kLeastReflection (the game's lit layer is far brighter than deep
    // water, which hid a reflection of 2%); a swamp's still skin keeps a faint sheen of the sky whatever the angle.
    float schlick = 0.02 + 0.98 * pow(1.0 - facing, 5.0);
    float fresnel = lerp(max(schlick, kLeastReflection), 0.03 + 0.6 * schlick, swamp);
    float3 r = upwards(reflect(-v, rippled));
    // What it reflects: the sky dome, and over it what the frame shows where the ray meets it (the shore, objects).
    float3 sky = reflected_sky(i.world, r);
    float4 seen = screen_reflection(i.world, r, i.pos.xy);
    float3 reflected = lerp(sky, seen.rgb, seen.a) * lerp(1.0, 0.7, swamp) * (1.0 - kShadedReflection * hidden);

    float3 toSun = -scene.sun;
    float3 h = normalize(toSun + v);
    float nh = saturate(dot(rippled, h));
    // The sun's glints and the ripples' sun shading, gone as the sun sets or a cloud hides it; a swamp has a glossy
    // lobe in place of the sharp one.
    float sunUp = saturate(toSun.z * 4.0) * (1.0 - hidden);
    float3 glint = scene.sunColour * sunUp *
                   (pow(nh, 400.0) * 0.25 * (1.0 - swamp) + pow(nh, 60.0) * 0.03 * (1.0 - 0.7 * swamp) +
                    pow(nh, 300.0) * 0.15 * swamp);
    // The ripples turned to the sun or away from it, by the sun's share of the light.
    float sunShare = saturate(max(scene.sunColour.r, max(scene.sunColour.g, scene.sunColour.b))) * sunUp;
    base *= clamp(1.0 + kRippleShade * sunShare * (dot(rippled, toSun) - dot(n, toSun)), 0.75, 1.3);

    float3 light = saturate(scene.ambient + saturate(toSun.z) * (1.0 - hidden) * scene.sunColour);
    float deep = 1.0 - exp(-kClearDepth * depth);
    // A swamp: see-through at its very edge, opaque a little way in; the water by its depth.
    float edge = swamp > 0.0 ? shore_distance(i.depth, i.slope) : 0.0;
    float swampBody = lerp(0.35, 0.97, max(smoothstep(0.0, kSwampClear, edge), smoothstep(0.0, 0.5, depth)));
    float body = lerp(saturate(opacity * lerp(0.5, 1.15, deep)), swampBody, swamp);
    float3 transmitted = lerp(base * lerp(1.0, 0.85, deep), lerp(base * kSwampLayer, light * kSwampBody, kSwampMurk),
                              swamp);

    // Over the ground: the transmitted layer under the reflection, then the foam (a swamp's scum) over both.
    float3 colour = transmitted * body * (1.0 - fresnel) + reflected * fresnel + glint;
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
    if (fs.mode == kLevels) return float4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
    // How much of the sun the map and the clouds hide.
    float hidden = shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, 2.0);
    hidden = with_clouds(hidden, i.shadow.xy);
    if (shadow.shadowView != 0) return float4((1.0 - hidden).xxx, 1.0);   // the harness's shadowview
    float len = length(i.normal);
    float3 n = len > 1e-6 ? i.normal / len : float3(0.0, 0.0, 1.0);
    float footprint = length(fwidth(i.world.xy));
    float open = fs.wet > 0.0 || fs.snow > 0.0 || fs.rain > 0.0 ? roof_open(i.shadow.xyz, n) : 1.0;
    float3 spec, c;
    // The layer is the batch's, not the interpolated `loss`'s sign: with multisampling a pixel on a triangle's edge is
    // shaded at its centre, past the triangle, and there the ground's loss (0 on the dry ground, more under the water)
    // fell below 0 at the shore, which drew the ground's edge pixels as water, nearly transparent: the sky showed
    // through in a dotted line along the triangles (scenario render_shore_seams).
    const bool water = fs.mode == kWater;
    float a = water ? i.col.a : t.a;
    // The water: in the shade of the map and the clouds as the ground (Water takes its glints and part of its
    // reflection away there too); the crests or the specular lights.
    if (water) {
        float3 lit = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, hidden, shadow.strength, false, 0.0, 1.0, spec);
        float seen;
        c = Water(i, saturate(t.rgb * lit + max(spec, i.spec.rgb)), a, n, hidden, footprint, fs.rain * open, seen);
        a = seen;
    } else {
        i.loss = max(i.loss, 0.0);   // past the triangle's edge (above) it can fall below 0
        float occlusion = i.occlusion * ambient_occlusion(i.pos.xy, i.pos.z, fs.occlusionOn, fs.occlusionDepthOffset,
                                                                fs.occlusionDepthScale, fs.occlusionScale, fs.occlusionTexel);
        if (fs.occlusionView != 0) return float4(occlusion.xxx, 1.0);   // the harness's occlusion view
        float3 lit = ground_light(i.world, n, i.col.rgb, 0.0, i.loss, hidden, shadow.strength, pointShadow.count > 0,
                                  pixel_turn(i.pos.xy), occlusion, spec);
        GroundWeather g = ground_weather(i, n, open);
        float4 rings = 0.0;
        if (g.puddle > 0.0) rings = rain_rings(i.world.xy, fs.rain, footprint);
        c = wet_ground(saturate(weathered(t.rgb, g) * lit + spec), i, n, g, hidden, rings);
    }
    if (fs.fogOn != 0) c = lerp(fs.fog, c, i.spec.a);
    return float4(c, a);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
