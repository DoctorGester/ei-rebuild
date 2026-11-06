// The landscape's ground (render_terrain.cpp: the zone's ground mesh), lit here (scene_lighting.hlsli): the vertices
// carry the normal, the material colour (colour word), the light lost under the water (`loss`, 0 or more; `rest.x`) and
// the ground's own occlusion of the ambient light (`extra`, 1 none: render_terrain.cpp's height field; `rest.w`), which
// the screen's (ambient_occlusion.hlsli) darkens more. `depth` (`rest.y`) is the depth of a swamp over the ground (-1
// where there is none near), for the swamps' muddy rim, and `otherWater` (`rest.z`) 1 where it lies under water that is
// not a swamp. The weather (wet ground, puddles, snow, the rain's rings) and the ground's tiles' animation (terrain_common.hlsli)
// are here; the water is terrain_water.hlsl.
#define SHADOW_BLOCK_SLOT 1
#define SHADOW_MAP_SLOT 1
#define SHADOW_RANGE_SLOT 2
#define SCENE_BLOCK_SLOT 2
#define POINT_BLOCK_SLOT 3
#define POINT_MAP_SLOT 3
#define CLOUD_MAP_SLOT 4
#define OCCLUSION_MAP_SLOT 5
#define OCCLUSION_DEPTH_SLOT 6
#define ROOF_MAP_SLOT 7
#define SKY_IMAGE_SLOT 8   // the puddles reflect the sky
#include "terrain_common.hlsli"

struct VsOut {
    [[vk::location(0)]] float3 col : TEXCOORD0;
    [[vk::location(1)]] float fog : TEXCOORD1;         // the edge fade and the distance fog's factor
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 shadow : TEXCOORD3;
    [[vk::location(4)]] float3 world : TEXCOORD4;
    [[vk::location(5)]] float3 normal : TEXCOORD5;
    [[vk::location(6)]] float loss : TEXCOORD6;
    [[vk::location(7)]] float depth : TEXCOORD7;
    [[vk::location(8)]] float occlusion : TEXCOORD8;
    [[vk::location(9)]] float otherWater : TEXCOORD9;
    [[vk::location(10)]] nointerpolation float layer : TEXCOORD10;   // the page's in the texture array
    float4 pos : SV_Position;
};

#ifdef EI_VERTEX
struct TerrainGroundVsParams {
    TERRAIN_VS_MEMBERS
};
ConstantBuffer<TerrainGroundVsParams> vs : VS_BLOCK(0);
#endif
#ifdef EI_FRAGMENT
struct TerrainGroundFsParams {
    TERRAIN_FS_MEMBERS
    int levels;            // the harness's level map (render::TerrainLevels): the page textures' grey per level
    // The surfaces' weather (render_weather.cpp): the wetness, the puddles and the snow.
    float wet;
    float puddles;
    float snow;
    int occlusionOn;       // ambient_occlusion's arguments
    int occlusionView;     // the harness's occlusion view (SetOcclusion's View)
    float occlusionDepthOffset;
    float occlusionDepthScale;
    float2 occlusionScale;
    float2 occlusionTexel;
};
ConstantBuffer<TerrainGroundFsParams> fs : FS_BLOCK(0);
#endif
#include "terrain_stages.hlsli"

#ifdef EI_VERTEX
VsOut vs_main(VsIn i) {
    VsOut o;
    o.pos = clip_position(i.pos);
    o.col = i.col.zyx;                  // D3DCOLOR bytes are B, G, R, A
    o.fog = fog_factor(i.spec.w, i.pos.xy, o.pos.w);
    o.uv = tile_uv(i);
    o.layer = i.uv.z;
    o.shadow = shadow_coord(i.pos, i.normal);
    o.world = i.pos;
    o.normal = i.normal;
    o.loss = i.rest.x;
    o.depth = i.rest.y;
    o.otherWater = i.rest.z;
    o.occlusion = i.rest.w;
    return o;
}
#endif

#ifdef EI_FRAGMENT
#include "ambient_occlusion.hlsli"
static const float kSnowScale = 1.0 / 11.0;     // the snow's edge pattern's repeats per landscape unit
static const float kPuddleScale = 1.0 / 7.0;    // the puddles' pattern's
static const float kWetDark = 0.6;              // wet ground's albedo, of the dry
static const float kPuddleDark = 0.55;          // a puddle's ground under the water, of the wet
static const float3 kSnow = float3(0.74, 0.76, 0.81);

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

float4 Shade(VsOut i) {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    float4 t = tex.Sample(smp, float3(i.uv, i.layer));
    if (fs.levels != 0) return float4(t.r, 90.0 / 255.0, 165.0 / 255.0, 1.0);
    // How much of the sun the map and the clouds hide.
    float hidden = shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, 2.0);
    hidden = with_clouds(hidden, i.shadow.xy);
    if (shadow.shadowView != 0) return float4((1.0 - hidden).xxx, 1.0);   // the harness's shadowview
    float3 n = surface_normal(i.normal);
    float footprint = length(fwidth(i.world.xy));
    float open = fs.wet > 0.0 || fs.snow > 0.0 || fs.rain > 0.0 ? roof_open(i.shadow.xyz, n) : 1.0;
    i.loss = max(i.loss, 0.0);   // past the triangle's edge (above) it can fall below 0
    float occlusion = i.occlusion * ambient_occlusion(i.pos.xy, i.pos.z, fs.occlusionOn, fs.occlusionDepthOffset,
                                                            fs.occlusionDepthScale, fs.occlusionScale, fs.occlusionTexel);
    if (fs.occlusionView != 0) return float4(occlusion.xxx, 1.0);   // the harness's occlusion view
    float3 spec;
    float3 lit = ground_light(i.world, n, i.col, 0.0, i.loss, hidden, shadow.strength, pointShadow.count > 0,
                              pixel_turn(i.pos.xy), occlusion, spec);
    GroundWeather g = ground_weather(i, n, open);
    float4 rings = 0.0;
    if (g.puddle > 0.0) rings = rain_rings(i.world.xy, fs.rain, footprint);
    float3 c = wet_ground(saturate(weathered(t.rgb, g) * lit + spec), i, n, g, hidden, rings);
    if (fs.fogOn != 0) c = lerp(fs.fog, c, i.fog);
    return float4(c, t.a);
}

DepthCopyOut ps_main(VsOut i) { return WithDepthCopy(Shade(i), i.pos.z); }
#endif
