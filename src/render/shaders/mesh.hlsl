// The meshes (render_mesh.cpp): the fixed-function texture stages of the old device as the game sets them
// (D3DTEXTUREOP and D3DTA values), evaluated per pixel; then specular, fog and the alpha test. A figure part lit on
// the GPU (render::MeshLighting) takes its diffuse colour from `figure_light` instead of the vertex colour (whose
// alpha it keeps), its ambient light darkened by the screen's ambient occlusion in the world (ambient_occlusion.hlsli).
// The world's meshes take the weather on their faces (weathered_mesh), a unit's figure on its smoothed normals
// (weathered_unit), an interface figure of a unit on its model-space normals (weathered_figure). A ground print is lit
// as the ground under it: by the time of day, the lights and the shadows (ground_shade).
// Each draw's own values are a MeshRecord in the frame's storage buffer, found by the draw's instance (VsIn::record:
// render_mesh.cpp's MeshDraws draws a run of meshes as one call), its lights a PartLight in a second one (draws lit
// alike share one); what the run shares is the fragment stage's blocks.
#include "bindings.hlsli"
#include "depth_copy.hlsli"
#include "distance_fog.hlsli"
struct VsIn {
    [[vk::location(0)]] float3 pos : TEXCOORD0;
    [[vk::location(1)]] float4 col : TEXCOORD1;
    [[vk::location(2)]] float4 spec : TEXCOORD2;
    [[vk::location(3)]] float2 uv : TEXCOORD3;
    [[vk::location(4)]] float3 normal : TEXCOORD4;
    [[vk::location(5)]] uint record : TEXCOORD5;   // per instance: its MeshRecord
};
struct VsOut {
    [[vk::location(0)]] float4 col : TEXCOORD0;
    [[vk::location(1)]] float4 spec : TEXCOORD1;
    [[vk::location(2)]] float2 uv : TEXCOORD2;
    [[vk::location(3)]] float4 shadow : TEXCOORD3;
    [[vk::location(4)]] float3 model : TEXCOORD4;
    [[vk::location(5)]] float3 normal : TEXCOORD5;
    [[vk::location(6)]] float3 land : TEXCOORD6;
    [[vk::location(7)]] float3 mapNormal : TEXCOORD7;   // the vertex normal in the shadow map's clip space
    [[vk::location(8)]] nointerpolation uint record : TEXCOORD8;
    float4 pos : SV_Position;
};
// One mesh draw's own values (render_mesh.cpp, MeshDraws), each member 16 bytes (gen_gpu_shaders.py's storage rule).
struct MeshRecord {
    row_major float4x4 matrix;
    row_major float4x4 light;   // model space to the shadow map
    float4 landscape[3];        // model space to the landscape's: rows (point lights)
    float4 place;               // the half-pixel shift (render_internal.h; xy), the depth's scale (the flat geometry's:
                                // shadows drawn just in front; z), how far along its normal a receiver's point is
                                // looked up in the map (w)
    float4 fog;                 // the distance fog (distance_fog.hlsli: start, density 0 none, depth per w), the eye
                                // above the mesh's origin (the sky dome's haze towards the horizon)
    float4 uvOffset;            // added to the texture coordinates (a mesh kept on the GPU; xy); a unit's front (z)
    float4 weather;             // a world mesh's wetness and snow cover (render_weather.cpp; 0 elsewhere)
    int4 flags;                 // z kept inside the depth range, the horizon's haze (render::MeshFog::Horizon), a ground
                                // print's normal (none: the landscape's up), a unit's shadows (MeshBatch::front)
    int4 kinds;                 // in the world: the point lights and their shadows taken (the landscape rows set), the
                                // shadows by a unit's mesh from pointFront, the weather's kind, the ambient occlusion taken
    int4 lit;                   // lit here, a ground print (ground_shade), -, the harness's occlusion view
    float4 pointFront;          // per map: a unit's nearest distance to the light (MeshBatch::pointFront)
    int4 lights;                // x: its PartLight (0: none, all zero)
};

