// The landscape's water (render_terrain.cpp: the zone's water mesh), lit here (scene_lighting.hlsli) and waved in the
// vertex stage: the vertices carry the normal, the material colour (colour word) and the vertex's wave phase offset
// (`rest.x`), and its specular word's colour bytes its material, column and row for the waves (`waved`, which puts the
// crests there). `depth` (`rest.y`) is its depth over the ground (negative where the ground is above it) and `extra`
// (`rest.w`) its kind (0 water, 1 a swamp, 2 lava: render_terrain.cpp's WaterKind), which the water's look (Water,
// below) starts from, and `slope` (`rest.z`) the rise of the ground under it per landscape unit. The ground is
// terrain_ground.hlsl.
#define SHADOW_BLOCK_SLOT 1
#define SHADOW_MAP_SLOT 1
#define SHADOW_RANGE_SLOT 2
#define SCENE_BLOCK_SLOT 2
#define POINT_BLOCK_SLOT 3
#define POINT_MAP_SLOT 3
#define CLOUD_MAP_SLOT 5   // 4 is the ripple map (render_water.cpp)
#define OCCLUSION_DEPTH_SLOT 6
#define ROOF_MAP_SLOT 7
#define SKY_IMAGE_SLOT 8
#define SCENE_COLOUR_SLOT 9   // the last frame's world view (render_water.cpp)
#include "terrain_common.hlsli"

struct VsOut {
    [[vk::location(0)]] float4 col : TEXCOORD0;
    [[vk::location(1)]] float3 crest : TEXCOORD1;      // the wave's crest colour (0 below its middle)
    [[vk::location(2)]] float fog : TEXCOORD2;         // the edge fade and the distance fog's factor
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float4 shadow : TEXCOORD4;
    [[vk::location(5)]] float3 world : TEXCOORD5;
    [[vk::location(6)]] float3 normal : TEXCOORD6;
    [[vk::location(7)]] float depth : TEXCOORD7;
    [[vk::location(8)]] float kind : TEXCOORD8;
    [[vk::location(9)]] float slope : TEXCOORD9;       // the ground's under the water
    [[vk::location(10)]] nointerpolation float layer : TEXCOORD10;   // the page's in the texture array
    float4 pos : SV_Position;
};

#ifdef EI_VERTEX
struct TerrainWaterVsParams {
    TERRAIN_VS_MEMBERS
    // The water's waves (render_terrain.cpp's WaterWaves; `waved`): on, the circling's time, the step along x and y
    // per vertex, per material its turn, lift and whether it keeps the ground's x, y, and its crest's colour.
    int waves;
    float waveTime;
    float2 waveDirection;
    float4 waveMaterials[32];   // Terrain::kWaveMaterials
    float4 waveCrests[32];
};
ConstantBuffer<TerrainWaterVsParams> vs : VS_BLOCK(0);
#endif
#ifdef EI_FRAGMENT
struct TerrainWaterFsParams {
    TERRAIN_FS_MEMBERS
    float4 ripples;        // the two ripple layers' offsets at the client time (render_water.cpp): xy, zw
    float4 slowRipples;    // ... the swamps' slower ones
    // The water's reflections (render_water.cpp): the camera's clip matrix (the landscape's space to the frame's), the
    // last frame's (its copy's), the depth's map to clip w (w = sceneDepthScale / (depth - sceneDepthOffset)), whether
    // the rays are marched (0: the water reflects the sky alone) and the half-size images' size in pixels.
    row_major float4x4 view;
    row_major float4x4 sceneView;
    float sceneDepthOffset;
    float sceneDepthScale;
    int sceneOn;
    float2 sceneSize;
};
ConstantBuffer<TerrainWaterFsParams> fs : FS_BLOCK(0);
#endif
#include "terrain_stages.hlsli"