// A figure part lit on the GPU (render_lighting.cpp, MeshLightRecord): the sun in the part's model space. A ground
// print's (GroundDecalLight): the scene's in the landscape's space, which its positions are in. The point lights are the
// frame's (point_lights.hlsli).
struct PartLight {
    float4 sun;                 // towards the sun in model space
    float4 sunColour;           // the sun's lit colour
    float4 ambient;
    float4 emissive;
    float4 mapSun;              // a ground print's: the direction the shadow map's light travels (unit)
    float4 daySunColour;        // a ground print's: the daylight sun (render::SceneLights::daySunColour)
    float4 dayAmbient;          // ... and ambient colour
    float4 pointTint;           // a figure part's: what its point lights' colours are multiplied by (rgb)
};

#ifdef EI_VERTEX
StructuredBuffer<MeshRecord> records : VS_STORAGE(0);
static const float kHazeTop = 0.2;   // the sine of the rise over the eye's level where the sky's haze ends (11.5 degrees)
VsOut vs_main(VsIn i) {
    const MeshRecord vs = records[i.record];
    float4 p = float4(i.pos, 1.0);
    VsOut o;
    o.pos = float4(dot(vs.matrix[0], p), dot(vs.matrix[1], p), dot(vs.matrix[2], p), dot(vs.matrix[3], p));
    // Meshes the old path projects to the screen without clipping (the interface's) are not cut at the near and far
    // planes there either.
    if (vs.flags.x != 0) o.pos.z = clamp(o.pos.z, 0.0, o.pos.w);
    o.pos.z *= vs.place.z;
    o.pos.xy += vs.place.xy * o.pos.w;
    o.col = i.col.zyxw;
    o.spec = i.spec.zyxw;
    // The distance fog lowers the fog factor in the specular alpha: the smaller of it and the vertex's own is kept.
    o.spec.a = min(o.spec.a, fog_kept(o.pos.w, vs.fog.x, vs.fog.y, vs.fog.z));
    if (vs.flags.y != 0) {
        float3 seen = i.pos - float3(0.0, 0.0, vs.fog.w);   // from the eye
        o.spec.a = min(o.spec.a, smoothstep(0.0, kHazeTop, seen.z / max(length(seen), 1e-6)));
    }
    o.uv = i.uv + vs.uvOffset.xy;
    // The normal offset: the point looked up in the map moved along its normal (a figure part's).
    float3 normal = vs.flags.z != 0 ? float3(0.0, 0.0, 1.0) : i.normal;
    float len = length(normal);
    float4 q = len > 1e-6 ? float4(i.pos + normal * (vs.place.w / len), 1.0) : p;
    o.shadow = float4(dot(vs.light[0], q), dot(vs.light[1], q), dot(vs.light[2], q), 1.0);
    o.model = i.pos;
    o.normal = i.normal;
    o.mapNormal = float3(dot(vs.light[0].xyz, i.normal), dot(vs.light[1].xyz, i.normal), dot(vs.light[2].xyz, i.normal));
    o.land = float3(dot(vs.landscape[0], p), dot(vs.landscape[1], p), dot(vs.landscape[2], p));
    o.record = i.record;
    return o;
}
#endif

#ifdef EI_FRAGMENT
#define SHADOW_BLOCK_SLOT 1
#define SHADOW_MAP_SLOT 3
#define SHADOW_RANGE_SLOT 4
#define POINT_BLOCK_SLOT 2
#define POINT_MAP_SLOT 5
#define CLOUD_MAP_SLOT 6
#define OCCLUSION_MAP_SLOT 7
#define OCCLUSION_DEPTH_SLOT 8
#define ROOF_MAP_SLOT 9
#define POINT_LIGHTS_SLOT 12   // after the records and their lights
#define POINT_GRID_SLOT 13
#include "shadow_receiver.hlsli"
#include "point_lights.hlsli"
#include "cloud_shadow.hlsli"
#include "ambient_occlusion.hlsli"
#include "roof.hlsli"
// What a run of draws shares (render_mesh.cpp, MeshDraws).
struct MeshFsParams {
    float3 fogColour;
    int fog;
    float4 factor;         // TEXTUREFACTOR
    int3 colourStage0;     // the texture stages' colour and alpha operations: op, arg1, arg2
    int3 colourStage1;
    int3 alphaStage0;
    int3 alphaStage1;
    int specular;
    int alphaFunc;         // the alpha test's (0 off)
    int alphaReference;
    float4 patchTo;        // stage 0's patch (render::TexturePatch): its rectangle (u0, v0, u1, v1), zero: none
    float2 patchScale;     // the patch texture's coordinates: uv * patchScale + patchOffset
    float2 patchOffset;
    float4 patchInside;    // ... kept inside its rectangle (u0, v0, u1, v1)
    float occlusionDepthOffset;   // ambient_occlusion's arguments (a world mesh lit here; MeshRecord::kinds.w on)
    float occlusionDepthScale;
    float2 occlusionScale;
    float2 occlusionTexel;
    // A world mesh's weather (render_weather.cpp; MeshRecord::weather the wetness and the snow cover, kinds.z its
    // kind: a mesh, a unit's or an interface figure's): towards the sun (the landscape's space) and its colour.
    float3 weatherSun;
    float3 weatherSunColour;
};
static const int kUnitWeather = 1;
static const int kFigureWeather = 2;
ConstantBuffer<MeshFsParams> fs : FS_BLOCK(0);
StructuredBuffer<MeshRecord> records : FS_STORAGE(10);
StructuredBuffer<PartLight> partLights : FS_STORAGE(11);
// The draw's own values and its lights (ps_main), read where used: a copy of the whole record would put its arrays in
// indexable scratch memory.
static uint drawRecord, drawLight;
#define draw records[drawRecord]
#define part partLights[drawLight]
Texture2D<float4> tex0 : FS_TEXTURE(0);
SamplerState smp0 : FS_SAMPLER(0);
Texture2D<float4> tex1 : FS_TEXTURE(1);
SamplerState smp1 : FS_SAMPLER(1);
Texture2D<float4> tex2 : FS_TEXTURE(2);   // stage 0's patch
SamplerState smp2 : FS_SAMPLER(2);