#ifdef EI_VERTEX
// The waves (Terrain::WaterVertex, terrain.cpp, which answers the game's height queries the same way; its constants).
static const int kWaveMaterials = 32;
static const float kSineIndex = 256.0 / 3.14159265;   // the game's sine table: entries per radian
static const float kQuarterTurn = 0.785398126;        // between the circling's columns (and rows)
static const float kRiseScale = 0.25;
static const float kCircleScale = 3.0;
static const float kFullTurn = 6.28318548;
static const float kSectorCells = 32.0;
// The game's sine table's entry at `phase` radians: phase * 256 / pi rounded half away from zero, mod 512.
float wave_sine(float phase) {
    float k = phase * kSineIndex;
    int entry = int(sign(k) * floor(abs(k) + 0.5));
    return sin(float(entry & 511) * (3.14159265 / 256.0));
}
// A water vertex where the waves move it (its place at rest `i.pos`, its phase offset in `rest.x`, its material, column
// and row in the specular word's colour bytes), and its crest's colour (`crest`, 0 where the wave is below its middle):
// the wave is the table's sine at the phase along the wave direction from its sector's first vertex (mod 2 pi) plus its
// material's turn and its offset; it lifts the vertex by a quarter of the material's lift, and circles it by three
// times the lift with the time (the entries at its column and row a quarter turn apart); a material that keeps the
// ground's x, y only has the crest.
float3 waved(VsIn i, out float3 crest) {
    crest = 0.0;
    float3 p = i.pos;
    float3 bytes = round(i.spec.zyx * 255.0);
    int material = int(bytes.x);
    if (vs.waves == 0 || material >= kWaveMaterials) return p;
    float column = bytes.y, row = bytes.z;
    float4 m = vs.waveMaterials[material];
    float2 corner = round((p.xy - float2(column, row)) / kSectorCells) * kSectorCells;
    float start = fmod(dot(vs.waveDirection, corner) + kFullTurn, kFullTurn);
    float wave = wave_sine(start + m.x + row * vs.waveDirection.y + column * vs.waveDirection.x + i.rest.x);
    if (wave >= 0.0) crest = fmod(floor(wave * vs.waveCrests[material].xyz + 0.5), 256.0) / 255.0;
    if (m.z != 0.0) return p;
    p.z += wave * m.y * kRiseScale;
    p.x += wave_sine(column * kQuarterTurn + vs.waveTime) * m.y * kCircleScale;
    p.y += wave_sine(row * kQuarterTurn + vs.waveTime) * m.y * kCircleScale;
    return p;
}
VsOut vs_main(VsIn i) {
    VsOut o;
    const float3 pos = waved(i, o.crest);
    o.pos = clip_position(pos);
    o.col = i.col.zyxw;                  // D3DCOLOR bytes are B, G, R, A
    o.fog = fog_factor(i.spec.w, pos.xy, o.pos.w);
    o.uv = tile_uv(i);
    o.layer = i.uv.z;
    o.shadow = shadow_coord(pos, i.normal);
    o.world = pos;
    o.normal = i.normal;
    o.depth = i.rest.y;
    o.slope = i.rest.z;
    o.kind = i.rest.w;
    return o;
}
#endif

#ifdef EI_FRAGMENT
// The last frame's world view at half size (render_water.cpp; bilinear, clamped); the rays are marched through this
// frame's depth before the water, the ambient occlusion's (occlusion_depth, read with Load).
Texture2D<float4> scene_colour : FS_TEXTURE(SCENE_COLOUR_SLOT);
SamplerState scene_smp : FS_SAMPLER(SCENE_COLOUR_SLOT);
Texture2D<float> occlusion_depth : FS_TEXTURE(OCCLUSION_DEPTH_SLOT);
Texture2D<float4> ripples : FS_TEXTURE(4);   // render_water.cpp: rg the slopes, b the foam's pattern
SamplerState rippleSmp : FS_SAMPLER(4);

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

// The water (docs/notes/renderer.md, "shaders/terrain_water.hlsl: the water"): the game's textured and lit layer `base`
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
static const float2 kRippleTurnB = float2(0.7961, 0.6052);   // B's turn: 0.65 radians, so no lattice is shared
static const float kPatchScale = 1.0 / 47.0;     // the patches' noise (the cloud map) per landscape unit
static const float2 kPatchTurn = float2(0.9211, 0.3894);     // ... turned 0.4 radians against the landscape
static const float kPatchesA = 8.0;              // layer A's patches across the noise's range
static const float kPatchesB = 10.6;             // layer B's: their borders lie between A's
static const float2 kPatchShift = float2(0.7548777, 0.5698403);   // a patch's shift of the map per patch (R2)
static const float kCalmest = 0.75;              // the slopes' scale where the noise is lowest
static const float kRoughest = 1.2;              // ... and highest
static const float kRippleSlope = 0.165;         // the slope at the map's steepest texel
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