// A figure part's colour as 3DfpFPU's LightMesh lit it (render.h, MeshLighting). See docs/notes/renderer.md, "The
// renderer's source files". `n`: the model-space normal; `land`: the point in the landscape's space, where the point
// lights are (in the world only: MeshRecord::kinds.x). `occlusion`: how much of the ambient light reaches the pixel (1
// all).
float3 figure_light(float3 n, float3 land, float3 face, float2 turn, float occlusion) {
    float3 ambient = part.ambient.rgb * occlusion;
    float facing = dot(part.sun.xyz, n);
    float3 lit = max(ambient, part.sunColour.rgb * facing);
    // The normal taken to the landscape's space (the part's matrix there turns and moves only).
    float3 ln = float3(dot(draw.landscape[0].xyz, n), dot(draw.landscape[1].xyz, n), dot(draw.landscape[2].xyz, n));
    uint2 span = draw.kinds.x != 0 ? cell_lights(land) : uint2(0, 0);
    for (uint k = 0; k < span.y; ++k) {
        PointLight l = pointLights[grid_word(span.x + k)];
        float3 d = l.at.xyz - land;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * l.at.w;
        if (falloff < 0.0) continue;
        float c = dot(d * rsqrt(max(distance2, 1e-12)), ln);
        float seen = 1.0;
        int slot = l.shadow.x;
        if (slot >= 0 && c > 0.0) {
            if (draw.kinds.y != 0) {
                float4 fronts = draw.pointFront;
                seen = point_seen(slot, land, 0.0, fronts[point_index(slot)], turn);
            } else {
                float lightFacing = saturate(dot(face, normalize(points.shadows[point_index(slot)].xyz - land)) * 6.0);
                if (lightFacing > 0.0) seen = lerp(1.0, point_seen(slot, land, face, kNoPointFront, turn), lightFacing);
            }
        }
        lit = max(lit, part.pointTint.rgb * l.colour.rgb * c * falloff * seen);
    }
    float3 c = min(1.0, lit + part.emissive.rgb);
    return c;
}
// A ground print's light, per channel: the light of the ground under it (scene_lighting.hlsli's ground_light on dry
// ground: no material colour, all of the ambient light) with the sun map, the clouds and the point shadows, over the
// same ground's light in the daylight colours or without the shadows, whichever is more. The print is blended over
// that ground; its own colour is its colour in daylight (by day the game's), so it darkens with the ground at night
// and in the shadows and takes the dawn's tint. `p`: the point (the landscape's space), `n`: the ground's normal,
// `hidden`: the share of the sun the map and the clouds hide. `spec`: the specular lights' part, which the ground adds
// again after its texture (ground_light), for the print to add after its colour.
float3 ground_shade(float3 p, float3 n, float hidden, float2 turn, out float3 spec) {
    float3 ambient = part.ambient.rgb;
    float facing = dot(part.sun.xyz, n);
    float3 sunlit = max(facing > 0.0 ? min(1.0, facing * part.sunColour.rgb) : 0.0, ambient);
    float3 daylight = max(facing > 0.0 ? min(1.0, facing * part.daySunColour.rgb) : 0.0, part.dayAmbient.rgb);
    float shade = 1.0 - hidden * shadow.strength * saturate(-dot(part.mapSun.xyz, n) * 6.0);
    float3 lit = min(lerp(sunlit, min(1.0, ambient), hidden), sunlit * shade);
    float3 open = sunlit;
    spec = 0.0;
    uint2 span = cell_lights(p);
    for (uint k = 0; k < span.y; ++k) {
        PointLight l = pointLights[grid_word(span.x + k)];
        float3 d = l.at.xyz - p;
        float distance2 = dot(d, d);
        float falloff = 1.0 - distance2 * l.at.w;
        if (falloff < 0.0) continue;
        float c = dot(n, d * rsqrt(max(distance2, 1e-12)));
        float3 v = (c <= 0.0 ? c + 1.0 : 1.0) * falloff * l.colour.rgb;
        float seen = l.shadow.x >= 0 ? point_seen(l.shadow.x, p, n, kNoPointFront, turn) : 1.0;
        if (l.colour.w != 0.0) spec = max(spec, min(1.0, v * seen) * shade);
        else lit = max(lit, min(1.0, v * seen) * shade);
        open = max(open, min(1.0, v));
    }
    float3 full = max(open, daylight);
    return lerp(1.0, saturate(min(1.0, max(lit, spec)) / max(full, 1e-3)), step(1e-3, full));
}
float4 arg(int a, float4 current, float4 diffuse, float4 texel, float4 spec) {
    int sel = a & 15;
    float4 v = sel == 0 ? diffuse : sel == 2 ? texel : sel == 3 ? fs.factor : sel == 4 ? spec : current;
    if ((a & 16) != 0) v = 1.0 - v;
    if ((a & 32) != 0) v = v.aaaa;
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
    if (op == 14) return lerp(a2, a1, fs.factor.a);
    if (op == 15) return a1 + a2 * (1.0 - texel.a);
    if (op == 16) return lerp(a2, a1, current.a);
    if (op == 18) return float4(a1.rgb + a1.a * a2.rgb, a1.a);
    if (op == 19) return float4(a1.rgb * a2.rgb, a1.a + a2.a);
    if (op == 20) return float4(a1.rgb + (1.0 - a1.a) * a2.rgb, a1.a);
    if (op == 24) return dot(a1.rgb * 2.0 - 1.0, a2.rgb * 2.0 - 1.0).xxxx;
    return a1 * a2;   // 4 MODULATE and anything not handled
}
// The weather on a world mesh's pixel of colour `rgb` lit by `light`, from its face (the map's clip space's derivatives
// `dsx`, `dsy` at `s` taken to the landscape's space and turned to the camera): snow on faces turned up (not on a
// unit), wet faces darker with a sheen of the sun (a unit only darker: its faces are few and large); none under a roof
// (roof_open; a unit has its own shelter). `hidden`: the share of the sun the map and the clouds hide. Without the
// map's space (no fit: `shadow.scale` 0) none.
static const float3 kSnow = float3(0.76, 0.78, 0.83);
float3 weathered_mesh(float3 rgb, float3 light, float3 s, float3 dsx, float3 dsy, bool unit, float hidden) {
    float3 n = cross(map_to_landscape(dsx * shadow.scale), map_to_landscape(dsy * shadow.scale));
    float len = length(n);
    if (len <= 1e-20) return rgb;
    n /= len;
    float3 v = map_to_landscape((shadow.camera - s) * shadow.scale);
    if (dot(n, v) < 0.0) n = -n;
    v = normalize(v);
    float open = unit ? 1.0 : roof_open(s, n);
    float cover = draw.weather.y;
    float snow = unit ? 0.0 : smoothstep(lerp(1.1, 0.6, cover), lerp(1.3, 0.85, cover), n.z) * saturate(cover * 3.0) * 0.85;
    snow *= open;
    rgb = lerp(rgb, kSnow * saturate(light), snow);
    float wet = draw.weather.x * (1.0 - snow) * open;
    rgb *= lerp(1.0, 0.65, wet);
    if (!unit) {
        float3 h = normalize(fs.weatherSun + v);
        float sunUp = saturate(fs.weatherSun.z * 4.0) * (1.0 - hidden);
        rgb += fs.weatherSunColour * sunUp * pow(saturate(dot(n, h)), 40.0) * 0.2 * wet;
    }
    return saturate(rgb);
}
// A value noise in 0 .. 1 over `p` (a lattice of hashed corners, smoothly blended).
float hash3(float3 c) { return frac(sin(dot(c, float3(12.9898, 78.233, 37.719))) * 43758.5453); }
float value_noise(float3 p) {
    float3 c = floor(p), f = p - c;
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(lerp(hash3(c), hash3(c + float3(1, 0, 0)), f.x), lerp(hash3(c + float3(0, 1, 0)), hash3(c + float3(1, 1, 0)), f.x), f.y);
    float b = lerp(lerp(hash3(c + float3(0, 0, 1)), hash3(c + float3(1, 0, 1)), f.x), lerp(hash3(c + float3(0, 1, 1)), hash3(c + 1.0), f.x), f.y);
    return lerp(a, b, f.z);
}
// A unit's snow and wetness on the pixel of colour `rgb` lit by `light`, the normal's up component `up0` (the noise
// fixed to the part, `model`, breaks it): snow dusted where it turns up, then wet parts darker. `wet` gets the share left
// wet, for the sheen.
float3 unit_snow_and_wet(float3 rgb, float3 light, float up0, float3 model, out float wet) {
    float cover = draw.weather.y;
    float grain = value_noise(model * 7.0) * 0.6 + value_noise(model * 19.0) * 0.4;
    float up = up0 + (grain - 0.5) * 0.35;
    // A dusting: a soft edge, and at most 0.45 .. 0.8 of the part (by the noise), so its texture shows through.
    float snow = smoothstep(lerp(0.95, 0.4, cover), lerp(1.25, 0.9, cover), up) * saturate(cover * 2.0) *
                 (0.45 + 0.35 * grain);
    // The snow shaded by the texture under it (its light against the pixel's light) and the noise, so a dusted back
    // keeps its shape.
    float albedo = saturate(dot(rgb, 1.0 / 3.0) / max(dot(light, 1.0 / 3.0), 0.05));
    rgb = lerp(rgb, kSnow * saturate(light) * (0.7 + 0.3 * albedo + 0.08 * grain), snow);
    wet = draw.weather.x * (1.0 - snow);
    return rgb * lerp(1.0, 0.6, wet);
}
// The weather on a unit's figure part lit on the GPU, per pixel from its interpolated vertex normal (`mapNormal`, in the
// map's clip space, taken to the landscape's; a low-poly figure's faces are too few and large for a face normal):
// snow settles where it turns up (the head, the shoulders, the tops of armour and backs), broken by a noise fixed to
// the part (`model`) so it rides with the unit; wet parts darker with a sheen: the sky they reflect (upward, so on the
// same tops; the ambient light's colour, Schlick's term) and the sun's highlight. `light`: the pixel's lit colour.
float3 weathered_unit(float3 rgb, float3 light, float3 s, float3 mapNormal, float3 model, float hidden) {
    float3 n = map_to_landscape(mapNormal * shadow.scale);
    float len = length(n);
    if (len <= 1e-20) return rgb;
    n /= len;
    float3 v = normalize(map_to_landscape((shadow.camera - s) * shadow.scale));
    float wet;
    rgb = unit_snow_and_wet(rgb, light, n.z, model, wet);
    float3 r = reflect(-v, n);
    float fresnel = 0.04 + 0.96 * pow(1.0 - saturate(dot(n, v)), 5.0);
    float sky = pow(saturate(r.z), 3.0) * (0.3 + fresnel);
    float3 h = normalize(fs.weatherSun + v);
    float sunUp = saturate(fs.weatherSun.z * 4.0) * (1.0 - hidden);
    float3 sheen = saturate(part.ambient.rgb + 0.1) * sky * 0.35 +
                   fs.weatherSunColour * sunUp * pow(saturate(dot(n, h)), 30.0) * 0.4;
    rgb += sheen * wet;
    return saturate(rgb);
}
// The same for an interface figure of a world unit (the HUD unit panel's; MeshRecord::kinds.z kFigureWeather): it has no map's
// space, no sun and no ambient light, so by its model-space normal (a unit's model stands up along z, as in the world)
// with a fixed sheen on the faces turned up.
float3 weathered_figure(float3 rgb, float3 light, float3 normal, float3 model) {
    float len = length(normal);
    if (len <= 1e-20) return rgb;
    float3 n = normal / len;
    float wet;
    rgb = unit_snow_and_wet(rgb, light, n.z, model, wet);
    rgb += pow(saturate(n.z), 3.0) * 0.08 * wet;
    return saturate(rgb);
}
// HLSL's round takes halves to the even neighbour, as the GLSL's roundEven.
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
// A texture stage: its colour and alpha operations (op, arg1, arg2) on `texel`, clamped.
float4 stage(int3 colourOp, int3 alphaOp, float4 current, float4 diffuse, float4 texel, float4 spec) {
    float3 c = combine(colourOp.x, arg(colourOp.y, current, diffuse, texel, spec),
                       arg(colourOp.z, current, diffuse, texel, spec), current, diffuse, texel).rgb;
    float a = current.a;
    if (alphaOp.x > 1)
        a = combine(alphaOp.x, arg(alphaOp.y, current, diffuse, texel, spec),
                    arg(alphaOp.z, current, diffuse, texel, spec), current, diffuse, texel).a;
    return saturate(float4(c, a));
}