// The ripple map at `uv` (its gradients `dx`, `dy`) in patch `k`: each whole k shifts the map elsewhere, so the
// map's repeat never lines up with itself across the water; between two patches a narrow blend of both, its slopes
// scaled back to their contrast (two unrelated slopes blended half and half are 0.71 as steep). The slopes -1 .. 1,
// the foam's pattern 0 .. 1.
float3 ripple_patch(float2 uv, float2 dx, float2 dy, float k) {
    float i = floor(k);
    float w = smoothstep(0.35, 0.65, k - i);
    float4 a = ripples.SampleGrad(rippleSmp, uv + frac(i * kPatchShift), dx, dy);
    float4 b = ripples.SampleGrad(rippleSmp, uv + frac((i + 1.0) * kPatchShift), dx, dy);
    float4 m = lerp(a, b, w);
    return float3((m.xy * 2.0 - 1.0) * rsqrt(1.0 - 2.0 * w + 2.0 * w * w), m.z);
}

// The two ripple layers at `xy` (its gradients `dx`, `dy`) moved by `offsets`, in the patches of `noise` (0 .. 1):
// the slopes (xy) and the foam's pattern (z).
float3 ripple_layers(float2 xy, float2 dx, float2 dy, float4 offsets, float noise) {
    float3 a = ripple_patch(xy * kRippleScaleA + offsets.xy, dx * kRippleScaleA, dy * kRippleScaleA, noise * kPatchesA);
    float3 b = ripple_patch(turned(kRippleTurnB, xy) * kRippleScaleB + offsets.zw, turned(kRippleTurnB, dx) * kRippleScaleB,
                            turned(kRippleTurnB, dy) * kRippleScaleB, noise * kPatchesB + 0.5);
    return float3(a.xy + b.xy * 0.45, a.z * 0.6 + b.z * 0.4);
}

float3 Water(VsOut i, float3 base, float opacity, float3 n, float hidden, float footprint, float rain, out float alpha) {
    const float3 game = base;
    float depth = max(i.depth, 0.0);
    float lava = saturate(i.kind - 1.0);
    float swamp = saturate(i.kind) - lava;
    float2 xy = i.world.xy;
    // The noise that picks the ripples' patches (ripple_patch) and scales their slopes: rougher and calmer stretches
    // of water some tens of units across.
    float noise = cloud_map.SampleLevel(cloud_smp, turned(kPatchTurn, xy) * kPatchScale, 0.0);
    float2 dx = ddx(xy), dy = ddy(xy);
    float3 layers = 0.0;
    if (swamp < 1.0) layers = ripple_layers(xy, dx, dy, fs.ripples, noise) * (1.0 - swamp);
    if (swamp > 0.0) layers += ripple_layers(xy, dx, dy, fs.slowRipples, noise) * swamp;
    float2 slope = layers.xy * kRippleSlope * lerp(1.0, kSwampSlope, swamp) * lerp(kCalmest, kRoughest, noise);
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
    float scum = 0.0;
    if (swamp > 0.0) {
        float patches = 0.5 * cloud_map.SampleLevel(cloud_smp, xy * kScumScale, 0.0) + 0.5 * layers.z;
        scum = swamp * smoothstep(0.78, 0.86, patches + 0.1 * (1.0 - smoothstep(0.0, 1.5, depth))) * 0.45;
    }
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
    float4 t = tex.Sample(smp, float3(i.uv, i.layer));
    // How much of the sun the map and the clouds hide (Water takes its glints and part of its reflection away there
    // too).
    float hidden = shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, 2.0);
    hidden = with_clouds(hidden, i.shadow.xy);
    if (shadow.shadowView != 0) return float4((1.0 - hidden).xxx, 1.0);   // the harness's shadowview
    float3 n = surface_normal(i.normal);
    float footprint = length(fwidth(i.world.xy));
    float open = fs.rain > 0.0 ? roof_open(i.shadow.xyz, n) : 1.0;
    // Lit as the ground, the crests or the specular lights over it; the alpha is the vertex colour's.
    float3 spec;
    float3 lit = ground_light(i.world, n, i.col.rgb, i.col.rgb, 0.0, hidden, shadow.strength, false, 0.0, 1.0, spec);
    float alpha;
    float3 c = Water(i, saturate(t.rgb * lit + max(spec, i.crest)), i.col.a, n, hidden, footprint, fs.rain * open, alpha);
    if (fs.fogOn != 0) c = lerp(fs.fog, c, i.fog);
    return float4(c, alpha);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