float4 Shade(VsOut i) {
    float3 dsx = ddx(i.shadow.xyz), dsy = ddy(i.shadow.xyz);
    // The face normal in the landscape's space, turned to the camera (point shadows).
    float3 face = cross(ddx(i.land), ddy(i.land));
    float faceLength = length(face);
    face = faceLength > 1e-20 ? face / faceLength : float3(0.0, 0.0, 1.0);
    if (dot(face, points.eye - i.land) < 0.0) face = -face;
    // How much of the sun the shadow map hides. A unit's mesh: the shadows other objects cast, on every face (none of
    // its own: its sun side is shaded by the lighting's N.L; by face it flipped triangle by triangle on a low-poly
    // creature). Any other mesh: on faces turned to the sun only, by their face normal (the smoothed vertex normals of
    // many static meshes lean far from their faces: a plank's top read sideways). The clouds' alike. Every mesh is
    // darkened by it alike, the colour lit here or by the game, the lamps' light and the ambient light with the sun's
    // (as before: in the towns the ambient light is nearly the sun's).
    // A ground print takes it as the ground does (ground_shade).
    bool unit = draw.flags.w != 0;
    bool groundPrint = draw.lit.y != 0;
    float hidden = with_clouds(shadow_amount(i.shadow.xyz, dsx, dsy, i.pos.xy, unit ? draw.uvOffset.z : 2.0),
                               i.shadow.xy);
    if (!unit && !groundPrint && hidden > 0.0) hidden *= shadow_facing(i.shadow.xyz, dsx, dsy);
    float4 diffuse = i.col;
    float occlusion = ambient_occlusion(i.pos.xy, i.pos.z, draw.kinds.w, fs.occlusionDepthOffset,
                                        fs.occlusionDepthScale, fs.occlusionScale, fs.occlusionTexel);
    if (draw.lit.x != 0) {
        diffuse.rgb = figure_light(i.normal, i.land, face, pixel_turn(i.pos.xy), occlusion) *
                      occlusion_direct(occlusion);
    }
    float4 current = diffuse;
    float4 t0 = tex0.Sample(smp0, i.uv);
    // Stage 0's patch: inside its rectangle the texels come from the patch texture (a unit's face).
    float2 w = i.uv - floor(i.uv);
    float4 tp = tex2.Sample(smp2, clamp(w * fs.patchScale + fs.patchOffset, fs.patchInside.xy, fs.patchInside.zw));
    if (all(w >= fs.patchTo.xy) && all(w < fs.patchTo.zw)) t0 = tp;
    if (fs.colourStage0.x > 1) {
        current = stage(fs.colourStage0, fs.alphaStage0, current, diffuse, t0, i.spec);
        if (fs.colourStage1.x > 1) {
            float4 t1 = tex1.Sample(smp1, i.uv);
            current = stage(fs.colourStage1, fs.alphaStage1, current, diffuse, t1, i.spec);
        }
    }
    if (fs.alphaFunc != 0 && !passes(fs.alphaFunc, current.a, fs.alphaReference)) discard;
    if (draw.lit.w != 0) return float4(occlusion.xxx, current.a);   // the harness's occlusion view
    float3 rgb = current.rgb;
    if (fs.specular != 0) rgb = saturate(rgb + i.spec.rgb);
    if (shadow.shadowView != 0) return float4((1.0 - hidden).xxx, current.a);   // the harness's shadowview
    if (draw.weather.x > 0.0 || draw.weather.y > 0.0) {
        if (draw.kinds.z == kFigureWeather)
            rgb = weathered_figure(rgb, diffuse.rgb, i.normal, i.model);
        else if (draw.kinds.z == kUnitWeather && draw.lit.x != 0)
            rgb = weathered_unit(rgb, diffuse.rgb, i.shadow.xyz, i.mapNormal, i.model, hidden);
        else
            rgb = weathered_mesh(rgb, diffuse.rgb, i.shadow.xyz, dsx, dsy, unit, hidden);
    }
    if (groundPrint) {
        float3 up = face.z < 0.0 ? -face : face;   // the ground's, turned up
        float3 spec;
        float3 shade = ground_shade(i.land, up, hidden, pixel_turn(i.pos.xy), spec);
        rgb = saturate(rgb * shade + spec);
    } else {
        rgb *= 1.0 - hidden * shadow.strength;
    }
    if (fs.fog != 0) rgb = lerp(fs.fogColour, rgb, i.spec.a);
    return float4(rgb, current.a);
}

DepthCopyOut ps_main(VsOut i) {
    drawRecord = i.record;
    drawLight = records[i.record].lights.x;
    return WithDepthCopy(Shade(i), i.pos.z);
}
#endif
